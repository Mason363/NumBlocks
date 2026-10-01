/* The mobs' models: Minecraft 1.8's boxes (ModelBiped, ModelQuadruped and the
 * others), in model pixels: y points down, the feet at y = 24, the face
 * towards -z. Animated per frame by mob_parts (mob.c). */
#ifndef NB_MOB_H
#define NB_MOB_H
#include "nb.h"

typedef struct {
  float x, y, z;         /* the box's corner */
  uint8_t w, h, d;       /* its size */
  uint8_t u, v;          /* where its faces start in the skin */
  uint8_t mirror, skin;
  float px, py, pz;      /* the point it turns about */
  float rx, ry, rz;      /* its turn (radians), Z then Y then X */
  float grow;            /* inflated by this much on every side (sheep's wool) */
} Part;

#define MAX_PARTS 12
int mob_parts(const Entity *e, float pt, Part *out);   /* this frame's parts */
float mob_width(int type), mob_height(int type);
float mob_scale(const Entity *e, float pt);            /* a creeper swells before it blows */
#endif
