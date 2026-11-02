#ifndef STILLWATER_ART_H
#define STILLWATER_ART_H
#include <cairo.h>
#include <stdint.h>
#define ART_SCENES 14
// One character cell: an optional colored ground, an optional glyph, and its
// ink. A zero bg paints the scene sky or water gradient; a zero glyph is a
// solid ground block. Scenes use both to build layered, shaded artwork.
typedef struct {
  uint32_t bg : 24;
  uint32_t fg : 24;
  uint32_t c : 8;
} Cell;
typedef struct {
  int width, height, cols, rows, scene;
  double cw, ch;
  Cell *cells, *base;
  struct ArtCache *cache;
} Art;
int art_init(Art *a, int width, int height, int scene);
// Mid-scene backdrop color; the full vertical gradient is painted by
// art_background() and by every cell the renderer touches.
uint32_t art_bg(const Art *a);
void art_background(Art *a, unsigned char *pixels, int stride, int scale);
void art_free(Art *a);
void art_frame(Art *a, double time);
void art_paint(Art *a, cairo_t *cr, Cell *previous);
int art_damage(Art *a, Cell *presented,
               void (*emit)(void *, int, int, int, int), void *data);
int art_blit(Art *a, unsigned char *pixels, int stride, int scale,
             Cell *previous);
#endif
