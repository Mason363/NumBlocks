# NumBlocks: a game like Minecraft 1.8.8 for the NumWorks calculator (part of NumPlay).
#   make            output/numblocks.nwa
#   make check      link it like the calculator does (output/numblocks.bin)
#   make host       build/play: the game on a computer, for tests and screenshots
#   make data       src/data.c from Minecraft 1.8.8's client.jar (CLIENT_JAR=path)
Q ?= @
CC = arm-none-eabi-gcc
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output
STRIP ?= arm-none-eabi-strip --strip-unneeded
CLIENT_JAR ?= client.jar
GEN ?= src/gen.c
GAME = src/main.c src/world.c src/edits.c src/render.c src/player.c src/phys.c src/inv.c src/entity.c src/mob.c src/tick.c src/gui.c src/hand.c src/save.c src/data.c src/names.c src/command.c $(GEN)
HEADERS = $(wildcard src/*.h) common/epsilon_app.h common/epsilon_files.h Makefile

CFLAGS = -std=gnu11 $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -fno-math-errno -fsingle-precision-constant -ffast-math
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
CFLAGS += -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: build check clean host data
build: $(BUILD_DIR)/numblocks.nwa

check: $(BUILD_DIR)/numblocks.nwa
	$(Q) $(NWLINK) nwa-bin --ram-length 153676 $< $(BUILD_DIR)/numblocks.bin
	@echo "BIN     $(BUILD_DIR)/numblocks.bin: $$(wc -c < $(BUILD_DIR)/numblocks.bin) bytes"

$(BUILD_DIR)/numblocks.nwa: $(GAME) src/plat_eadk.c $(HEADERS) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $(GAME) src/plat_eadk.c $(BUILD_DIR)/icon.o -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) arm-none-eabi-size $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
	$(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
	$(Q) mkdir -p $@

HOST_FLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Isrc -DHOST
host: build/play build/play_ref
build/play: $(GAME) src/plat_host.c tests/play.c $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) $(GAME) src/plat_host.c tests/play.c -lm -o $@
	@echo "HOST    $@"
build/unit: $(GAME) src/plat_host.c tests/unit.c $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) $(filter-out src/main.c,$(GAME)) src/plat_host.c tests/unit.c tests/stubs.c -lm -o $@
test: build/unit
	@mkdir -p build/unit-saves
	./build/unit
# every pixel traced: the reference the adaptive picture is checked against
build/play_ref: $(GAME) src/plat_host.c tests/play.c $(HEADERS)
	$(Q) mkdir -p build
	$(Q) cc $(HOST_FLAGS) -DFULL_TRACE $(GAME) src/plat_host.c tests/play.c -lm -o $@
	@echo "HOST    $@"

data:
	python3 tools/blocks.py --header > src/blocks.h
	python3 tools/items.py --header > src/items.h
	python3 tools/pack.py $(CLIENT_JAR)

clean:
	$(Q) rm -rf $(BUILD_DIR) build

# NumPlay launcher: the game as a module (see ../../tools/npmodule.py) for the
# calculator, and as a relocatable object for the simulator build.
.PHONY: module sim-module
module: $(BUILD_DIR)/module.o
$(BUILD_DIR)/module.o: $(GAME) src/plat_eadk.c $(HEADERS) | $(BUILD_DIR)
	$(Q) $(CC) $(CFLAGS) -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main -flinker-output=nolto-rel \
	  $(GAME) src/plat_eadk.c -o $@

sim-module: $(BUILD_DIR)/sim-module.o
$(BUILD_DIR)/sim-module.o: $(GAME) src/plat_eadk.c $(HEADERS) | $(BUILD_DIR)
	$(Q) mkdir -p $(BUILD_DIR)/sim
	$(Q) for f in $(basename $(notdir $(GAME) src/plat_eadk.c)); do cc -std=gnu11 -O2 -fPIC \
	  $(shell $(NWLINK) eadk-cflags-simulator) -fno-math-errno -Dmain=np_numblocks_main -c src/$$f.c \
	  -o $(BUILD_DIR)/sim/$$f.o || exit 1; done
	$(Q) ld -r $(BUILD_DIR)/sim/*.o -exported_symbol _np_numblocks_main -o $@
