#include "art.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Character cells hold a ground colour, an ink and a glyph. Scenes paint
// grounds as solid blocks and glyphs as texture, which is what gives the
// landscapes their layered, pixel-art depth without a per-pixel surface.
#define TILE_SLOTS 4096
typedef struct {
  uint32_t bg, fg;
  uint32_t c;
  uint32_t *pixels;
} Tile;
typedef struct {
  int index, phase;
  double speed;
  char c;
} Star;
struct ArtCache {
  Star *stars;
  int star_count, scale, tile_width, tile_height;
  unsigned char *water_noise;
  double *waves;
  Tile tiles[TILE_SLOTS];
};

#include "scene-palettes.h"

// Backdrop stops sit at these fractions of the output height.
#define TAU 6.283185307179586
static const double bg_positions[5] = {0, .26, .46, .70, 1};

static void cache_free(struct ArtCache *c) {
  if (!c)
    return;
  for (int i = 0; i < TILE_SLOTS; i++)
    free(c->tiles[i].pixels);
  free(c->stars);
  free(c->water_noise);
  free(c->waves);
  free(c);
}
static double clamp(double v, double lo, double hi) {
  return fmax(lo, fmin(hi, v));
}
static double hash(double x, double y) {
  double n = sin(x * 127.1 + y * 311.7) * 43758.5453;
  return n - floor(n);
}
static double noise2(double x, double y) {
  double ix = floor(x), iy = floor(y), fx = x - ix, fy = y - iy;
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  double a = hash(ix, iy), b = hash(ix + 1, iy), c = hash(ix, iy + 1),
         d = hash(ix + 1, iy + 1);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
// Ordered dither; shading ramps step between neighbouring tones instead of
// banding across large surfaces.
static double bayer(int x, int y) {
  static const unsigned char matrix[16] = {0, 8,  2,  10, 12, 4,  14, 6,
                                           3, 11, 1,  9,  15, 7,  13, 5};
  return (matrix[((y & 3) << 2) | (x & 3)] + .5) / 16.;
}
static uint32_t mix_color(uint32_t from, uint32_t to, double amount) {
  uint32_t result = 0;
  amount = clamp(amount, 0, 1);
  for (int shift = 0; shift <= 16; shift += 8) {
    double first = (from >> shift) & 255, last = (to >> shift) & 255;
    result |= (uint32_t)round(first + (last - first) * amount) << shift;
  }
  return result;
}
static uint32_t ramp_color(const uint32_t *colors, int count, double value,
                           int x, int y) {
  // Flat tones with a narrow dithered seam: large faces stay calm and only
  // the transition between two neighbouring tones breaks up.
  if (count < 2)
    return colors[0];
  double scaled = clamp(value, 0, 1) * (count - 1) + (bayer(x, y) - .5) * .12;
  int index = (int)floor(scaled);
  if (index < 0)
    index = 0;
  if (index >= count)
    index = count - 1;
  return colors[index];
}

// Scene backgrounds are vertical gradients. Pond, sky, rain and dunes use
// palettes of their own; mountains and forest keep one Midnight colour.
static const uint32_t extra_bg[8][5];
static const uint32_t *scene_bg(const Art *a) {
  if (a->scene >= 6) return extra_bg[a->scene-6];
  switch (a->scene) {
  case 0: return pond_colors.bg;
  case 1: return sky_colors.bg;
  case 4: return rain_colors.bg;
  case 5: return dunes_colors.bg;
  default: return scene_palettes[a->scene].bg;
  }
}
static uint32_t bg_row(const Art *a, int row) {
  const uint32_t *stops = scene_bg(a);
  double t = (clamp(row, 0, a->rows - 1) + .5) / (double)a->rows;
  int k = 0;
  while (k < 3 && t > bg_positions[k + 1])
    k++;
  double span = bg_positions[k + 1] - bg_positions[k];
  double amount = span > 0 ? (t - bg_positions[k]) / span : 0;
  return mix_color(stops[k], stops[k + 1], amount);
}
uint32_t art_bg(const Art *a) {
  return bg_row(a, a->rows / 2);
}
void art_background(Art *a, unsigned char *pixels, int stride, int scale) {
  int width = a->width * scale, height = a->height * scale;
  for (int row = 0; row < a->rows; row++) {
    uint32_t color = 0xff000000u | bg_row(a, row);
    int top = (int)floor(row * a->ch) * scale;
    int bottom = (int)floor((row + 1) * a->ch) * scale;
    if (bottom > height)
      bottom = height;
    for (int y = top; y < bottom; y++) {
      uint32_t *line = (uint32_t *)(pixels + (size_t)y * stride);
      for (int x = 0; x < width; x++)
        line[x] = color;
    }
  }
}

static void put_cell(Art *a, int x, int y, char c, uint32_t fg, uint32_t bg) {
  if (x < 0 || x >= a->cols || y < 0 || y >= a->rows)
    return;
  Cell cell = {bg & 0xffffff, fg & 0xffffff, (uint8_t)c};
  a->cells[y * a->cols + x] = cell;
}
// A glyph in the ink colour. It keeps any ground already painted in that
// cell, so texture written over a dune, wall or pond surface reads as texture
// rather than as a hole through to the sky gradient.
static void put(Art *a, double x, double y, char c, uint32_t fg) {
  int ix = (int)round(x), iy = (int)round(y);
  if (ix < 0 || ix >= a->cols || iy < 0 || iy >= a->rows)
    return;
  Cell *cell = &a->cells[iy * a->cols + ix];
  cell->fg = fg & 0xffffff;
  cell->c = (uint8_t)c;
}
// An opaque ground block; no glyph is drawn on it.
static void block(Art *a, double x, double y, uint32_t bg) {
  put_cell(a, (int)round(x), (int)round(y), 0, 0, bg);
}
// A glyph on its own coloured tile.
static void glyph_tile(Art *a, double x, double y, char c, uint32_t fg, uint32_t bg) {
  put_cell(a, (int)round(x), (int)round(y), c, fg, bg);
}
static void fill_rect(Art *a, int x0, int y0, int x1, int y1, uint32_t bg) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      block(a, x, y, bg);
}

typedef struct {
  char key;
  char glyph;
  uint32_t fg, bg;
} SpriteKey;
// Stamps hand-placed pixel art. A space is transparent; a glyph of 0 paints a
// solid block in bg; otherwise fg is drawn on bg (bg 0 keeps the gradient).
static void stamp(Art *a, const char *const *rows, int height,
                  const SpriteKey *keys, int key_count, int x, int y,
                  int flip) {
  for (int r = 0; r < height; r++) {
    int width = (int)strlen(rows[r]);
    for (int c = 0; c < width; c++) {
      char key = rows[r][flip ? width - 1 - c : c];
      if (key == ' ')
        continue;
      for (int k = 0; k < key_count; k++)
        if (keys[k].key == key) {
          put_cell(a, x + c, y + r, keys[k].glyph, keys[k].fg, keys[k].bg);
          break;
        }
    }
  }
}

// Forest keeps the material remap that separates bamboo, foliage and flowers;
// every other scene paints final palette colours directly.
static uint32_t ink_color(const Art *a, uint32_t color) {
  const ScenePalette *p = &scene_palettes[a->scene];
  if (a->scene != 3)
    return color;
  switch (color) {
  case 0x1b352c: return p->distant;
  case 0x2a4a39: return p->middle;
  case 0x41634a: return p->foreground;
  case 0x304c33: return p->foliage;
  case 0x2e473a: return p->texture;
  case 0xc8c77f: return p->accent;
  case 0x728451: return p->shimmer;
  default: return p->highlight;
  }
}

static void moon(Art *a) {
  const uint32_t ink = 0xd1d5b4;
  double r = fmin(a->cols * .035, a->rows * .085);
  for (double y = -r; y <= r; y++)
    for (double x = -r * 1.8; x <= r * 1.8; x++) {
      double d = pow(x / 1.65, 2) + y * y,
             cut = pow(x / 1.65 - r * .4, 2) + pow(y + r * .19, 2);
      if (d < r * r && cut > r * r * .86)
        put(a, a->cols * .76 + x, a->rows * .19 + y,
            hash(x, y) > .6 ? 'o' : ':', ink_color(a, ink));
    }
}
static void mountain_base(Art *a) {
  const ScenePalette *p = &scene_palettes[2];
  double water = a->rows * .70;
  const uint32_t shades[] = {p->distant, p->middle, p->foreground,
                             p->foliage};
  for (int x = 0; x < a->cols; x++) {
    double ridges[4];
    for (int l = 0; l < 4; l++) {
      double n = (double)x / a->cols,
             q = fabs(sin(n * (7 + l * 1.8) + l * 1.9)) * .14 +
                 fabs(sin(n * (18 + l * 2) + l)) * .055;
      ridges[l] = a->rows * (.57 + l * .082 - q * (1 - l * .13));
    }
    for (int y = 0; y < water; y++) {
      int hit = -1;
      double top = 0;
      for (int l = 0; l < 4; l++)
        if (y > ridges[l]) {
          hit = l;
          top = ridges[l];
        }
      if (hit >= 0) {
        double n = hash(x, y);
        int snow = hit == 0 && y - top < 3 + hash(x, 1) * 2;
        put(a, x, y,
            snow      ? '/'
            : n > .8  ? '/'
            : n > .45 ? ':'
                      : '.',
            snow ? p->highlight : shades[hit]);
      }
    }
  }
  for (int i = 0; i < 30; i++) {
    double x = a->cols * (hash(i, 25) > .5 ? .04 + hash(i, 21) * .14
                                           : .83 + hash(i, 21) * .15),
           base = a->rows * (.82 + hash(i, 22) * .15),
           height = 5 + hash(i, 23) * 13;
    for (int yy = 0; yy < height; yy++) {
      double width = yy / height * height * .34;
      for (double xx = -width; xx <= width; xx++)
        put(a, x + xx, base - height + yy,
            xx == 0  ? '|'
            : xx < 0 ? '/'
                     : '\\',
            p->foliage);
    }
  }
}
static void forest_base(Art *a) {
  moon(a);
  for (int layer = 0; layer < 3; layer++)
    for (int i = 0; i < 18; i++) {
      double x = hash(i + layer * 41, 18) * a->cols,
             base = a->rows * (.64 + layer * .13),
             height = a->rows * (.18 + hash(i, 19) * .25);
      uint32_t color = layer == 0 ? 0x1b352c : layer == 1 ? 0x2a4a39 : 0x41634a;
      for (int y = 0; y < height; y++) {
        double width = (y / height) * height * .32;
        for (double dx = -width; dx <= width; dx++)
          if (fabs(dx) < 1 || hash(i + y, dx) > .12)
            put(a, x + dx, base - height + y,
                fabs(dx) < 1 ? '|'
                : dx < 0     ? '/'
                             : '\\',
                ink_color(a, color));
      }
    }
  for (int x = 0; x < a->cols; x++)
    for (int y = (int)(a->rows * .9); y < a->rows; y++)
      if (hash(x, y) > .65)
        put(a, x, y, hash(x, y) > .85 ? 'v' : '.',
            ink_color(a, 0x304c33));
}

// ---------------------------------------------------------------------------
// Pond: a night garden. Moon glitter, lily pads, koi and a lit stone lantern.
// ---------------------------------------------------------------------------

static void pond_stars(Art *a, double t) {
  const PondPalette *p = &pond_colors;
  int count = (int)(a->cols * a->rows * .012);
  if (count < 40)
    count = 40;
  for (int i = 0; i < count; i++) {
    double x = hash(i, 31) * a->cols, y = hash(i, 32) * a->rows * .40;
    double twinkle = .5 + .5 * sin(t * (.35 + hash(i, 33) * .8) + i);
    double d = hypot(x - a->cols * .72, (y - a->rows * .155) * 1.8);
    if (d < fmin(a->cols * .11, a->rows * .22))
      continue;
    char c = hash(i, 34) > .93 ? '+' : hash(i, 35) > .72 ? '*' : '.';
    uint32_t color = twinkle > .82 ? (hash(i, 36) > .7 ? p->star_warm : p->star)
                     : twinkle > .5 ? p->star
                                    : p->star_dim;
    put(a, x, y, c, color);
  }
}
static void pond_moon(Art *a) {
  const PondPalette *p = &pond_colors;
  double r = fmin(a->cols * .032, a->rows * .085);
  double cx = a->cols * .72, cy = a->rows * .155;
  for (double y = -r * 1.9; y <= r * 1.9; y++)
    for (double x = -r * 2.6; x <= r * 2.6; x++) {
      double d = hypot(x / 1.65, y) / r;
      if (d > 1 && d < 1.95 && hash(x * 2.1, y * 2.1) < (1.95 - d) * .22)
        put(a, cx + x, cy + y, '.', p->moon_halo);
      if (d > 1)
        continue;
      // A few soft maria, kept on the left of the disc.
      double spot = hypot(x / 1.65 + r * .25, y + r * .16) / r;
      double spot2 = hypot(x / 1.65 - r * .18, y - r * .26) / r;
      int crater = (spot < .34 || spot2 < .20) && hash(x, y) > .35;
      if (crater)
        glyph_tile(a, cx + x, cy + y, hash(x, y) > .55 ? 'o' : ':',
                   p->moon_shade, p->moon);
      else
        block(a, cx + x, cy + y, p->moon);
    }
}
static void pond_pad(Art *a, double x, double y, double r, int seed, int blossom) {
  const PondPalette *p = &pond_colors;
  double ry = r * .40;
  for (double yy = -ry; yy <= ry; yy++)
    for (double xx = -r; xx <= r; xx++) {
      double d = (xx * xx) / (r * r) + (yy * yy) / (ry * ry);
      if (d > 1 || (xx > 0 && fabs(yy) < xx * .16))
        continue;
      uint32_t color;
      if (d > .86)
        color = yy < 0 ? p->pad_rim : p->pad_deep;
      else if (yy > ry * .45)
        color = p->pad_deep;
      else
        color = hash(xx + seed, yy) > .55 ? p->pad : mix_color(p->pad, p->pad_deep, .3);
      block(a, x + xx, y + yy, color);
    }
  for (double k = -r * .8; k <= r * .8; k += 1.35)
    put(a, x + k, y + sin(k * .8 + seed) * ry * .55, '.', p->vein);
  if (!blossom)
    return;
  static const char *const flower[] = {
      "..p.p..", ".ppPpp.", "ppPcPpp", ".ppPpp.", "..p.p.."};
  const SpriteKey keys[] = {{'p', 0, 0, p->petal[1]},
                            {'P', 0, 0, p->petal[0]},
                            {'c', 0, 0, p->stamen}};
  stamp(a, flower, 5, keys, 3, (int)round(x) - 3, (int)round(y) - 2, 0);
}
static void pond_koi(Art *a, double cx, double cy, int dir, double t, int id,
                     const uint32_t colors[4]) {
  int left = (int)round(cx) - 7, top = (int)round(cy) - 2;
  // Tail: a rooted fan that forks as it trails away, swaying with the swim.
  int root = (int)round(cx) - dir * 4;
  for (int k = 0; k <= 5; k++) {
    double u = k / 5.;
    double lobe = 1.7 * (.45 + .9 * u);
    double sway = sin(t * 1.8 + id * 2.1) * 1.2 * u * u;
    for (int v = -4; v <= 4; v++) {
      double off = v - sway;
      if (fabs(off) > lobe)
        continue;
      if (u > .42 && fabs(off) < lobe * .28)
        continue;
      block(a, root - dir * k, cy + v, colors[2]);
      if (fabs(fabs(off) - lobe) < .5)
        glyph_tile(a, root - dir * k, cy + v, ':', colors[3], colors[2]);
    }
  }
  static const char *const body[] = {
      ".....BBBBBB...", "...BBBBBBBBBB.", "..BBBBBBPPBBBB",
      "...BBBBBBBBBB.", ".....BBBBBB..."};
  const SpriteKey keys[] = {{'B', 0, 0, colors[0]},
                            {'P', 0, 0, colors[1]},
                            {'e', 0, 0, 0x0d0d0a}};
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 14; c++) {
      char key = body[r][c];
      if (key == '.')
        continue;
      if (r == 2 && c == 11)
        key = 'e';
      int x = left + (dir > 0 ? c : 13 - c);
      for (int q = 0; q < 3; q++)
        if (keys[q].key == key)
          put_cell(a, x, top + r, keys[q].glyph, keys[q].fg, keys[q].bg);
    }
  // Pectoral fin and a pale belly.
  int shoulder = left + (dir > 0 ? 10 : 3);
  block(a, shoulder, top + 5, colors[2]);
  block(a, shoulder + dir, top + 5, colors[2]);
  for (int c = 4; c < 10; c++)
    if (hash(c, id * 3) > .5)
      put(a, left + (dir > 0 ? c : 13 - c), top + 4, '.', colors[3]);
  for (int k = 0; k < 3; k++)
    put(a, root - dir * (7 + k * 1.2), cy + sin(k * .9 + id) * .9, '.',
        pond_colors.ripple);
}
static void pond_lantern(Art *a) {
  const PondPalette *p = &pond_colors;
  int x = (int)(a->cols * .85), y = (int)(a->rows * .46) - 12;
  static const char *const rows[] = {
      "...sss...", "..sssss..", ".sssssss.", "sssssssss",
      ".sssssss.", "..sllls..", "..slgls..", "..sllls..",
      "...sss...", "...sss...", "..sssss..", ".sssssss.",
      "..sssss.."};
  const SpriteKey keys[] = {
      {'s', 0, 0, p->lantern_stone},
      {'l', 0, 0, p->lantern_light},
      {'g', 0, 0, p->lantern_warm},
  };
  stamp(a, rows, 13, keys, 3, x, y, 0);
  for (int yy = -6; yy <= 8; yy++)
    for (int xx = -7; xx <= 7; xx++) {
      double d = hypot(xx * .8, yy);
      if (d > 3.5 && d < 7 && hash(x + xx, y + yy) > .52 - d * .035)
        put(a, x + 4 + xx, y + 6 + yy, '.', p->lantern_glow);
    }
  // Broken warm reflection in the water below.
  for (int yy = 1; yy < 14; yy++)
    for (int xx = -5; xx <= 5; xx++)
      if (hash(xx + 30, yy + 9) > .46 - yy * .012)
        put(a, x + 4 + xx * .7 + sin(yy * .7) * 1.4, a->rows * .46 + yy,
            yy % 3 ? '~' : '.', mix_color(p->lantern_warm, p->water_dim,
                                          yy / 14.));
}
static void pond_base(Art *a) {
  const PondPalette *p = &pond_colors;
  double water = a->rows * .46;
  for (int x = 0; x < a->cols; x++) {
    double u = (x + .5) / a->cols;
    double far = water - a->rows * (.075 + .05 * noise2(u * 4.2, 8.1) +
                                    .018 * sin(u * 17));
    for (int y = (int)ceil(far); y < water; y++)
      block(a, x, y, p->far_ridge);
    if (hash(x, 2) > .55)
      put(a, x, far, '.', p->rim);
  }
  for (int x = 0; x < a->cols; x++) {
    double u = (x + .5) / a->cols;
    double top = water - a->rows * (.022 + .055 * (0.5 + 0.5 * sin(u * 8.2 + 1.1)) +
                                    .028 * noise2(u * 6.5, 3.4));
    for (int y = (int)ceil(top); y < water - 1; y++)
      block(a, x, y, p->tree_ridge);
    if (hash(x, 5) > .45)
      put(a, x, top, '.', p->rim);
  }
  for (int x = 0; x < a->cols; x++)
    if (hash(x, 6) > .28)
      put(a, x, water - 1 + hash(x, 7) * .8,
          hash(x, 8) > .7 ? 'v' : '.', hash(x, 9) > .7 ? p->grass : p->moss);
  // Reeds growing out of the bank.
  for (int i = 0; i < 26; i++) {
    int left = i < 16;
    double x = left ? hash(i, 11) * a->cols * .17
                    : a->cols * (.86 + hash(i, 11) * .13);
    double base = water - 1, height = a->rows * (.05 + hash(i, 12) * .16);
    for (int k = 0; k < height; k++) {
      double bend = sin(k * .18 + i + 1.2) * (k / height) * 1.6;
      put(a, x + bend, base - k, k > height * .7 && hash(i, k) > .6 ? ':' : '|',
          p->reed);
      if (k % 4 == 1 && hash(i, k + 3) > .45)
        put(a, x + bend - 1, base - k, '/', p->reed);
      if (k % 5 == 2 && hash(i, k + 5) > .55)
        put(a, x + bend + 1, base - k, '\\', p->reed);
    }
  }
  // Pines along the far bank, silhouetted by the moon.
  static const struct {
    double u, s;
  } pines[] = {{.06, 1}, {.13, .8}, {.21, 1.15}, {.30, .7},
               {.55, .9}, {.64, .7}, {.93, 1.1}};
  for (int i = 0; i < 7; i++) {
    int sx = (int)(a->cols * pines[i].u), sy = (int)(water - 1);
    static const char *const pine[] = {
        "...p...", "..ppp..", ".ppppp.", "..ppp..",
        ".ppppp.", "ppppppp", "...t...", "...t..."};
    const SpriteKey keys[] = {{'p', 0, 0, p->tree_ridge},
                              {'t', '|', p->reed, 0}};
    stamp(a, pine, 8, keys, 2, sx, sy - 7, 0);
    if (pines[i].s > 1)
      stamp(a, pine, 8, keys, 2, sx + 6, sy - 6, 0);
  }
  // Tall reeds rising out of the near bank frame the pond.
  for (int i = 0; i < 18; i++) {
    int left = i < 11;
    double x = left ? 1 + hash(i, 201) * a->cols * .10
                    : a->cols * (.905 + hash(i, 201) * .09);
    double height = a->rows * (.30 + hash(i, 202) * .36);
    double bend = (hash(i, 203) - .5) * .5;
    for (int k = 0; k < height; k++) {
      double px = x + bend * pow(k / height, 2) * 12 +
                  sin(k * .12 + i) * .6;
      put(a, px, a->rows - 1 - k,
          k > height * .82 && hash(i, k) > .5 ? ':' : '|',
          k > height * .55 ? p->grass : p->reed);
      if (k % 5 == 2 && hash(i, k) > .35)
        put(a, px - 2, a->rows - 1 - k, '/',
            k > height * .55 ? p->grass : p->reed);
      if (k % 6 == 4 && hash(i, k + 1) > .4)
        put(a, px + 2, a->rows - 1 - k, '\\',
            k > height * .55 ? p->grass : p->reed);
    }
    if (hash(i, 204) > .55) {
      put(a, x + bend * 12, a->rows - 1 - height, ':', p->grass);
      put(a, x + bend * 12, a->rows - 2 - height, ':', p->grass);
    }
  }
  pond_moon(a);
  pond_lantern(a);
}
static void pond_frame(Art *a, double t) {
  const PondPalette *p = &pond_colors;
  double water = a->rows * .46;
  pond_stars(a, t);
  // Moon glitter: a broken, slowly shifting road on the water.
  for (int y = (int)water; y < a->rows; y++) {
    double depth = (y - water) / fmax(1, a->rows - water);
    double width = a->cols * (.008 + depth * .045);
    double xc = a->cols * .72 + sin(y * .55 + t * .8) * (1 + depth * 4);
    for (int x = (int)(xc - width); x <= (int)(xc + width); x++)
      if (hash(x, y) > .58) {
        double light = (.85 - depth * .45) * (.65 + .35 * sin(t * 1.7 + x * .7 + y));
        put(a, x, y, light > .55 ? '~' : '-',
            mix_color(p->water_dim, p->water_glint, clamp(light, 0, 1)));
      }
  }
  for (int y = (int)water; y < a->rows; y++) {
    double depth = (y - water) / fmax(1, a->rows - water);
    double width = a->cols * (.010 + depth * .030);
    for (int x = (int)(a->cols * .72 - width); x <= a->cols * .72 + width; x++)
      if (hash(x, y) > .72 - depth * .1) {
        double glow = .45 + .55 * sin(t * 1.4 + x * .8 + y);
        put(a, x, y, glow > .65 ? '~' : '-',
            mix_color(p->water_dim, p->water_glint,
                      clamp((.35 + .55 * glow) * (1 - depth * .45), 0, 1)));
      }
  }
  for (int i = 0; i < 90; i++) {
    double x = fmod(hash(i, 21) * a->cols + t * (1.5 + hash(i, 22) * 2.5),
                    a->cols);
    double y = water + hash(i, 23) * (a->rows - water);
    put(a, x, y, hash(i, 24) > .55 ? '~' : '-', p->ripple);
  }
  // A low swell across the open water.
  for (int i = 0; i < 110; i++) {
    double x = fmod(hash(i, 61) * a->cols + t * (.8 + hash(i, 62)), a->cols);
    double y = water + 2 + hash(i, 63) * (a->rows - water - 2);
    put(a, x, y, hash(i, 64) > .6 ? '~' : '-',
        mix_color(p->water_dim, p->ripple, .55 + .45 * hash(i, 65)));
  }
  // Dark tree reflections hanging just under the far bank.
  for (int i = 0; i < 70; i++) {
    double x = hash(i, 66) * a->cols;
    double depth = 1 + hash(i, 67) * 5;
    if (hash(i, 68) > .35)
      put(a, x + sin(t * .4 + i) * .4, water + depth, '|',
          mix_color(p->tree_ridge, bg_row(a, (int)(water + depth)), .55));
  }
  // Ripple rings from the pads and koi.
  for (int i = 0; i < 5; i++) {
    double r = fmod(t * 1.1 + i * 3.7, 16);
    for (double v = 0; v < TAU; v += .16)
      if (sin(v * 7 + i) > .35)
        put(a, a->cols * .13 + cos(v) * (8 + r), a->rows * .56 + sin(v) * (3 + r * .2),
            fabs(cos(v)) > .7 ? '|' : '~',
            mix_color(p->water_dim, p->ripple, (1 - r / 16) * .8));
  }
  // A soft mist band along the far waterline.
  for (int i = 0; i < a->cols * .8; i++) {
    double x = fmod(hash(i, 26) * a->cols + t * .18, a->cols);
    put(a, x, water + hash(i, 27) * a->rows * .06, '.', p->mist);
  }
  static const struct {
    double u, v, r;
    int blossom;
  } pads[] = {{.13, .56, 8.0, 1}, {.24, .65, 4.6, 0}, {.78, .58, 8.6, 1},
              {.89, .70, 5.2, 0}, {.55, .88, 7.0, 0}, {.32, .94, 4.6, 0},
              {.70, .95, 5.6, 0}};
  for (int i = 0; i < 7; i++) {
    double x = pads[i].u * a->cols + sin(t * .12 + i) * .7,
           y = pads[i].v * a->rows + cos(t * .15 + i * 1.3) * .4;
    pond_pad(a, x, y, pads[i].r, i * 13, pads[i].blossom);
  }
  static const struct {
    double u, v, ru, rv, period, phase, len, width;
    int id, dir;
  } lanes[] = {{.48, .57, .19, .040, 26, 0.0, 10.5, 1.55, 0, 1},
               {.58, .80, .14, .040, 34, 2.1, 9.5, 1.45, 1, -1},
               {.29, .80, .16, .035, 41, 4.2, 12.0, 1.75, 2, 1}};
  for (int i = 0; i < 3; i++) {
    double angle = t * 2 * TAU / lanes[i].period + lanes[i].phase;
    double x = a->cols * (lanes[i].u + lanes[i].ru * sin(angle));
    double y = a->rows * (lanes[i].v + lanes[i].rv * cos(angle));
    double dir = cos(angle) >= 0 ? 1 : -1;
    pond_koi(a, x, y, (int)dir, t, lanes[i].id, p->koi[lanes[i].id]);
    double wake = fmod(t * 1.1 + i * 4.7, 11);
    for (int k = 0; k < 7; k++) {
      double r = wake + k * 1.4;
      put(a, x - dir * lanes[i].len * 1.9 - r, y + sin(k * .8 + t) * .7, '.',
          mix_color(p->water_dim, p->ripple, (1 - r / 21) * .8));
    }
  }
  // A few pale petals drifting near the lantern.
  for (int i = 0; i < 7; i++) {
    double x = fmod(a->cols * .72 + hash(i, 41) * a->cols * .22 + t * .35,
                    a->cols);
    double y = water + hash(i, 42) * (a->rows - water) * .8;
    put(a, x, y + sin(t * .5 + i) * .6, i % 2 ? '.' : ',', p->petal[0]);
  }
  // Lantern light breathes just enough to read as a flame.
  int x = (int)(a->cols * .845) + 4, y = (int)(a->rows * .46) - 6;
  double flame = .5 + .5 * sin(t * 1.7);
  if (flame > .3)
    glyph_tile(a, x, y, flame > .8 ? '*' : '.', p->lantern_glow,
               p->lantern_light);
}

// ---------------------------------------------------------------------------
// Sky: night over open water. Milky way, moon, island lights and a sailboat.
// ---------------------------------------------------------------------------

static void sky_moon(Art *a) {
  const SkyPalette *p = &sky_colors;
  double r = fmin(a->cols * .033, a->rows * .086);
  double cx = a->cols * .77, cy = a->rows * .150;
  for (double y = -r * 1.9; y <= r * 1.9; y++)
    for (double x = -r * 2.6; x <= r * 2.6; x++) {
      double d = hypot(x / 1.65, y) / r;
      if (d > 1 && d < 1.95 && hash(x * 2.3, y * 2.3) < (1.95 - d) * .20)
        put(a, cx + x, cy + y, '.', p->moon_halo);
      if (d > 1)
        continue;
      double spot = hypot(x / 1.65 + r * .28, y + r * .18) / r;
      double spot2 = hypot(x / 1.65 - r * .20, y - r * .30) / r;
      int crater = (spot < .30 || spot2 < .16) && hash(x, y) > .30;
      if (crater)
        glyph_tile(a, cx + x, cy + y, hash(x, y) > .55 ? 'o' : ':',
                   p->moon_shade, p->moon);
      else
        block(a, cx + x, cy + y, p->moon);
    }
}
static void sky_star(Art *a, double x, double y, double light, int seed) {
  const SkyPalette *p = &sky_colors;
  char c = light > .9 ? (hash(seed, 3) > .55 ? '+' : '*')
          : light > .55 ? '.'
                        : hash(seed, 4) > .7 ? '.' : 0;
  uint32_t color = light > .88 ? (hash(seed, 5) > .55 ? p->star_warm : p->star_bright)
                   : light > .58 ? p->star
                                 : p->star_dim;
  if (c)
    put(a, x, y, c, color);
}
static void sky_base(Art *a) {
  const SkyPalette *p = &sky_colors;
  double sea = a->rows * .70;
  // Milky way: a wide, dusty diagonal with a denser bright core.
  for (int y = 0; y < (int)(sea * .97); y++)
    for (int x = 0; x < a->cols; x++) {
      double line = a->rows * .02 + (x / (double)a->cols) * a->rows * .34;
      double d = fabs(y - line) / (a->rows * .13);
      if (d > 2.4)
        continue;
      double density = exp(-d * d) * .42;
      if (hash(x * 1.7, y * 1.7) < density)
        put(a, x, y, hash(x, y) > .86 ? ':' : '.',
            hash(x, y) > .62 ? p->dust : p->dust_dim);
      double core = exp(-pow((x / (double)a->cols - .46) / .20, 2)) * .16;
      if (hash(x * 2.1, y * 2.1) < core * exp(-d * d))
        put(a, x, y, '+', p->star_dim);
    }
  // Two constellations, joined by faint dots.
  static const int dipper[][2] = {{9, 10}, {15, 7}, {21, 9}, {26, 14},
                                  {33, 13}, {38, 18}, {45, 17}};
  for (int i = 0; i < 7; i++) {
    int x = a->cols * dipper[i][0] / 100, y = a->rows * dipper[i][1] / 100;
    put(a, x, y, '+', p->star_bright);
    for (int k = 1; k < 5; k++) {
      if (i == 6)
        break;
      int nx = a->cols * dipper[i + 1][0] / 100,
          ny = a->rows * dipper[i + 1][1] / 100;
      put(a, x + (nx - x) * k / 5., y + (ny - y) * k / 5., '.',
          p->constellation);
    }
  }
  static const int triangle[][2] = {{61, 23}, {67, 30}, {58, 31}};
  for (int i = 0; i < 3; i++) {
    int x = a->cols * triangle[i][0] / 100, y = a->rows * triangle[i][1] / 100;
    put(a, x, y, '+', p->star);
    int nx = a->cols * triangle[(i + 1) % 3][0] / 100,
        ny = a->rows * triangle[(i + 1) % 3][1] / 100;
    for (int k = 1; k < 4; k++)
      put(a, x + (nx - x) * k / 4., y + (ny - y) * k / 4., '.',
          p->constellation);
  }
  sky_moon(a);
  for (int x = 0; x < a->cols; x++)
    block(a, x, sea, p->sea_rim);
  // Foreground headland; dark rock, a lit edge, and three pines.
  double edge = a->cols * .30;
  int tops[512];
  for (int x = 0; x < (int)edge; x++) {
    double u = x / edge;
    double top = a->rows * (.700 + .30 * pow(u, 1.35)) +
                 noise2(x * .11, 4.3) * 2.6;
    tops[x < 512 ? x : 511] = (int)top;
    for (int y = (int)top; y < a->rows; y++) {
      uint32_t color = ramp_color((const uint32_t[]){p->cliff, p->cliff_lit},
                                  2, .30 + .35 * noise2(x * .18, y * .16), x, y);
      if (y == (int)top)
        color = p->cliff_lit;
      block(a, x, y, color);
    }
    if (hash(x, 13) > .72)
      put(a, x, top + 1, '.', p->moon_halo);
  }
  static const struct {
    double u;
  } pines[] = {{.035}, {.10}, {.165}};
  for (int i = 0; i < 3; i++) {
    int x = (int)(a->cols * pines[i].u);
    int y = tops[x < 512 ? x : 511] - 7;
    static const char *const pine[] = {
        "...p...", "..ppp..", ".ppppp.", "..ppp..",
        ".ppppp.", "ppppppp", "...t...", "...t..."};
    const SpriteKey keys[] = {{'p', 0, 0, p->pine},
                              {'t', '|', p->pine, 0}};
    stamp(a, pine, 8, keys, 2, x, y, 0);
  }
  // Foam where the headland meets the sea.
  for (int i = 0; i < 22; i++)
    put(a, hash(i, 21) * edge, a->rows * (.86 + hash(i, 22) * .12),
        hash(i, 23) > .5 ? '~' : '.', p->moon_halo);
}
static void sky_frame(Art *a, double t) {
  const SkyPalette *p = &sky_colors;
  double sea = a->rows * .70;
  int count = (int)(a->cols * a->rows * .020);
  if (count < 90)
    count = 90;
  for (int i = 0; i < count; i++) {
    double x = hash(i, 51) * a->cols, y = hash(i, 52) * sea * .96;
    double d = hypot(x - a->cols * .77, (y - a->rows * .15) * 1.7);
    if (d < fmin(a->cols * .11, a->rows * .22) ||
        (x < a->cols * .32 && y > a->rows * .66))
      continue;
    double light = .5 + .5 * sin(t * (.24 + hash(i, 53) * .8) + i * 1.7);
    sky_star(a, x, y, light, i);
  }
  // Thin clouds crossing the moon's glow.
  for (int layer = 0; layer < 2; layer++) {
    double y = a->rows * (.26 + layer * .10) + sin(t * .05 + layer) * .6;
    double x0 = fmod(t * (.30 + layer * .18), a->cols);
    for (int i = 0; i < a->cols * .55; i++) {
      double x = fmod(x0 + hash(i, 61 + layer) * a->cols * .75, a->cols);
      if (hash(i, 63 + layer) > .62)
        put(a, x, y + sin(x * .07 + layer) * 1.6 + hash(i, 64) * 1.2,
            hash(i, 65) > .78 ? '~' : '.',
            hash(i, 66) > .55 ? p->cloud_lit : p->cloud);
    }
  }
  double cycle = fmod(t + 5, 15);
  if (cycle < 1.2) {
    double sx = a->cols * (.10 + cycle * .11),
           sy = a->rows * (.08 + cycle * .05);
    for (int k = 0; k < 18; k++) {
      double f = k / 18.;
      put(a, sx + k, sy + k * .30,
          k == 0 ? '*' : k < 4 ? '+' : '.',
          k == 0 ? p->meteor : mix_color(p->meteor, p->meteor_tail, f));
    }
  }
  // Sea: long slow swells and the moon's road.
  for (int y = (int)sea + 1; y < a->rows; y++) {
    double depth = (y - sea) / fmax(1, a->rows - sea);
    for (int x = 0; x < a->cols; x++) {
      double shift = sin(y * .45 + t * .5) * 2;
      if (hash(x + shift, y) > .70)
        put(a, x, y, hash(x, y) > .55 ? '~' : '-',
            mix_color(p->wave, p->wave_lit,
                      clamp(.35 + .55 * sin(x * .18 + t * .4 + y), 0, 1)));
    }
    double width = a->cols * (.012 + depth * .045);
    double xc = a->cols * .77 + sin(y * .6 + t * .9) * (1 + depth * 3);
    for (int x = (int)(xc - width); x <= (int)(xc + width); x++)
      if (hash(x, y) > .58)
        put(a, x, y, hash(x, y) > .78 ? '~' : '.',
            mix_color(p->wave, p->glint, clamp(1 - depth * .5, 0, 1)));
  }
  // Sailboat, riding the swell with a lantern at the mast.
  int bx = (int)(a->cols * .60),
      by = (int)(a->rows * .745 + sin(t * .55) * .9);
  static const char *const boat[] = {
      "......l......", ".....sss.....", "....sssss....", "...sssssss...",
      "..sssssssss..", ".sssssssssss.", "......m......", "..hhhhhhhhh..",
      "...hhhhhhh..."};
  const SpriteKey keys[] = {{'m', '|', p->sail, 0},
                            {'s', 0, 0, p->sail},
                            {'h', 0, 0, p->boat},
                            {'l', 0, 0, p->boat_light}};
  stamp(a, boat, 9, keys, 4, bx, by, 0);
  for (int k = 1; k < 6; k++)
    put(a, bx + 6 + k, by + k, '.', p->moon_halo);
  for (int k = 1; k < 9; k++)
    glyph_tile(a, bx + k, by + 7, '.', p->moon_halo, p->boat);
  double flicker = .5 + .5 * sin(t * 2.3);
  put(a, bx + 6, by + 0, flicker > .35 ? '*' : '.', p->boat_light);
  for (int k = 1; k < 8; k++)
    if (hash(k, 71) > .3)
      put(a, bx + 5 + sin(k + t) * .5, by + 9 + k, '.',
          mix_color(p->boat_reflection, p->wave, k / 8.));
}

// ---------------------------------------------------------------------------
// Rain: a daylight garden. Pale mist, bamboo, plaster wall, hydrangeas,
// stepping stones, a stone basin and a slow rain.
// ---------------------------------------------------------------------------

static void rain_bamboo(Art *a, double x, double base, double top,
                        uint32_t stalk, uint32_t leaf, uint32_t node,
                        int seed, int thick) {
  int x0 = (int)round(x);
  for (int y = (int)top; y <= base; y++) {
    block(a, x0, y, stalk);
    if (thick > 1)
      block(a, x0 + 1, y, mix_color(stalk, node, .22));
    if (y % 6 == seed % 6)
      for (int k = 0; k < thick; k++)
        glyph_tile(a, x0 + k, y, '=', node, mix_color(stalk, node, .18));
  }
  // Leaf fans on alternating sides, denser towards the crown.
  for (int k = 0; k < 8; k++) {
    double y = top + (base - top) * (.05 + k * .11);
    int side = (hash(seed, k) > .5) ? 1 : -1;
    int len = 2 + (int)(hash(seed, k + 3) * 3);
    for (int j = 1; j <= len; j++) {
      char c = side > 0 ? '/' : '\\';
      put(a, x0 + side * (thick + j), y - j * .80, c, leaf);
      put(a, x0 + side * (thick + j + 1), y - j * .80 - 1, c, leaf);
      if (thick > 1)
        put(a, x0 + side * (thick + j), y - j * .80 - 1, c, leaf);
      if (j > 2)
        put(a, x0 + side * (thick + j - 1), y - j * .80 + 1, '.', leaf);
    }
  }
  for (int j = -2; j <= 2; j++)
    put(a, x0 + j, top - abs(j) * .8, j == 0 ? 'v' : ',', leaf);
  if (thick > 1) {
    put(a, x0 + 1, top - 3, '/', leaf);
    put(a, x0 + 2, top - 4, '/', leaf);
  }
}
static void rain_bloom(Art *a, double x, double y, double r, int seed) {
  const RainPalette *p = &rain_colors;
  uint32_t base = p->bloom[(int)(hash(seed, 3) * 4)];
  for (int yy = -2; yy <= 2; yy++)
    for (int xx = -3; xx <= 3; xx++) {
      double d = hypot(xx / (r * 1.5), yy / (r * .95));
      if (d > 1)
        continue;
      uint32_t color = (xx + yy) & 1 ? base : mix_color(base, p->wall, .18);
      if (d > .78)
        color = mix_color(base, p->leaf_deep, .30);
      block(a, x + xx * .8, y + yy * .8, color);
    }
  put(a, x, y, '*', p->bloom_center);
  put(a, x + 1, y - 1, '.', p->bloom_center);
}
static void rain_bush(Art *a, double x, double y, double r, int seed) {
  const RainPalette *p = &rain_colors;
  // A solid leafy mound, then hydrangea heads sitting in it.
  for (double yy = -r * .75; yy <= r * .55; yy++)
    for (double xx = -r * 1.3; xx <= r * 1.3; xx++) {
      double d = (xx * xx) / (r * r * 1.69) + (yy * yy) / (r * r * .56);
      if (d > 1)
        continue;
      double n = noise2(xx * .5 + seed, yy * .5);
      block(a, x + xx, y + yy,
            d > .78 ? p->leaf_deep
                    : n > .5 ? mix_color(p->leaf_deep, p->moss, .55)
                             : mix_color(p->leaf_deep, p->moss, .15));
      if (n > .64)
        put(a, x + xx, y + yy, hash(xx, yy) > .5 ? ',' : 'v', p->moss_deep);
    }
  for (int i = 0; i < 9; i++) {
    double bx = x + (hash(seed, i + 20) - .5) * r * 2.1,
           by = y - r * .10 + (hash(seed, i + 60) - .5) * r * .8;
    rain_bloom(a, bx, by, r * .20, seed * 7 + i);
  }
}
static void rain_paving(Art *a) {
  const RainPalette *p = &rain_colors;
  int cols = a->cols, rows = a->rows;
  double top = rows * .575, bottom = rows;
  for (int y = (int)top; y < rows; y++) {
    double v = (y - top) / fmax(1, bottom - top);
    double half = cols * (.045 + .21 * pow(v, .8));
    double centre = cols * (.48 + .03 * v);
    for (int x = 0; x < cols; x++) {
      double u = (x - centre) / half;
      if (fabs(u) > 1.08)
        continue;
      // Stone courses shrink with distance.
      int course = (int)floor((y - top) / (2.2 + v * 2.6));
      double stone_w = 4.5 + v * 5.5;
      int stone = (int)floor((x - centre + half + (course % 2) * stone_w / 2) /
                             stone_w);
      int joint = ((int)floor((x - centre + half + (course % 2) * stone_w / 2)) %
                       (int)(stone_w)) == 0 ||
                  (y - top) - (int)(course * (2.2 + v * 2.6)) == 0;
      uint32_t color = (u < -.90 || u > .90 || joint)
                           ? p->path[2]
                           : hash(stone, course) > .5 ? p->path[0] : p->path[1];
      if (hash(x, y) > .94)
        color = mix_color(color, p->wet, .4);
      block(a, x, y, color);
      if (hash(x + 3, y) > .975)
        put(a, x, y, '.', p->wet);
    }
  }
  // Moss and gravel outside the path.
  for (int y = (int)top; y < rows; y++) {
    double v = (y - top) / fmax(1, bottom - top);
    double half = cols * (.055 + .21 * pow(v, .8));
    double centre = cols * (.48 + .03 * v);
    for (int x = 0; x < cols; x++) {
      if (fabs(x - centre) < half)
        continue;
      double moss = hash(x, y) > .42 ? 1 : 0;
      block(a, x, y, mix_color(p->path[1], p->moss,
                               .30 + .30 * moss + .2 * noise2(x * .1, y * .1)));
      if (hash(x, y) > .82)
        put(a, x, y, hash(x, y) > .92 ? ',' : '.', p->moss_deep);
    }
  }
  for (int x = 0; x < cols; x++) {
    int y = (int)(rows * .575);
    if (hash(x, 2) > .35)
      put(a, x, y, '.', p->moss);
  }
}
static void rain_gate(Art *a) {
  const RainPalette *p = &rain_colors;
  int cols = a->cols, rows = a->rows;
  double cx = cols * .50, cy = rows * .475,
         rx = fmax(3.2, cols * .072), ry = fmax(4.6, rows * .094);
  for (double y = -ry * 1.4; y <= ry * 1.4; y++)
    for (double x = -rx * 1.5; x <= rx * 1.5; x++) {
      double d = hypot(x / rx, y / ry);
      if (d > 1.02)
        continue;
      // A paler garden shows through the opening.
      block(a, cx + x, cy + y,
            mix_color(p->wall_shade, p->bg[1],
                      clamp(.30 + y / ry * .25, 0, 1)));
    }
  // Bamboo and stepping stones glimpsed through the gate.
  // A dark inner shadow under the arch.
  for (double y = -ry * .98; y < -ry * .55; y++)
    for (double x = -rx * .95; x <= rx * .95; x++)
      if (hypot(x / rx, y / ry) < .98)
        block(a, cx + x, cy + y, mix_color(p->leaf_deep, p->bg[0], .55));
  // Pale foliage and a path receding into the mist beyond the gate.
  for (int i = 0; i < 10; i++) {
    double lx = cx + (hash(i, 31) - .5) * rx * 1.4,
           ly = cy + ry * (.05 + hash(i, 32) * .45);
    if (abs(lx - cx) > rx * .35 && hash(i, 33) > .4)
      block(a, lx, ly, mix_color(p->far[2], p->bg[0], .20));
  }
  for (int s2 = 0; s2 < 2; s2++) {
    double sx = cx + (s2 ? rx * .72 : -rx * .72);
    for (double y = cy - ry * .7; y < cy + ry * .8; y++)
      put(a, sx, y, fmod(y, 5) < 1 ? '=' : '|',
          mix_color(p->bamboo[1], p->bg[0], .25));
    for (int k = 0; k < 3; k++)
      put(a, sx + (s2 ? 1 : -1), cy - ry * (.05 + k * .18), '/',
          mix_color(p->leaf[0], p->bg[0], .25));
  }
  for (double y = cy + ry * .18; y < cy + ry * .92; y++) {
    double w = (y - cy) / ry * rx * .30;
    for (double x = -w; x <= w; x++)
      block(a, cx + x, y, mix_color(p->path[0], p->bg[0], .45));
  }
  for (int i = 0; i < 3; i++)
    block(a, cx - rx * .15 + i * rx * .2, cy + ry * (.55 + i * .15),
          mix_color(p->stone[1], p->bg[0], .25));
  block(a, cx - rx * .35, cy + ry * .62, p->stone[1]);
  block(a, cx + rx * .25, cy + ry * .74, p->stone[2]);
  for (int k = 0; k < 6; k++)
    put(a, cx - rx * .5 + k, cy - ry * .15 + k * .7, '.', p->wet);
  // Ring of stone blocks around the opening, thicker at the shoulders.
  for (double v = 0; v < TAU; v += .028) {
    double thickness = 2.0 + .9 * pow(fabs(sin(v)), .6);
    for (double k = 0; k <= thickness; k += .7) {
      double q = 1.02 + k / rx;
      uint32_t color = ((int)(v * 9) + (int)(k * .8)) % 3
                           ? mix_color(p->stone[1], p->wall_shade, .35)
                           : mix_color(p->stone[2], p->wall_shade, .20);
      block(a, cx + cos(v) * rx * q, cy + sin(v) * ry * q, color);
    }
  }
  // Keystone, seated in the ring at the top of the arch.
  for (int k = -2; k <= 2; k++)
    block(a, cx + k, cy - ry * 1.16, k ? p->stone[1] : p->stone[0]);
  put(a, cx, cy - ry * 1.16, '.', p->stone[3]);
}
static void rain_base(Art *a) {
  const RainPalette *p = &rain_colors;
  int cols = a->cols, rows = a->rows;
  // Three misty bamboo layers, far to near.
  for (int layer = 0; layer < 3; layer++)
    for (int i = 0; i < 7 + layer * 3; i++) {
      double x = hash(i + layer * 37, 3) * cols,
             base = rows * (.58 - layer * .012),
             top = rows * (.08 + hash(i, 4) * .22);
      rain_bamboo(a, x, base, top, p->far[layer], p->leaf[layer > 1],
                  p->node, i + layer * 9, layer == 2 ? 2 : 1);
    }
  // Plaster wall with a tiled cap.
  int walltop = (int)(rows * .355), wallbase = (int)(rows * .565),
      wallleft = (int)(cols * .30);
  fill_rect(a, wallleft, walltop, cols - 1, wallbase, p->wall);
  for (int y = walltop + 2; y < wallbase; y++)
    for (int x = wallleft; x < cols; x++)
      if (hash(x, y) > .88)
        block(a, x, y, mix_color(p->wall, p->wall_shade,
                                 .25 + hash(y, x) * .45));
  fill_rect(a, wallleft, wallbase, cols - 1, wallbase + 1, p->moss_deep);
  for (int y = walltop + 3; y < wallbase; y += 5)
    for (int x = wallleft; x < cols; x++)
      if (hash(x, y) > .55)
        put(a, x, y, '.', p->wall_shade);
  // A plaster post terminates the wall on the left.
  fill_rect(a, wallleft - 1, walltop - 2, wallleft + 1, wallbase,
            p->stone[2]);
  for (int y = walltop - 1; y < wallbase; y += 4)
    put(a, wallleft, y, '=', p->stone[3]);
  for (int x = wallleft - 2; x < cols; x++) {
    block(a, x, walltop - 2, p->cap);
    block(a, x, walltop - 1, p->cap_shade);
    if (x % 4 == 0) {
      block(a, x, walltop - 2, mix_color(p->cap, p->cap_shade, .55));
      put(a, x, walltop - 1, '=', p->cap_shade);
    }
    if (x % 2 == 1)
      put(a, x, walltop - 1, '.', mix_color(p->cap, p->cap_shade, .35));
    if (hash(x, 9) > .78)
      put(a, x, wallbase - 1 - hash(x, 10) * 4, '.', p->moss);
  }
  if (wallleft > 1)
    for (int y = walltop - 2; y < wallbase; y++)
      block(a, wallleft - 2, y, p->cap_shade);
  rain_gate(a);
  // Ground and its wet stone path.
  rain_paving(a);
  // Puddles: darker water with pale sky glints.
  static const struct {
    double u, v, r;
  } puddles[] = {{.24, .74, .070}, {.71, .82, .080}, {.48, .94, .055}};
  for (int i = 0; i < 3; i++) {
    double cx = cols * puddles[i].u, cy = rows * puddles[i].v,
           rx = cols * puddles[i].r, ry = rx * .38;
    for (double yy = -ry; yy <= ry; yy++)
      for (double xx = -rx; xx <= rx; xx++) {
        double d = (xx * xx) / (rx * rx) + (yy * yy) / (ry * ry) +
                   .12 * noise2(xx * .25 + i, yy * .35);
        if (d > 1)
          continue;
        uint32_t color = d > .86 ? p->puddle[2]
                          : yy < -ry * .15 ? p->puddle[0]
                                             : p->puddle[1];
        block(a, cx + xx, cy + yy, color);
      }
    for (int k = 0; k < 5; k++)
      put(a, cx + (hash(i, k) - .5) * rx * 1.2,
          cy - ry * .35 + (hash(i, k + 9) - .5) * ry * .4,
          hash(i, k + 20) > .5 ? '-' : '.', p->splash);
  }
  // A few broad stepping stones where the moss creeps in.
  static const struct {
    double u, v, r;
  } stones[] = {{.13, .70, .045}, {.90, .78, .050}, {.86, .92, .060},
                {.18, .90, .055}};
  for (int i = 0; i < 4; i++) {
    double cx = cols * stones[i].u, cy = rows * stones[i].v,
           rx = cols * stones[i].r, ry = rx * .36;
    for (double yy = -ry; yy <= ry; yy++)
      for (double xx = -rx; xx <= rx; xx++) {
        double d = (xx * xx) / (rx * rx) + (yy * yy) / (ry * ry);
        if (d < 1)
          block(a, cx + xx, cy + yy,
                d > .72 ? p->stone[2]
                        : hash(xx, yy + i) > .6 ? p->stone[1] : p->stone[0]);
      }
    for (int k = -2; k <= 2; k++)
      if (hash(i, k) > .4)
        put(a, cx + k, cy - ry * .8, '.', p->stone[3]);
  }
  // Stone basin and its bamboo spout, in the left foreground.
  int bx = (int)(cols * .08), by = (int)(rows * .76);
  static const char *const basin[] = {
      "..sssssssss..", "..sddddddds..", ".sddddddddds.",
      "sddddddddddds", ".sdddddddds..", "..sssssss...."};
  const SpriteKey basin_keys[] = {{'s', 0, 0, p->stone[1]},
                                  {'d', 0, 0, p->stone[3]}};
  stamp(a, basin, 6, basin_keys, 2, bx, by, 0);
  for (int x = bx + 4; x <= bx + 8; x++) {
    block(a, x, by + 2, p->puddle[0]);
    block(a, x, by + 3, p->puddle[1]);
  }
  for (int y = by - 7; y <= by - 1; y++)
    block(a, bx + 11, y, p->bamboo[1]);
  for (int y = by - 7; y <= by - 1; y += 2)
    block(a, bx + 11, y, p->node);
  block(a, bx + 12, by - 7, p->bamboo[1]);
  put(a, bx + 12, by - 7, 'v', p->leaf[1]);
  // Hydrangeas crowding the wall foot.
  rain_bush(a, cols * .335, rows * .585, cols * .040, 10);
  rain_bush(a, cols * .53, rows * .585, cols * .050, 20);
  rain_bush(a, cols * .65, rows * .590, cols * .062, 30);
  rain_bush(a, cols * .83, rows * .585, cols * .055, 40);
  rain_bush(a, cols * .93, rows * .595, cols * .035, 50);
  // Foreground bamboo on the left edge.
  for (int i = 0; i < 2; i++) {
    double x = cols * (.025 + i * .045);
    rain_bamboo(a, x, rows - 1, rows * (.02 + i * .08), p->bamboo[1 + i],
                p->leaf[1], p->node, 70 + i, 2);
  }
  // Ferns and moss tufts where the paving meets the garden.
  for (int i = 0; i < 14; i++) {
    double x = hash(i, 31) < .5 ? cols * (.10 + hash(i, 32) * .22)
                                : cols * (.72 + hash(i, 32) * .24);
    double y = rows * (.63 + hash(i, 33) * .30);
    for (int k = 0; k < 4; k++)
      put(a, x + (k - 1.5) * .8, y - 1 - k * .8, '/', p->moss_deep);
    put(a, x, y - 3, '.', p->moss);
  }
  // Low mist hanging over the path.
  for (int i = 0; i < cols; i++)
    if (hash(i, 15) > .78)
      put(a, i, rows * (.585 + hash(i, 16) * .06), '.', p->rain_dim);
  // Two birds in the mist above the wall.
  put(a, cols * .62, rows * .16, 'v', p->bird);
  put(a, cols * .66, rows * .18, 'v', p->bird);
}
static void rain_frame(Art *a, double t) {
  const RainPalette *p = &rain_colors;
  int rows = a->rows, cols = a->cols;
  // Rain streaks; most are quiet grey, a few catch the light.
  int count = (int)(cols * 1.4);
  if (count < 90)
    count = 90;
  for (int i = 0; i < count; i++) {
    double speed = 8 + hash(i, 81) * 7;
    double fall = fmod(hash(i, 82) * rows + t * speed, rows * 1.08);
    double y = fall - rows * .04;
    double x = fmod(hash(i, 83) * cols - y * .16 + cols, cols);
    int bright = hash(i, 84) > .70;
    for (int k = 0; k < 3; k++)
      put(a, x + k * .18, y - k, '/', bright ? p->rain : p->rain_dim);
  }
  // Splashes on stone.
  for (int i = 0; i < 40; i++) {
    double age = fmod(t * 1.7 + hash(i, 91) * 7, 2.2);
    double x = hash(i, 92) * cols, y = rows * (.62 + hash(i, 93) * .36);
    if (age < .9)
      for (int k = -1; k <= 1; k++)
        put(a, x + k, y - (k == 0 ? age * 2 : -age), k ? '`' : ',',
            p->splash);
  }
  // Rings across the puddles.
  for (int i = 0; i < 8; i++) {
    double cx = cols * (.22 + (i % 3) * .23),
           cy = rows * (.72 + (i % 2) * .08);
    double radius = fmod(t * 2.0 + i * 2.3, 5);
    for (double v = 0; v < TAU; v += .18)
      if (sin(v * 6 + i) > .15)
        put(a, cx + cos(v) * radius, cy + sin(v) * radius * .38,
            fabs(cos(v)) > .7 ? '|' : '~', p->ripple);
  }
}
// ---------------------------------------------------------------------------
// Dunes: bright daylight. A high sun, layered ochre dunes, grasses and a slow
// camel caravan.
// ---------------------------------------------------------------------------

// Dunes are broad ellipsoid mounds, painted back to front. Their normals give
// the sunlight direction directly, so each crest gets a lit shoulder and a
// shaded slip face instead of horizontal bands.
typedef struct {
  double cx, cy, rx, ry;
} Mound;
static const Mound dunes[3] = {
    {.16, 1.02, .64, .45}, {.96, 1.00, .52, .42},
    {.58, 1.28, .98, .58}};
static double mound_top(const Art *a, int i, double x) {
  double s = (x - dunes[i].cx * a->cols) / (dunes[i].rx * a->cols);
  if (s <= -1 || s >= 1)
    return a->rows + 1;
  return a->rows * (dunes[i].cy - dunes[i].ry * sqrt(1 - s * s));
}
// Highest visible surface at a column (the smallest row), used to plant grass.
static double dune_surface(const Art *a, double x) {
  double top = a->rows * .56;
  for (int i = 0; i < 3; i++)
    top = fmin(top, mound_top(a, i, x));
  return top;
}
static void dune_grass(Art *a, double x, double y, int seed, int front) {
  const DunesPalette *p = &dunes_colors;
  uint32_t color = p->grass[front];
  for (int k = 0; k < 4; k++) {
    double h = 1.5 + hash(seed, k) * 3;
    double lean = (hash(seed, k + 4) - .5) * 2.4;
    for (int j = 0; j < h; j++)
      put(a, x + lean * (j / h), y - j, lean < 0 ? '/' : '\\', color);
  }
  put(a, x + (hash(seed, 9) - .5) * 3, y - 3, ':',
      front ? p->grass[1] : p->grass[0]);
}
static void dunes_caravan(Art *a, double t) {
  const DunesPalette *p = &dunes_colors;
  double travel = fmod(t * .20, 2);
  travel = travel < 1 ? travel : 2 - travel; // slow drift, then back
  int cx = (int)(a->cols * (.30 + .20 * travel));
  if (cx < 34)
    cx = 34;
  if (cx > a->cols - 6)
    cx = a->cols - 6;
  static const char *const camel[2][5] = {
      {".....HHH..", ".H..HHHHH.", "HHHHHHHHH.", ".H.H.H.H..",
       ".l.l.l.l.."},
      {".....HHH..", ".H..HHHHH.", "HHHHHHHHH.", ".H.H.H.H..",
       "..l.l.l.l."}};
  static const char *const guide[6] = {
      "..g.", ".gg.", "..g.", ".ll.", ".l..", "lll."};
  const SpriteKey keys[] = {
      {'H', 0, 0, p->camel[0]}, {'g', 0, 0, p->camel[0]},
      {'l', '|', p->camel[1], 0}};
  for (int i = 0; i < 3; i++) {
    int x = cx - 30 + i * 10;
    int bob = (int)floor(sin(t * 2.1 + i * 1.9) + .5);
    stamp(a, camel[(i + (int)(t * 2.2)) % 2], 5, keys, 3, x,
          (int)mound_top(a, 0, x) - 4 + bob, 0);
  }
  // Rider and blanket on the centre camel.
  int mx = cx - 20;
  int my = (int)mound_top(a, 0, mx);
  block(a, mx + 3, my - 2, p->blanket);
  block(a, mx + 4, my - 2, p->blanket);
  block(a, mx + 4, my - 4, p->camel[1]);
  int gx = cx - 33;
  stamp(a, guide, 6, keys, 3, gx, (int)mound_top(a, 0, gx) - 5, 0);
  put(a, gx + 2, (int)mound_top(a, 0, gx) - 4, '|', p->camel[1]);
  for (int i = 0; i < 10; i++)
    if (hash(i, 4) > .3)
      put(a, cx - 22 + i * 1.7, (int)mound_top(a, 0, cx) + 2 +
              hash(i, 3) * 2, '.', p->ripple_dark);
}
static void dunes_base(Art *a) {
  const DunesPalette *p = &dunes_colors;
  double radius = fmin(a->cols * .028, a->rows * .060);
  double sx = a->cols * .78, sy = a->rows * .150;
  // Sun: a warm disc with a tight bright core and a soft halo.
  for (double y = -radius * 2.4; y <= radius * 2.4; y++)
    for (double x = -radius * 2.8; x <= radius * 2.8; x++) {
      double d = hypot(x / 1.65, y) / radius;
      if (d >= 1 && d < 2.2 && hash(x * 2.1, y * 2.1) < (2.2 - d) * .13)
        put(a, sx + x, sy + y, '.', p->sun_halo);
    }
  for (double y = -radius; y <= radius; y++)
    for (double x = -radius * 1.65; x <= radius * 1.65; x++) {
      double d = hypot(x / 1.65, y) / radius;
      if (d <= .78)
        block(a, sx + x, sy + y, p->sun_inner);
      else if (d <= 1)
        block(a, sx + x, sy + y, p->sun);
    }
  // Two calm clouds: solid pixel lumps, lit on top and shaded below.
  static const struct {
    double u, v, w;
  } clouds[] = {{.20, .245, .15}, {.47, .325, .09}};
  for (int i = 0; i < 2; i++) {
    double cx = a->cols * clouds[i].u, cy = a->rows * clouds[i].v;
    int w = (int)(a->cols * clouds[i].w);
    for (int k = 0; k < w; k++) {
      double u = k / (double)w;
      int tall = 1 + (int)round(2.0 * exp(-pow((u - .38) * 3.4, 2))) +
                 (int)round(1.1 * exp(-pow((u - .72) * 4.8, 2)));
      for (int y = 0; y < tall; y++)
        block(a, cx - w / 2 + k, cy - y,
              y == tall - 1 ? p->cloud : mix_color(p->cloud, p->haze, .35));
      if (hash(i, k + 60) > .5)
        put(a, cx - w / 2 + k, cy + 1, '.', p->haze);
    }
  }
  // Distant dune sea and two mesas above it.
  double horizon = a->rows * .545;
  for (int x = 0; x < a->cols; x++) {
    double top = a->rows * (.545 + .012 * sin(x / (double)a->cols * 5.1) +
                            .010 * noise2(x * .045, 3.7));
    for (int y = (int)top; y < a->rows * .66; y++)
      block(a, x, y,
            ramp_color(p->dune[0], 4, .50 - (y - top) / (double)a->rows * .3,
                       x, y));
  }
  static const struct {
    double u, w, h;
  } mesas[] = {{.05, .20, .075}, {.87, .10, .048}};
  for (int i = 0; i < 2; i++) {
    int x0 = (int)(a->cols * mesas[i].u), w = (int)(a->cols * mesas[i].w),
        h = (int)(a->rows * mesas[i].h);
    for (int x = 0; x < w; x++) {
      double u = x / (double)w;
      double shoulder = u < .16 ? u / .16 : u > .84 ? (1 - u) / .16 : 1;
      double top = h * (.45 + .55 * shoulder);
      for (int y = 0; y < top; y++) {
        uint32_t c = mix_color(p->mesa[1], p->haze, .26 + y / (double)h * .30);
        block(a, x0 + x, horizon - y, c);
      }
      if (u > .80)
        for (int y = 0; y < top - 1; y += 2)
          if ((x + y) % 3)
            put(a, x0 + x, horizon - y, '.', p->mesa[0]);
    }
  }
  // Dune mounds, back to front. Each face is toned down to the crest that
  // hides it, so the visible slice carries the full light-to-shadow ramp.
  for (int i = 0; i < 3; i++) {
    double lit[4096];
    int count = a->cols < 4096 ? a->cols : 4096;
    for (int x = 0; x < count; x++) {
      double slope = (mound_top(a, i, x + 5) - mound_top(a, i, x - 5)) / 10.0;
      lit[x] = clamp(.5 + slope * 3.2, 0, 1);
    }
    for (int pass = 0; pass < 2; pass++)
      for (int x = 1; x < count - 1; x++)
        lit[x] = (lit[x - 1] + lit[x] * 2 + lit[x + 1]) * .25;
    for (int x = 0; x < a->cols; x++) {
      double top = mound_top(a, i, x);
      if (top >= a->rows)
        continue;
      double bottom = a->rows;
      for (int j = i + 1; j < 3; j++)
        bottom = fmin(bottom, mound_top(a, j, x));
      if (bottom <= top + 1)
        continue;
      int first = (int)ceil(top);
      double l = lit[x < count ? x : count - 1];
      for (int y = first; y < a->rows; y++) {
        double depth = clamp((y - top) / fmax(1, bottom - top), 0, 1);
        double tone = (.34 + .62 * l) - depth * (.28 + .16 * l);
        uint32_t color = ramp_color(p->dune[i], 5, tone, x, y);
        if (y == first && l > .54)
          color = p->rim_lit;
        else if (y == first && l < .36)
          color = p->rim_shade;
        block(a, x, y, color);
      }
    }
  }
  // Wind streaks follow the near crest in long, broken contours.
  for (int k = 0; k < 3; k++) {
    double offset = 3 + k * 3.5;
    for (int x = 0; x < a->cols; x++) {
      double top = mound_top(a, 2, x);
      double y = top + offset + sin(x * .10 + k * 1.7) * .8;
      if (y > a->rows - 1)
        continue;
      if (hash(x, k + 150) > .58)
        put(a, x, y, '-', p->ripple_dark);
      if (hash(x, k + 170) > .90)
        put(a, x, y - 1, '.', p->ripple_light);
    }
  }
  // Dune grasses sit on the topmost visible crest, not on fixed heights.
  static const struct {
    double u;
    int seed;
  } tufts[] = {{.08, 3}, {.33, 7}, {.68, 11}, {.90, 17}, {.24, 23}};
  for (int i = 0; i < 5; i++) {
    double x = a->cols * tufts[i].u;
    dune_grass(a, x, dune_surface(a, x), tufts[i].seed, i > 1);
  }
  for (int i = 0; i < 18; i++) {
    double x = hash(i, 101) * a->cols;
    double y = dune_surface(a, x) + 2 + hash(i, 102) * 6;
    put(a, x, y, hash(i, 103) > .5 ? 'o' : '.',
        hash(i, 104) > .4 ? p->pebble[0] : p->pebble[1]);
  }
  // A lone acacia on the left crest, flat-topped like the real thing.
  int tx = (int)(a->cols * .90), ty = (int)dune_surface(a, tx);
  static const char *const acacia[] = {
      "...cccc....", ".cccccccc..", "cccccccccc.", ".cccccccc..",
      "....cc.....", "....tt.....", "..t.tt.t...", "....tt....."};
  const SpriteKey tree_keys[] = {
      {'c', 0, 0, p->tree[0]}, {'t', '|', p->tree[1], 0}};
  stamp(a, acacia, 8, tree_keys, 2, tx - 5, ty - 8, 0);
  for (int i = 0; i < 9; i++)
    if (hash(i, 161) > .35)
      put(a, tx - 6 + hash(i, 162) * 12, ty - 7 + hash(i, 163) * 3,
          hash(i, 164) > .6 ? 'v' : '.', p->tree[1]);
  // Three birds over the far dune.
  for (int i = 0; i < 3; i++)
    put(a, a->cols * (.56 + i * .045), a->rows * (.33 + sin(i * 2) * .018),
        'v', p->bird);
}
static void dunes_frame(Art *a, double t) {
  const DunesPalette *p = &dunes_colors;
  dunes_caravan(a, t);
  // Sand skating low across the foreground.
  for (int i = 0; i < 34; i++) {
    double x = fmod(hash(i, 121) * a->cols + t * (1.2 + hash(i, 122) * 2.4),
                    a->cols);
    double y = dune_surface(a, x) +
               a->rows * (.03 + hash(i, 123) * .10) + sin(t * .7 + i);
    put(a, x, y, hash(i, 124) > .55 ? '~' : '.',
        hash(i, 125) > .6 ? p->ripple_light : p->ripple_dark);
  }
  // Heat shimmer above the farthest crest.
  for (int i = 0; i < a->cols * .4; i++) {
    double x = fmod(hash(i, 131) * a->cols + t * .3, a->cols);
    double y = a->rows * .55 + sin(t * 1.2 + x * .3) * .6;
    if (hash(i, 132) > .7)
      put(a, x, y, '.', p->haze);
  }
}

// ---------------------------------------------------------------------------

// Additional landscapes share drawing primitives, but keep distinct compositions.
static const uint32_t extra_bg[8][5] = {
 {0x171b40,0x493455,0xc47759,0x233d50,0x101f32},
 {0x101d32,0x263f56,0x46617a,0x657e91,0x9aafbb},
 {0x02080f,0x04131f,0x072632,0x08323b,0x03181f},
 {0xadc9ca,0xc7d8d0,0xe4dace,0x9aa995,0x657d71},
 {0x090f23,0x172139,0x29344a,0x182a30,0x101d25},
 {0x080f28,0x14233c,0x243d52,0x31475e,0x142c40},
 {0xb9c9c3,0xd5d6ba,0xdcc7a1,0x879b91,0x526f71},
 {0x142334,0x243c51,0x355269,0x243746,0x18222e}
};
// Branch tiers break up the silhouette; snow catches only upper edges.
static void small_tree(Art *a,int x,int y,int h,uint32_t ink) {
 fill_rect(a,x,y,x+1,y+h,ink);
 for(int r=1;r<h;r++) {
  int radius=(int)(r*.34)*(r%4==0?.68:1);
  for(int c=-radius;c<=radius;c++) {
   uint32_t shade=mix_color(ink,0x789399,(c<0?.12:.02));
   block(a,x+c,y+r,shade);
   if(a->scene==7 && r%4==1 && c<radius-1)
    put(a,x+c,y+r,'-',0x879eae);
  }
 }
}
static void extra_detail(Art *a) {
 int n=a->scene-6,w=a->cols,h=a->rows;
 if(n==0) {
  // Stratified rock, a tapering tower, gallery and lantern room.
  for(int y=h*.7;y<h;y++)for(int x=0;x<w;x++)
   if(y>h*.83-x*.12) {
    double v=noise2(x*.12,y*.35);
    glyph_tile(a,x,y,v>.6?'_':v<.3?'/':0,0x56616a,
       mix_color(0x17232e,0x424753,v*.6));
   }
  int x=w*.76,y=h*.34,foot=h*.77;
  for(int r=y;r<foot;r++) {
   int radius=2+(r-y)/9;
   for(int c=-radius;c<=radius;c++)block(a,x+c,r,
    mix_color(0x65717b,0xd4c5b0,(double)(c+radius)/(2*radius+1)));
   if((r-y)%8==3)put(a,x,r,'|',0x354352);
  }
  fill_rect(a,x-4,y-3,x+4,y-2,0x27313d);
  for(int c=-3;c<=3;c++)glyph_tile(a,x+c,y-1,c%2?'|':' ',0x4a4241,0xffce88);
  fill_rect(a,x-5,y,x+5,y,0x25333e);
  for(int c=-4;c<=4;c++)put(a,x+c,y+1,'|',0x73808a);
  for(int i=0;i<4;i++)fill_rect(a,x-i,y-6+i,x+i,y-6+i,0x33404c);
 } else if(n==1) {
  // Uneven snow banks, distant trees and a substantial log cabin.
  for(int x=0;x<w;x++) {
   int y=h*.82+sin(x*.043)*2;
   fill_rect(a,x,y,x,h,mix_color(0x819cab,0xb1c3cc,(double)x/w));
   if(x%4==0)put(a,x,y,'_',0xc2d0d7);
  }
  int x=w*.55,y=h*.62;
  for(int r=0;r<8;r++)for(int c=-10;c<=10;c++)glyph_tile(a,x+c,y+r,r%2?'_':0,0x8a7262,c<0?0x5d4c47:0x493e3e);
  for(int r=0;r<8;r++)for(int c=-r*2-1;c<=r*2+1;c++)block(a,x+c,y-8+r,r<5?0xbacbd5:0x8fa5b4);
  fill_rect(a,x-16,y,x+16,y,0x526575);
  fill_rect(a,x+6,y-10,x+8,y-5,0x665758);
  fill_rect(a,x+6,y-10,x+9,y-10,0xc3d0d5);
  fill_rect(a,x+5,y+2,x+8,y+7,0x302f34);
  for(int r=1;r<5;r++)for(int c=-5;c<=0;c++)block(a,x+c,y+r,c==-3||r==3?0x71503e:0xffc477);
  for(int i=0;i<12;i++)put(a,x+6+i*.7,y+9+i*.45,'_',0x718b9b);
 } else if(n==2) {
  for(int x=0;x<w;x++) {
   int y=h*.88+sin(x*.08)*2;
   for(int r=y;r<h;r++)glyph_tile(a,x,r,hash(x,r)>.86?'.':0,0x297779,mix_color(0x06151e,0x113c40,noise2(x*.1,r*.3)));
  }
  for(int i=0;i<9;i++)for(int r=0;r<4;r++)put(a,w*(.1+i*.1)+sin(r+i)*2,h*.9-r,'/',0x1e6b6a);
 } else if(n==3 || n==6) {
  // Branching, irregular crowns rather than a row of identical circles.
  for(int i=0;i<8;i++) {
   double x=w*(i%2?.88:.10)+(i/2)*w*(i%2?-.047:.047),y=h*(.18+(i/2)*.10);
   for(int r=0;r<h*.38;r++) {
    put(a,x+r*.13,y+r,'/',0x61564d);
    if(r%3==0)for(int j=0;j<7;j++)put(a,x+r*.13+(i%2?-j:j),y+r-j*.4,'_',0x756357);
   }
   for(int r=-6;r<7;r++)for(int c=-15;c<=15;c++) {
    double edge=noise2((c+i*13)*.24,(r+i*7)*.3);
    if(c*c/12.+r*r<20+edge*25) {
     uint32_t dark=n==3?0xa8798c:0x995136,light=n==3?0xf1c6d0:0xe1a454;
     glyph_tile(a,x+c,y+r,hash(c+i,r)>.8?'*':0,mix_color(dark,light,.8),mix_color(dark,light,clamp(edge*.6+(6-r)*.035,0,1)));
    }
   }
  }
  if(n==3) {
   for(int x=w*.35;x<w*.65;x++) {
    int y=h*.68-3*sin((x-w*.35)/(w*.3)*3.14159);
    fill_rect(a,x,y,x,y+1,0x8c8980);put(a,x,y-2,'_',0x707c73);
    if(x%5==0)fill_rect(a,x,y-2,x,y,0x707c73);
   }
  } else {
   for(int y=h*.72;y<h;y++)for(int x=w*.32;x<w*.84;x++) {
    block(a,x,y,mix_color(0x526f71,0x87958b,(1.-(double)y/h)*2));
    if(hash(x,y)>.89)put(a,x,y,'-',0x9ba398);
   }
  }
 } else if(n==4 || n==5) {
  for(int i=0;i<75;i++)if(hash(i,14)>.3)put(a,hash(i,8)*w,hash(i,9)*h*.42,'.',0x77919d);
  if(n==4) {
   for(int x=0;x<w;x++) {
    int y=h*.6+sin(x*.04)*3;
    fill_rect(a,x,y,x,h*.73,0x21363a);
    if(x%9==0)small_tree(a,x,y-4,6,0x1c3033);
    if(x%13==0){fill_rect(a,x,h*.79,x+1,h*.9,0x1a2229);put(a,x,h*.9,'/',0x33454a);}
   }
   fill_rect(a,0,h*.77,w,h*.77,0x4e5a60);
  } else {
   for(int x=0;x<w;x++) {
    double peak=fabs(sin(x*.037)+.4*sin(x*.081));
    int y=h*(.58-.14*peak);
    fill_rect(a,x,y,x,h*.67,0x344e66);
    for(int r=0;r<2+peak*2;r++)block(a,x,y+r,r?0x748e9f:0xa0b3ba);
   }
   for(int i=0;i<7;i++)small_tree(a,i*w/7,h*.6,7+i%3,0x122b39);
   for(int i=0;i<80;i++)put(a,hash(i,31)*w,h*(.72+hash(i,32)*.27),'-',0x355569);
  }
 } else if(n==7) {
  // Interior walls frame cool glass; a sleeping tabby rests on the sill.
  for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(x<w*.16||x>w*.84||y<h*.12||y>h*.84)
   block(a,x,y,mix_color(0x302b32,0x514047,(double)y/h));
  for(int i=0;i<15;i++) {
   int x=w*.2+i*w*.04,y=h*(.48+hash(i,6)*.2);
   fill_rect(a,x,y,x+3,h*.79,0x223546);
   if(i%3==0)put(a,x+1,y+2,'.',0xb5a078);
  }
  fill_rect(a,w*.5,h*.12,w*.51,h*.8,0x786252);
  fill_rect(a,w*.16,h*.45,w*.84,h*.46,0x786252);
  int cx=w*.4,cy=h*.75;
  for(int r=-3;r<=2;r++)for(int c=-9;c<=10;c++)if(c*c/11.+r*r<11)
   block(a,cx+c,cy+r,mix_color(0x936e58,0xd4ad82,(3.-r)/6));
  for(int r=-4;r<=1;r++)for(int c=-10;c<=-4;c++)block(a,cx+c,cy+r,0xc9a17b);
  put(a,cx-10,cy-5,'^',0xe0b994);put(a,cx-4,cy-5,'^',0xe0b994);
  put(a,cx-9,cy-1,'_',0x554639);put(a,cx-5,cy-1,'_',0x554639);
  for(int i=0;i<4;i++)put(a,cx+i*3,cy-2,'/',0x795a48);
  fill_rect(a,w*.69,h*.73,w*.75,h*.79,0xa66c54);
  for(int i=0;i<5;i++)for(int r=0;r<7;r++)put(a,w*.72+sin(i*2)*r*.6,h*.73-r,'/',i%2?0x6b8c72:0x87a282);
 }
}
static void extra_base(Art *a) {
 int n=a->scene-6,w=a->cols,h=a->rows;
 for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
  double v=(double)y/h*4; int k=(int)v; if(k>3)k=3;
  block(a,x,y,mix_color(extra_bg[n][k],extra_bg[n][k+1],
        clamp(v-k+(bayer(x,y)-.5)*.035,0,1)));
 }
 if(n==0) {
  for(int y=h*2/3;y<h;y++) for(int x=0;x<w;x++) if(y>h*.83-x*.12) block(a,x,y,0x202b35);
  int x=w*.76,y=h*.34;
  fill_rect(a,x-2,y,x+2,h*.78,0xb1aaa0);fill_rect(a,x-3,y-2,x+3,y,0x253443);
  fill_rect(a,x-2,y-1,x+2,y-1,0xffd58b);
  put(a,w*.24,h*.25,'v',0xd6b7a4);put(a,w*.3,h*.29,'v',0xd6b7a4);
 } else if(n==1) {
  fill_rect(a,0,h*.8,w,h,0x9aafbb);
  for(int i=0;i<22;i++) small_tree(a,hash(i,42)*w,h*(.44+hash(i,43)*.14),h*(.18+hash(i,44)*.18),i%3?0x294653:0x193747);
  int x=w*.55,y=h*.62;
  fill_rect(a,x-8,y,x+8,y+7,0x584847);
  for(int r=0;r<6;r++) fill_rect(a,x-r*2,y-6+r,x+r*2,y-6+r,0xc0d0d5);
  fill_rect(a,x+4,y-8,x+5,y-3,0x635458);
  fill_rect(a,x-2,y+2,x+1,y+4,0xffcb79);
 } else if(n==3 || n==6) {
  for(int y=h*.58;y<h;y++) for(int x=0;x<w;x++) if(abs(x-w/2)<(y-h*.5)*.7) block(a,x,y,0xc4bbaa);
  for(int i=0;i<5;i++) {
   int x=(i%2?w*.85:w*.15)+i,y=h*.18+i%3;
   fill_rect(a,x,y,x+1,h*.77,0x605448);

  }
  fill_rect(a,w*.64,h*.66,w*.64+2,h*.78,0x75847d);fill_rect(a,w*.64-1,h*.66,w*.64+3,h*.66,0x555f5a);
  put(a,w*.64+1,h*.69,'*',0xffdc9b);
 } else if(n==4) {
  for(int x=0;x<w;x++)fill_rect(a,x,h*.64+sin(x*.06)*3,x,h,0x15282b);
  fill_rect(a,0,h*.78,w,h*.78,0x647075);
 } else if(n==5) {
  for(int x=0;x<w;x++)fill_rect(a,x,h*.55+sin(x*.055)*4,x,h*.68,0x708b99);
 } else if(n==7) {
  fill_rect(a,w*.16,h*.12,w*.18,h*.8,0x685950);fill_rect(a,w*.82,h*.12,w*.84,h*.8,0x685950);
  fill_rect(a,w*.16,h*.8,w*.84,h*.84,0x9c8069);
  fill_rect(a,w*.5,h*.12,w*.51,h*.8,0x685950);
  for(int y=-3;y<=3;y++)for(int x=-9;x<=9;x++)if(x*x/9+y*y<12)block(a,w*.43+x,h*.75+y,0xb78d71);
  put(a,w*.36,h*.72,'^',0xe2b793);put(a,w*.39,h*.72,'^',0xe2b793);
  fill_rect(a,w*.7,h*.73,w*.74,h*.79,0xb77d62);
  for(int i=0;i<6;i++)put(a,w*.72+sin(i)*3,h*.72-i,'/',0x719b83);
 }
}
static void extra_frame(Art *a,double t) {
 int n=a->scene-6,w=a->cols,h=a->rows;
 if(n==0) {
  double aim=sin(t*.16)*.9;
  for(int y=0;y<h*.66;y++)for(int x=0;x<w*.74;x++)if(fabs((y-h*.34)/(w*.76-x)-aim)<.055) {
   Cell *c=&a->cells[y*w+x];c->bg=mix_color(c->bg,0xffdc99,.22);
  }
  for(int i=0;i<45;i++)put(a,fmod(i*17+t*.6,w),h*(.6+hash(i,77)*.36),'~',0x536f80);
 } else if(n==1 || n==3 || n==6 || n==7) {
  for(int i=0;i<40;i++) {
   double x=fmod(hash(i,80)*w+sin(t*.2+i)*3+w,w),y=fmod(hash(i,81)*h+t*(n==7?3:.7),h);
   if(n!=7 || (x>w*.19 && x<w*.81 && y<h*.7))put(a,x,y,n==1?(i%3?'.':'*'):n==7?'|':',',n==1?0xd9e5ea:n==3?0xf6c5d1:n==6?0xe6a255:0x668499);
  }
  if(n==1)for(int i=0;i<6;i++)put(a,w*.55+7+sin(t*.3+i*.6)*(1+i*.3),h*.62-11-i,'.',0x9cabb5);
  if(n==7)put(a,w*.49+sin(t*.5),h*.77,'~',0xc69c7f);
 } else if(n==2) {
  for(int i=0;i<18;i++)for(int r=0;r<8+i%9;r++)put(a,i*w/18+sin(t*.25+i+r*.3)*2,h-1-r,r%3?'|':'/',r%2?0x19575c:0x25747a);
  for(int i=0;i<9;i++)put(a,(i*23)%w,h-fmod(t*.8+i*11,h),'o',0x327a83);
  for(int i=0;i<5;i++) {
   int x=w*(i+1)/6+sin(t*.2+i)*3,y=h*(.25+.1*(i%3))+sin(t*.3+i)*2;
   uint32_t c=mix_color(0x537b98,0xc897ec,.5+.5*sin(t*.7+i));
   for(int r=-5;r<9;r++)for(int d=-9;d<=9;d++) {
    int px=x+d,py=y+r;
    if(px<0||py<0||px>=w||py>=h)continue;
    double halo=exp(-(d*d/32.+r*r/18.))*.24;
    Cell *cell=&a->cells[py*w+px];cell->bg=mix_color(cell->bg,c,halo);
   }
   for(int r=-3;r<=0;r++)for(int d=-5;d<=5;d++)if(d*d+r*r*3<29)
    glyph_tile(a,x+d,y+r,r==0?'_':hash(d,i)>.7?':':0,c,mix_color(0x123442,c,.22+.18*(r+3)/3.));
   for(int d=-4;d<=4;d+=2)for(int r=1;r<7+(i%3);r++)put(a,x+d+sin(t*.5+r*.45+i),y+r,r%3?'|':':',mix_color(c,0x103239,r/11.));
  }
 } else if(n==4) {
  double phase=fmod(t+22,65); if(phase<18)for(int i=0;i<4;i++) {
   int x=(phase/18*(w+70))-70+i*16,y=h*.78-4;
   fill_rect(a,x,y,x+13,y+3,0x344854);fill_rect(a,x,y,x+13,y,0x69747b);for(int j=2;j<12;j+=3)glyph_tile(a,x+j,y+1,'=',0xffd897,0xa87e54);put(a,x+2,y+4,'o',0x161d25);put(a,x+11,y+4,'o',0x161d25);
  }
 } else if(n==5) {
  for(int x=0;x<w;x++)for(int r=0;r<5;r++) {
   int y=h*.23+sin(x*.025+t*.12)*4+sin(x*.07+t*.08)*2+r;
   if(y>=0&&y<h) {
    Cell *cell=&a->cells[y*w+x];
    double fade=sin((r+1)*3.14159/7)*(.5+.5*sin(x*.035+t*.18));
    uint32_t glow=mix_color(0x33786f,0x8173a7,(double)x/w);
    cell->bg=mix_color(cell->bg,glow,fade*.38);
    if(hash(x,r)>.3)put(a,x,y,r%2?'/':':',mix_color(cell->bg,0x96c4ac,fade*.55));
   }
   if(r%2==0)put(a,x,h*.87-(y-h*.24)*.35,'-',0x3c6573);
  }
 }
}

// Frame animation for the original forest landscape.
// ---------------------------------------------------------------------------
static void forest_frame(Art *a, double t) {
  for (int i = 0; i < 32; i++) {
    double x = a->cols * (.08 + hash(i, 70) * .84) + sin(t * .18 + i) * 2;
    double y = a->rows * (.35 + hash(i, 71) * .48) + cos(t * .23 + i) * 1.4;
    double light = .5 + .5 * sin(t * .8 + i * 1.7);
    if (light > .45)
      put(a, x, y, light > .87 ? '*' : '.',
          light > .87 ? ink_color(a, 0xc8c77f) : ink_color(a, 0x728451));
  }
  for (int i = 0; i < 45; i++)
    put(a, fmod(i * 7.3 + t * .5, a->cols),
        a->rows * .78 + sin(i * .4 + t * .15) * 2, '~',
        ink_color(a, 0x2e473a));
}
static void mountain_frame(Art *a, double t) {
  for (int i = 0; i < a->cache->star_count; i++) {
    const Star *star = &a->cache->stars[i];
    double light = .5 + .5 * sin(t * star->speed + star->phase);
    int g = 65 + (int)round(light * 12) * 8;
    a->cells[star->index] = (Cell){
        .bg = 0,
        .fg = (uint32_t)((int)(g * .82) << 16 | (int)(g * .94) << 8 | (g + 15)),
        .c = (uint8_t)star->c};
  }
  double cycle = fmod(t + 8, 29);
  if (cycle < 1.6) {
    double sx = a->cols * (.18 + cycle * .15),
           sy = a->rows * (.12 + cycle * .07);
    for (int k = 0; k < 13; k++)
      put(a, sx - k, sy - k * .3, k == 0 ? '*' : '.',
          k < 3 ? 0xe0e5d6 : 0x40596c);
  }
  double water = a->rows * .7;
  double *waves = a->cache->waves;
  for (int x = 0; x < a->cols; x++) {
    waves[x * 3] = sin(x * .16 + t * .4);
    waves[x * 3 + 1] = cos(x * .16 + t * .4);
    waves[x * 3 + 2] = sin(x * .052 - t * .2);
  }
  for (int y = (int)ceil(water); y < a->rows; y++) {
    double depth = (y - water) / (a->rows - water);
    double sy = waves[a->cols * 3 + y * 2],
           cy = waves[a->cols * 3 + y * 2 + 1];
    for (int x = 0; x < a->cols; x++) {
      int index = y * a->cols + x;
      if (a->base[index].c)
        continue;
      double wave = waves[x * 3] * cy + waves[x * 3 + 1] * sy + waves[x * 3 + 2];
      unsigned char noise = a->cache->water_noise[index];
      if ((noise & 1) || wave > 1.55)
        a->cells[index] = (Cell){
            .bg = 0,
            .fg = wave > 1.3   ? 0x47616a
                  : depth < .2 ? 0x2b4551
                               : 0x1b343e,
            .c = (uint8_t)(wave > 1.3 ? '~' : '-')};
      if (fabs(x - a->cols * .76) < a->cols * .04 * (depth + .3) &&
          (noise & 2))
        a->cells[index] = (Cell){.bg = 0, .fg = 0x6b7976, .c = '~'};
    }
  }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
int art_init(Art *a, int width, int height, int scene) {
  memset(a, 0, sizeof(*a));
  if (width < 1 || height < 1 || width > 16384 || height > 16384 || scene < 0 ||
      scene >= ART_SCENES)
    return -1;
  a->width = width;
  a->height = height;
  a->scene = scene;
  a->cw = width < 600 ? 6 : 8;
  a->ch = a->cw * 1.65;
  a->cols = (int)ceil(width / a->cw);
  a->rows = (int)ceil(height / a->ch);
  size_t bytes = (size_t)a->cols * a->rows * sizeof(Cell);
  a->cells = calloc(1, bytes);
  a->base = calloc(1, bytes);
  if (!a->cells || !a->base) {
    art_free(a);
    return -1;
  }
  switch (scene) {
  case 0: pond_base(a); break;
  case 1: sky_base(a); break;
  case 2: moon(a); mountain_base(a); break;
  case 3: forest_base(a); break;
  case 4: rain_base(a); break;
  case 5: dunes_base(a); break;
  default: extra_base(a); extra_detail(a); break;
  }
  memcpy(a->base, a->cells, bytes);
  a->cache = calloc(1, sizeof(*a->cache));
  if (!a->cache) {
    art_free(a);
    return -1;
  }
  if (scene == 2) {
    int count = (int)round(a->cols * a->rows * .065);
    a->cache->stars = calloc((size_t)count + 1, sizeof(Star));
    if (!a->cache->stars) {
      art_free(a);
      return -1;
    }
    for (int i = 0; i < count; i++) {
      int x = (int)round(hash(i, 4) * a->cols),
          y = (int)round(hash(i, 5) * a->rows * .7);
      if (x >= a->cols || y >= a->rows || a->base[y * a->cols + x].c)
        continue;
      a->cache->stars[a->cache->star_count++] =
          (Star){y * a->cols + x, i, .3 + hash(i, 6),
                 hash(i, 7) > .97  ? '+'
                 : hash(i, 8) > .8 ? '*'
                                   : '.'};
    }
    a->cache->water_noise = calloc((size_t)a->cols * a->rows, 1);
    a->cache->waves =
        calloc((size_t)(a->cols * 3 + a->rows * 2), sizeof(double));
    if (!a->cache->water_noise || !a->cache->waves) {
      art_free(a);
      return -1;
    }
    double water = a->rows * .7;
    for (int y = (int)ceil(water); y < a->rows; y++) {
      double depth = (y - water) / (a->rows - water);
      a->cache->waves[a->cols * 3 + y * 2] = sin(depth * 21);
      a->cache->waves[a->cols * 3 + y * 2 + 1] = cos(depth * 21);
      for (int x = 0; x < a->cols; x++) {
        double noise = hash(x, y);
        a->cache->water_noise[y * a->cols + x] =
            (noise > .53 ? 1 : 0) | (noise > .48 ? 2 : 0);
      }
    }
  }
  return 0;
}
void art_free(Art *a) {
  cache_free(a->cache);
  a->cache = NULL;
  free(a->cells);
  free(a->base);
  a->cells = a->base = NULL;
}
void art_frame(Art *a, double t) {
  memcpy(a->cells, a->base, (size_t)a->cols * a->rows * sizeof(Cell));
  switch (a->scene) {
  case 0: pond_frame(a, t); break;
  case 1: sky_frame(a, t); break;
  case 2: mountain_frame(a, t); break;
  case 3: forest_frame(a, t); break;
  case 4: rain_frame(a, t); break;
  case 5: dunes_frame(a, t); break;
  default: extra_frame(a, t); break;
  }
}

static void rgb(cairo_t *cr, uint32_t color) {
  cairo_set_source_rgb(cr, ((color >> 16) & 255) / 255.,
                       ((color >> 8) & 255) / 255., (color & 255) / 255.);
}
static uint32_t cell_bg(const Art *a, Cell cell, int row) {
  return cell.bg ? cell.bg : bg_row(a, row);
}
static void font(cairo_t *cr, const Art *a) {
  cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, a->cw + 1);
}
void art_paint(Art *a, cairo_t *cr, Cell *previous) {
  font(cr, a);
  for (int y = 0; y < a->rows; y++)
    for (int x = 0; x < a->cols; x++) {
      int i = y * a->cols + x;
      Cell c = a->cells[i];
      if (previous && c.c == previous[i].c && c.bg == previous[i].bg &&
          c.fg == previous[i].fg)
        continue;
      double px = x * a->cw, py = floor(y * a->ch);
      rgb(cr, cell_bg(a, c, y));
      cairo_rectangle(cr, px, py, a->cw, floor((y + 1) * a->ch) - py);
      cairo_fill(cr);
      if (c.c) {
        char text[2] = {(char)c.c, 0};
        rgb(cr, ink_color(a, c.fg));
        cairo_move_to(cr, px, py + a->cw);
        cairo_show_text(cr, text);
      }
      if (previous)
        previous[i] = c;
    }
}
static uint32_t *tile_pixels(Art *a, Cell cell, uint32_t bg, int scale) {
  struct ArtCache *cache = a->cache;
  uint32_t hash_key = (bg * 2654435761u) ^ (cell.fg * 40503u) ^
                      ((uint32_t)cell.c * 2246822519u);
  unsigned int slot = hash_key >> (32 - 12);
  unsigned int first = slot;
  for (;;) {
    Tile *entry = &cache->tiles[slot];
    if (!entry->pixels)
      break;
    if (entry->bg == bg && entry->fg == cell.fg && entry->c == cell.c)
      return entry->pixels;
    slot = (slot + 1) & (TILE_SLOTS - 1);
    if (slot == first) {
      slot = hash_key >> (32 - 12);
      break;
    }
  }
  int width = cache->tile_width, height = cache->tile_height;
  uint32_t *pixels = malloc((size_t)width * height * 4);
  if (!pixels)
    return NULL;
  cairo_surface_t *surface = cairo_image_surface_create_for_data(
      (unsigned char *)pixels, CAIRO_FORMAT_RGB24, width, height, width * 4);
  cairo_t *cr = cairo_create(surface);
  cairo_scale(cr, scale, scale);
  rgb(cr, bg);
  cairo_paint(cr);
  font(cr, a);
  rgb(cr, ink_color(a, cell.fg));
  cairo_move_to(cr, 0, a->cw);
  char text[2] = {(char)cell.c, 0};
  cairo_show_text(cr, text);
  cairo_surface_flush(surface);
  int failed = cairo_status(cr) != CAIRO_STATUS_SUCCESS;
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  if (failed) {
    free(pixels);
    return NULL;
  }
  Tile *entry = &cache->tiles[slot];
  free(entry->pixels);
  entry->pixels = pixels;
  entry->bg = bg;
  entry->fg = cell.fg;
  entry->c = cell.c;
  return pixels;
}
int art_blit(Art *a, unsigned char *pixels, int stride, int scale,
             Cell *previous) {
  if (scale < 1 || scale > 8 || stride < a->width * scale * 4)
    return -1;
  struct ArtCache *cache = a->cache;
  if (cache->scale != scale) {
    for (int i = 0; i < TILE_SLOTS; i++) {
      free(cache->tiles[i].pixels);
      cache->tiles[i] = (Tile){0};
    }
    cache->scale = scale;
    cache->tile_width = (int)a->cw * scale;
    cache->tile_height = (int)ceil(a->ch) * scale;
  }
  for (int y = 0; y < a->rows; y++) {
    int py = (int)floor(y * a->ch) * scale;
    int height = (int)floor((y + 1) * a->ch) * scale - py;
    if (py + height > a->height * scale)
      height = a->height * scale - py;
    for (int x = 0; x < a->cols; x++) {
      int index = y * a->cols + x;
      Cell cell = a->cells[index];
      if (previous && cell.c == previous[index].c &&
          cell.bg == previous[index].bg && cell.fg == previous[index].fg)
        continue;
      int px = (int)(x * a->cw) * scale, width = cache->tile_width;
      if (px + width > a->width * scale)
        width = a->width * scale - px;
      uint32_t bg = cell_bg(a, cell, y);
      uint32_t *source = cell.c ? tile_pixels(a, cell, bg, scale) : NULL;
      if (cell.c && !source)
        return -1;
      for (int row = 0; row < height; row++) {
        uint32_t *destination = (uint32_t *)(pixels + (py + row) * stride) + px;
        if (source) {
          memcpy(destination, source + row * cache->tile_width,
                 (size_t)width * 4);
        } else {
          uint32_t color = 0xff000000u | bg;
          for (int col = 0; col < width; col++)
            destination[col] = color;
        }
      }
      if (previous)
        previous[index] = cell;
    }
  }
  return 0;
}

// Damage is relative to the last presented frame, not to the older shm buffer.
static int same_cell(Cell a, Cell b) {
  return a.c == b.c && a.bg == b.bg && a.fg == b.fg;
}
int art_damage(Art *a, Cell *presented,
               void (*emit)(void *, int, int, int, int), void *data) {
  int rectangles = 0;
  for (int y = 0; y < a->rows; y++) {
    int start = -1, last = -1;
    for (int x = 0; x <= a->cols; x++) {
      if (x < a->cols &&
          !same_cell(a->cells[y * a->cols + x], presented[y * a->cols + x])) {
        if (start < 0)
          start = x;
        last = x;
      } else if (start >= 0 && (x == a->cols || x - last > 2)) {
        int px = (int)(start * a->cw), end = (int)((last + 1) * a->cw);
        int py = (int)floor(y * a->ch), bottom = (int)floor((y + 1) * a->ch);
        if (end > a->width)
          end = a->width;
        if (bottom > a->height)
          bottom = a->height;
        emit(data, px, py, end - px, bottom - py);
        rectangles++;
        start = -1;
      }
    }
  }
  memcpy(presented, a->cells, (size_t)a->cols * a->rows * sizeof(Cell));
  return rectangles;
}
