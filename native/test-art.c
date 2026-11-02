#include "art.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  unsigned char *target, *source;
  int width, height, stride, scale;
  size_t area;
} Damage;
static void apply_damage(void *data, int x, int y, int w, int h) {
  Damage *d = data;
  assert(x >= 0 && y >= 0 && w > 0 && h > 0);
  assert(x + w <= d->width && y + h <= d->height);
  d->area += (size_t)w * h;
  for (int row = y * d->scale; row < (y + h) * d->scale; row++)
    memcpy(d->target + row * d->stride + x * d->scale * 4,
           d->source + row * d->stride + x * d->scale * 4,
           (size_t)w * d->scale * 4);
}
static void fill(cairo_surface_t *surface, Art *a, int scale) {
  cairo_surface_flush(surface);
  art_background(a, cairo_image_surface_get_data(surface),
                 cairo_image_surface_get_stride(surface), scale);
  cairo_surface_mark_dirty(surface);
}
int main(void) {
  assert(sizeof(Cell) == 8);
  Art invalid;
  assert(art_init(&invalid, 0, 1080, 0) == -1);
  assert(art_init(&invalid, 1920, 1080, ART_SCENES) == -1);
  // Scenes are deliberately either night pieces or daylight pieces, and each
  // backdrop is distinct. Pond, sky, mountains and forest are dark; the rain
  // garden and dunes are drawn for daylight.
  const int daylight[ART_SCENES] = {0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0};
  uint32_t backgrounds[ART_SCENES];
  for (int scene = 0; scene < ART_SCENES; scene++) {
    Art a;
    assert(art_init(&a, 390, 844, scene) == 0);
    backgrounds[scene] = art_bg(&a);
    double luminance = .2126 * ((backgrounds[scene] >> 16) & 255) +
                       .7152 * ((backgrounds[scene] >> 8) & 255) +
                       .0722 * (backgrounds[scene] & 255);
    if (daylight[scene])
      assert(luminance > 150);
    else if (scene < 6)
      assert(luminance < 40);
    else
      assert(luminance < 180);
    for (int previous = 0; previous < scene; previous++)
      assert(backgrounds[scene] != backgrounds[previous]);
    art_free(&a);
  }
  const int sizes[][2] = {{1, 1}, {390, 844}, {1920, 1080}};
  for (int scene = 0; scene < ART_SCENES; scene++)
    for (int z = 0; z < 3; z++)
      for (int scale = 1; scale <= 2; scale++) {
          int w = sizes[z][0], h = sizes[z][1];
          Art a;
          assert(art_init(&a, w, h, scene) == 0);
          size_t cell_bytes = (size_t)a.cols * a.rows * sizeof(Cell);
          Cell *previous[2] = {calloc(1, cell_bytes), calloc(1, cell_bytes)};
          Cell *presented = malloc(cell_bytes);
          assert(previous[0] && previous[1] && presented);
          memset(presented, 0xff, cell_bytes);
          cairo_surface_t *images[2];
          for (int b = 0; b < 2; b++) {
            images[b] = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                   w * scale, h * scale);
            fill(images[b], &a, scale);
          }
          int stride = cairo_image_surface_get_stride(images[0]);
          size_t pixel_bytes = (size_t)stride * h * scale;
          unsigned char *screen = calloc(1, pixel_bytes);
          assert(screen);
          for (int f = 0; f < 10; f++) {
            // Alternate buffers, including returning to an earlier frame:
            // damage must follow the presented image, not the older buffer's
            // contents.
            art_frame(&a, f % 3 == 2 ? 0 : f * 3.2);
            cairo_surface_t *image = images[f % 2];
            cairo_surface_flush(image);
            unsigned char *source = cairo_image_surface_get_data(image);
            assert(art_blit(&a, source, stride, scale, previous[f % 2]) == 0);
            cairo_surface_mark_dirty(image);
            Damage damage = {screen, source, w, h, stride, scale, 0};
            art_damage(&a, presented, apply_damage, &damage);
            assert(art_damage(&a, presented, apply_damage, &damage) == 0);
            cairo_surface_t *fresh = cairo_image_surface_create(
                CAIRO_FORMAT_RGB24, w * scale, h * scale);
            fill(fresh, &a, scale);
            cairo_t *cr = cairo_create(fresh);
            cairo_scale(cr, scale, scale);
            art_paint(&a, cr, NULL);
            cairo_surface_flush(fresh);
            unsigned char *expected = cairo_image_surface_get_data(fresh);
            if (memcmp(source, expected, pixel_bytes) ||
                memcmp(screen, expected, pixel_bytes)) {
              fprintf(stderr,
                      "Mismatch scene=%d size=%dx%d scale=%d frame=%d\n", scene,
                      w, h, scale, f);
              abort();
            }
            cairo_destroy(cr);
            cairo_surface_destroy(fresh);
          }
          for (int b = 0; b < 2; b++) {
            free(previous[b]);
            cairo_surface_destroy(images[b]);
          }
          free(presented);
          free(screen);
          art_free(&a);
        }
  puts("PASS: cached tiles and partial damage match full Cairo rendering for 14 "
       "landscapes × 3 sizes × 2 scales × 10 frames, including alternating "
       "buffer reuse. Explicit night/day backdrops, invalid input and "
       "unchanged damage checks passed.");
  return 0;
}
