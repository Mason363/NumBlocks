#!/usr/bin/env python3
"""Builds bench.c + gen.c for the Cortex-M7 (-Os, hard float) and runs it in Unicorn,
counting instructions per gen_slab call (1 instruction ~ 1 cycle at 216 MHz, the same
convention as tools/emu.py) in the game's calling pattern (see bench.c). Also reports the
deepest stack use (painted stack) and gen.c's own static RAM.
Usage: bench.py [OUTDIR] [--no-spawn] [--profile] [--lines FUNC,...] [--y0 N] [--h N] [--seed S] [--ox CX] [--oz CZ]
                [--game-flags]"""
import os
import subprocess
import sys

from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "..", "src")
args = [a for a in sys.argv[1:] if not a.startswith("--")]
OUT = args[0] if args else "/tmp"
def opt(name, default):
    global args
    if name not in sys.argv:
        return default
    v = sys.argv[sys.argv.index(name) + 1]
    args = [a for a in args if a != v]
    return int(v)


Y0 = opt("--y0", 56)
H = opt("--h", 24)
SEED = opt("--seed", 12345)
OX = opt("--ox", 0)
OZ = opt("--oz", 0)
NO_SPAWN = "--no-spawn" in sys.argv
LINES = sys.argv[sys.argv.index("--lines") + 1] if "--lines" in sys.argv else None
if LINES:
    args = [a for a in args if a != LINES]
PROFILE = "--profile" in sys.argv or "--lines" in sys.argv
ELF = os.path.join(OUT, "gen_bench.elf")
CFLAGS = ["-mcpu=cortex-m7", "-mfpu=fpv5-sp-d16", "-mfloat-abi=hard", "-mthumb", "-Os", "-std=c11",
          "-Wall", "-Wextra", "-Wdouble-promotion", "-ffunction-sections", "-fdata-sections", "-g"]
if "--game-flags" in sys.argv:  # the game's build of gen.c
    CFLAGS = ["-std=gnu11", "-mcpu=cortex-m7", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv5-sp-d16", "-O2",
              "-ffunction-sections", "-fdata-sections", "-fsingle-precision-constant", "-ffast-math", "-g",
              "-Wall", "-Wextra", "-Wdouble-promotion"]
subprocess.check_call(["arm-none-eabi-gcc", *CFLAGS, "-nostartfiles", "--specs=nano.specs", "--specs=nosys.specs",
                       "-T", os.path.join(HERE, "bench.ld"), "-Wl,--gc-sections",
                       os.path.join(HERE, "bench.c"), os.path.join(SRC, "gen.c"), "-lm", "-o", ELF,
                       f"-DY0={Y0}", f"-DH={H}", f"-DSEED={SEED}LL", f"-DOX={OX}", f"-DOZ={OZ}"] + (["-DNO_SPAWN"] if NO_SPAWN else []))
OBJ = os.path.join(OUT, "gen_arm.o")
subprocess.check_call(["arm-none-eabi-gcc", *CFLAGS, "-c", os.path.join(SRC, "gen.c"), "-o", OBJ])
print("gen.c alone:")
subprocess.call(["arm-none-eabi-size", OBJ])

syms = {}
funcs = []
with open(ELF, "rb") as f:
    elf = ELFFile(f)
    for s in elf.get_section_by_name(".symtab").iter_symbols():
        syms[s.name] = s["st_value"]
        if s["st_info"]["type"] == "STT_FUNC" and s["st_size"]:
            funcs.append((s["st_value"] & ~1, s["st_size"], s.name))
    segs = [(p["p_paddr"], p.data()) for p in elf.iter_segments() if p["p_type"] == "PT_LOAD"]

uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
uc.mem_map(0x08000000, 1 << 20)
uc.mem_map(0x20000000, 512 << 10)
for addr, data in segs:
    uc.mem_write(addr, data)
STACK_TOP = 0x20000000 + (512 << 10)
PAINT = 64 << 10
uc.mem_write(STACK_TOP - PAINT, b"\xa5" * PAINT)
vec = uc.mem_read(0x08000000, 8)
sp = int.from_bytes(vec[0:4], "little")
pc = int.from_bytes(vec[4:8], "little")
uc.reg_write(UC_ARM_REG_SP, sp)

CHUNK = 21600 if not PROFILE else 997  # 0.1 ms (prime-ish when sampling)
samples = {}
lrs = {}
insns = 0
marks = []
last_phase = 0
phase_addr = syms["phase"]
while True:
    try:
        uc.emu_start(pc | 1, 0xFFFFFFFF, count=CHUNK)
    except Exception as e:
        p = uc.reg_read(UC_ARM_REG_PC)
        if uc.mem_read(p & ~1, 2) == b"\x00\xbe":  # bkpt: done
            ph = int.from_bytes(uc.mem_read(phase_addr, 4), "little")
            if ph != last_phase:
                marks.append((ph, insns))
            break
        print(f"fault {e} at pc {p:#x} after ~{insns} insns", file=sys.stderr)
        raise
    insns += CHUNK
    pc = uc.reg_read(UC_ARM_REG_PC)
    if PROFILE:
        samples[pc & ~1] = samples.get(pc & ~1, 0) + 1
        lrs[pc & ~1] = uc.reg_read(UC_ARM_REG_LR) & ~1
    ph = int.from_bytes(uc.mem_read(phase_addr, 4), "little")
    if ph != last_phase:
        marks.append((ph, insns))
        last_phase = ph
    op = uc.mem_read(pc & ~1, 2)
    if op == b"\x00\xbe":  # bkpt
        break
    if insns > 20_000_000_000:
        break

prev = 0
NCALLS = 12 + 9 + 8
names = ["gen_init"] + [f"gen_slab {Y0}..{Y0 + H} #{i}" for i in range(NCALLS)] + ["gen_spawn"]
slabs = []
for i, (ph, n) in enumerate(marks):
    d = n - prev
    prev = n
    name = names[i] if i < len(names) else f"phase {ph}"
    if name.startswith("gen_slab"):
        slabs.append(d)
    print(f"{name:22s} {d / 1e6:8.2f} M insns  ~{d / 216e3:7.1f} ms at 216 MHz")
if slabs:
    print(f"gen_slab (+ 256 gen_top): avg {sum(slabs) / len(slabs) / 1e6:.2f} M insns = "
          f"{sum(slabs) / len(slabs) / 216e3:.1f} ms; load (12 calls) avg {sum(slabs[:12]) / 12 / 216e3:.1f} ms, "
          f"moves (17 calls) avg {sum(slabs[12:]) / max(1, len(slabs) - 12) / 216e3:.1f} ms, max {max(slabs) / 216e3:.1f} ms")
st = uc.mem_read(STACK_TOP - PAINT, PAINT)
used = PAINT - next(i for i in range(PAINT) if st[i] != 0xA5)
print(f"deepest stack: {used} bytes")
sums = uc.mem_read(syms["sums"], 64)
print("checksums:", " ".join(f"{int.from_bytes(sums[i:i + 4], 'little'):08x}" for i in range(0, 64, 4)))
if not NO_SPAWN:
    sp3 = uc.mem_read(syms["spawn"], 12)
    print("spawn:", [int.from_bytes(sp3[i:i + 4], "little", signed=True) for i in range(0, 12, 4)])

if PROFILE:
    import bisect
    funcs.sort()
    starts = [f[0] for f in funcs]
    per = {}
    total = sum(samples.values())
    for a, n in samples.items():
        i = bisect.bisect_right(starts, a) - 1
        name = funcs[i][2] if i >= 0 and a < funcs[i][0] + funcs[i][1] else f"{a:#x}"
        per[name] = per.get(name, 0) + n
    for name, n in sorted(per.items(), key=lambda t: -t[1])[:40]:
        print(f"{100 * n / total:6.2f}%  {name}")
    # helpers (libgcc): attribute to the caller's source line through lr
    helper = {}
    for a, n in samples.items():
        i = bisect.bisect_right(starts, a) - 1
        if i >= 0 and funcs[i][2].startswith("__"):
            helper[lrs[a]] = helper.get(lrs[a], 0) + n
    top = sorted(helper.items(), key=lambda t: -t[1])[:25]
    if top:
        out = subprocess.run(["arm-none-eabi-addr2line", "-f", "-i", "-e", ELF] + [hex(a - 2) for a, _ in top],
                             capture_output=True, text=True).stdout.split("\n")
        print("libgcc helper time by call site:")
        for k, (a, n) in enumerate(top):
            print(f"{100 * n / total:6.2f}%  {a:#x}")
        print("\n".join(out))

if LINES:
    tot = sum(samples.values())
    for fname in LINES.split(","):
        lo = hi = None
        for a0, sz, name in funcs:
            if name == fname:
                lo, hi = a0, a0 + sz
        pcs = {a: n for a, n in samples.items() if lo is not None and lo <= a < hi}
        out = subprocess.run(["arm-none-eabi-addr2line", "-e", ELF] + [hex(a) for a in pcs],
                             capture_output=True, text=True).stdout.split("\n")
        per = {}
        for (a, n), line in zip(pcs.items(), out):
            line = line.split("/")[-1]
            per[line] = per.get(line, 0) + n
        print(f"lines of {fname}:")
        for line, n in sorted(per.items(), key=lambda t: -t[1])[:12]:
            print(f"{100 * n / tot:6.2f}%  {line}")
