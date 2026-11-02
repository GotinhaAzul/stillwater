#ifndef STILLWATER_ART_H
#define STILLWATER_ART_H
#include <cairo.h>
#include <stdint.h>
#define ART_SCENES 6
#define ART_THEMES 10
typedef struct {
  uint32_t color : 24;
  uint32_t c : 8;
} Cell;
typedef struct {
  int width, height, cols, rows, scene, theme;
  double cw, ch;
  Cell *cells, *base;
  struct ArtCache *cache;
} Art;
int art_init(Art *a, int width, int height, int scene);
int art_set_theme(Art *a, int theme);
uint32_t art_bg(const Art *a);
void art_free(Art *a);
void art_frame(Art *a, double time);
void art_paint(Art *a, cairo_t *cr, Cell *previous);
int art_damage(Art *a, Cell *presented,
               void (*emit)(void *, int, int, int, int), void *data);
int art_blit(Art *a, unsigned char *pixels, int stride, int scale,
             Cell *previous);
uint32_t art_background(int scene);
#endif
