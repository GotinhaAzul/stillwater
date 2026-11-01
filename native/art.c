#include "art.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
// Scene constants and colored character tiles are reused across frames.
#define TILE_SLOTS 512
typedef struct {
  uint32_t key;
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
static void put(Art *a, double x, double y, char c, uint32_t color) {
  int ix = (int)round(x), iy = (int)round(y);
  if (ix >= 0 && ix < a->cols && iy >= 0 && iy < a->rows)
    a->cells[iy * a->cols + ix] = (Cell){color, c};
}
uint32_t art_background(int scene) {
  static const uint32_t backgrounds[ART_SCENES] = {
      0x081411, 0x080f20, 0x0a1520, 0x09150f, 0x0a1519, 0x18140e};
  return backgrounds[scene >= 0 && scene < ART_SCENES ? scene : 0];
}
static const uint32_t theme_backgrounds[ART_THEMES] = {
    0, 0xf6f0e3, 0xf7e7df, 0xeaf1f0, 0x1d100e, 0x10152a};
static const uint32_t theme_inks[ART_THEMES] = {0,        0x283c32, 0x703c45,
                                                0x204c58, 0xe7ad78, 0xa2b9dc};
uint32_t art_bg(const Art *a) {
  return a->theme ? theme_backgrounds[a->theme] : art_background(a->scene);
}
int art_set_theme(Art *a, int theme) {
  if (theme < 0 || theme >= ART_THEMES)
    return -1;
  if (a->cache && theme != a->theme)
    for (int i = 0; i < TILE_SLOTS; i++) {
      free(a->cache->tiles[i].pixels);
      a->cache->tiles[i] = (Tile){0};
    }
  a->theme = theme;
  return 0;
}
static uint32_t art_color(const Art *a, uint32_t color) {
  if (!a->theme)
    return color;
  double source[3] = {(color >> 16) & 255, (color >> 8) & 255, color & 255};
  double luminance =
      (source[0] * .2126 + source[1] * .7152 + source[2] * .0722) / 255.;
  uint32_t bg = art_bg(a), ink = theme_inks[a->theme], result = 0;
  double weight = a->theme <= 3 ? .22 + luminance * .70 : luminance * .95;
  for (int channel = 0; channel < 3; channel++) {
    int shift = 16 - channel * 8;
    double base = (bg >> shift) & 255, target = (ink >> shift) & 255;
    double chroma = (source[channel] - luminance * 255) * .65;
    int value =
        (int)round(clamp(base + (target - base) * weight + chroma, 0, 255));
    result |= (uint32_t)value << shift;
  }
  return result;
}
static void ring(Art *a, double x, double y, double r, uint32_t color,
                 double phase) {
  for (double v = 0; v < 6.28; v += .07)
    if (sin(v * 5 + phase) > 0)
      put(a, x + cos(v) * r, y + sin(v) * r * .43,
          fabs(cos(v)) > .7 ? '|' : '~', color);
}
static void moon(Art *a) {
  double r = fmin(a->cols * .035, a->rows * .085);
  for (double y = -r; y <= r; y++)
    for (double x = -r * 1.8; x <= r * 1.8; x++) {
      double d = pow(x / 1.65, 2) + y * y,
             cut = pow(x / 1.65 - r * .4, 2) + pow(y + r * .19, 2);
      if (d < r * r && cut > r * r * .86)
        put(a, a->cols * .76 + x, a->rows * .19 + y,
            hash(x, y) > .6 ? 'o' : ':', 0xd1d5b4);
    }
}
static void pond_base(Art *a) {
  for (int y = 0; y < a->rows; y++)
    for (int x = 0; x < a->cols; x++) {
      double n = hash(x, y);
      if (n < .13)
        put(a, x, y, n < .025 ? '~' : '.', 0x17372f);
    }
  for (int i = 0; i < 95; i++) {
    double x = hash(i, 1) * a->cols, y = hash(i, 2) * a->rows;
    if ((x < a->cols * .14 && y < a->rows * .6) ||
        (x > a->cols * .88 && y > a->rows * .2))
      for (int k = 0; k < 2 + hash(i, 3) * 7; k++)
        put(a, x + sin(k * .25 + i) * 1.2, y - k, '/', 0x2e5140);
  }
  const double pads[6][3] = {{.22, .24, 5.5}, {.29, .19, 3.8}, {.78, .64, 6.1},
                             {.83, .71, 4},   {.16, .64, 4.2}, {.72, .19, 3.8}};
  for (int i = 0; i < 6; i++) {
    double x = pads[i][0] * a->cols, y = pads[i][1] * a->rows, r = pads[i][2];
    ring(a, x, y, r + 2, 0x214138, 0);
    for (double yy = -r * .53; yy <= r * .53; yy++)
      for (double xx = -r; xx <= r; xx++) {
        double d = xx * xx / (r * r) + yy * yy / (r * r * .28);
        if (d < 1 && !(xx > 0 && fabs(yy) < xx * .2))
          put(a, x + xx, y + yy,
              d > .72                  ? 'o'
              : hash(xx + i, yy) > .55 ? ':'
                                       : '=',
              d > .72 ? 0x56734c : 0x3a5839);
      }
    if (i == 0 || i == 2) {
      put(a, x - 1, y - 1, '{', 0xe1b6a2);
      put(a, x, y - 1, '*', 0xf4d9be);
      put(a, x + 1, y - 1, '}', 0xc48584);
      put(a, x, y, 'v', 0xa97076);
    }
  }
}
static void fish(Art *a, double cx, double cy, double angle, int id, double t) {
  double len = 7 + id % 3, co = cos(angle), si = sin(angle);
  for (double u = -len - 4; u <= len; u += .6) {
    int tail = u < -len * .65;
    double bw = 2.8 * sqrt(fmax(0, 1 - pow(u / len, 2))),
           tw = tail ? (-u - len * .65) / (len * .35 + 4) * 2.5 : 0,
           fin = fabs(u + 1) < 2.5 ? 3.8 - fabs(u + 1) * .6 : 0,
           sway = sin(t * 1.7 + u * .5 + id) * clamp(-u / len, 0, 1) * 1.1,
           patch_u = sin(u * .9 + id * 2);
    for (double v = -3.7; v <= 3.7; v += .65) {
      double vv = v + sway;
      if (fabs(vv) > fmax(bw, fmax(tw, fin * .8)))
        continue;
      int edge = fabs(vv) > bw * .68,
          patch = patch_u + cos(vv * 1.7 + id) > .15;
      uint32_t color = id % 3 == 0 ? (patch ? 0xd4cbb0 : 0xb56c48)
                                   : (patch ? 0xda9864 : 0x7f4034);
      char c = edge ? ':' : patch ? 'o' : '#';
      if (fabs(vv) > bw && !tail) {
        color = 0x5c6b52;
        c = '/';
      }
      if (tail) {
        c = ((int)round(u + t * 2) % 2) ? '/' : ':';
        color = 0xb28461;
      }
      if (u > len * .65 && fabs(vv) < .6) {
        c = '.';
        color = 0xf3dcb1;
      }
      put(a, cx + (u * co - vv * si) / 1.1,
          cy + (u * si + vv * co) * a->cw / a->ch, c, color);
    }
  }
}
static void mountain_base(Art *a) {
  double water = a->rows * .70;
  const uint32_t shades[] = {0x4e6879, 0x354f60, 0x233d4b, 0x142c37};
  for (int x = 0; x < a->cols; x++) {
    double ridges[4];
    for (int l = 0; l < 4; l++) {
      double n = (double)x / a->cols,
             p = fabs(sin(n * (7 + l * 1.8) + l * 1.9)) * .14 +
                 fabs(sin(n * (18 + l * 2) + l)) * .055;
      ridges[l] = a->rows * (.57 + l * .082 - p * (1 - l * .13));
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
            snow ? 0xa0b2b6 : shades[hit]);
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
            0x091c23);
    }
  }
}
static void sky_base(Art *a) {
  for (int i = 0; i < a->cols * 12; i++) {
    double x = hash(i, 10) * a->cols,
           y = a->rows * .17 + x / a->cols * a->rows * .26 +
               (hash(i, 11) - .5) * a->rows * .19,
           d = fabs(y - (a->rows * .17 + x / a->cols * a->rows * .26)) /
               (a->rows * .1);
    if (hash(i, 12) > d * .7)
      put(a, x, y, '.', hash(i, 13) > .8 ? 0x667c96 : 0x293e57);
  }
  for (int x = 0; x < a->cols; x++) {
    double n = (double)x / a->cols,
           top = a->rows * (.70 + sin(n * 7) * .055 + sin(n * 19) * .025 +
                            sin(n * 39) * .009);
    for (int y = (int)top; y < a->rows; y++)
      put(a, x, y, hash(x, y) > .96 ? '~' : '.',
          y > a->rows * .85 ? 0x0c192a : 0x14263a);
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
                color);
      }
    }
  for (int x = 0; x < a->cols; x++)
    for (int y = (int)(a->rows * .9); y < a->rows; y++)
      if (hash(x, y) > .65)
        put(a, x, y, hash(x, y) > .85 ? 'v' : '.', 0x304c33);
}
static void rain_base(Art *a) {
  for (int i = 0; i < 22; i++) {
    double x = hash(i, 50) * a->cols;
    if (x > a->cols * .20 && x < a->cols * .82)
      continue;
    double top = a->rows * (.1 + hash(i, 51) * .35), base = a->rows * .87;
    for (double y = top; y < base; y++) {
      put(a, x, y, ((int)y % 8) == 0 ? '=' : '|', 0x355549);
      if ((int)y % 7 == 0)
        for (int k = 1; k < 6; k++) {
          put(a, x - k, y - k * .3, '/', 0x4c6a51);
          put(a, x + k, y - k * .3, '\\', 0x4c6a51);
        }
    }
  }
  for (int y = (int)(a->rows * .72); y < a->rows; y++)
    for (int x = 0; x < a->cols; x++)
      if (hash(x, y) > .88)
        put(a, x, y, '-', 0x26454b);
}
static void dunes_base(Art *a) {
  double radius = fmin(a->cols * .04, a->rows * .10);
  for (double y = -radius; y <= radius; y++)
    for (double x = -radius * 1.65; x <= radius * 1.65; x++)
      if (x * x / (1.65 * 1.65) + y * y < radius * radius)
        put(a, a->cols * .74 + x, a->rows * .22 + y,
            hash(x, y) > .6 ? ':' : 'o', 0xc6a36a);
  for (int layer = 0; layer < 4; layer++)
    for (int x = 0; x < a->cols; x++) {
      double n = (double)x / a->cols,
             ridge = a->rows *
                     (.47 + layer * .13 + sin(n * 5.5 + layer * 2) * .065 +
                      sin(n * 12 + layer) * .015);
      const uint32_t colors[] = {0x4b4231, 0x706047, 0x8b7450, 0xad8f62};
      for (int y = (int)ridge; y < a->rows; y++) {
        double noise = hash(x, y + layer * 13);
        if (y - ridge < 1.2 || noise > .60)
          put(a, x, y,
              y - ridge < 1.2 ? '~'
              : noise > .88   ? ':'
                              : '.',
              colors[layer]);
        else
          put(a, x, y, ' ', colors[layer]);
      }
    }
  for (int i = 0; i < 9; i++) {
    double x = a->cols * (.06 + hash(i, 61) * .12), y = a->rows * .91;
    for (int k = 0; k < 3 + hash(i, 62) * 5; k++)
      put(a, x + k * .3, y - k, '/', 0xb0a16b);
  }
}
static void new_scene_frame(Art *a, double t) {
  if (a->scene == 3) {
    for (int i = 0; i < 32; i++) {
      double x = a->cols * (.08 + hash(i, 70) * .84) + sin(t * .18 + i) * 2;
      double y = a->rows * (.35 + hash(i, 71) * .48) + cos(t * .23 + i) * 1.4;
      double light = .5 + .5 * sin(t * .8 + i * 1.7);
      if (light > .45)
        put(a, x, y, light > .87 ? '*' : '.',
            light > .87 ? 0xc8c77f : 0x728451);
    }
    for (int i = 0; i < 45; i++)
      put(a, fmod(i * 7.3 + t * .5, a->cols),
          a->rows * .78 + sin(i * .4 + t * .15) * 2, '~', 0x2e473a);
  } else if (a->scene == 4) {
    for (int i = 0; i < 90; i++) {
      double y = fmod(hash(i, 73) * a->rows + t * (4 + hash(i, 74) * 3),
                      a->rows * .83);
      double x = hash(i, 72) * a->cols - y * .16;
      if (a->cols > 0) {
        x = fmod(x + a->cols, a->cols);
      }
      for (int k = 0; k < 2 + (i % 3); k++)
        put(a, x + k * .15, y - k, '/', i % 3 ? 0x355b64 : 0x618a8e);
    }
    for (int i = 0; i < 8; i++) {
      double r = fmod(t * 2 + i * 2.3, 13);
      ring(a, hash(i, 76) * a->cols, a->rows * (.76 + hash(i, 77) * .2), r,
           r < 7 ? 0x55777c : 0x2b484e, i);
    }
  } else {
    for (int i = 0; i < 65; i++) {
      double x = fmod(hash(i, 80) * a->cols + t * (.6 + hash(i, 81)), a->cols);
      double y = a->rows * (.48 + hash(i, 82) * .46) + sin(t * .2 + i) * .6;
      put(a, x, y, i % 7 == 0 ? '~' : '.', i % 3 ? 0x8d7851 : 0xc5ab76);
    }
  }
}
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
  if (scene == 0)
    pond_base(a);
  else if (scene == 3)
    forest_base(a);
  else if (scene == 4)
    rain_base(a);
  else if (scene == 5)
    dunes_base(a);
  else {
    moon(a);
    if (scene == 1)
      sky_base(a);
    else
      mountain_base(a);
  }
  memcpy(a->base, a->cells, bytes);
  a->cache = calloc(1, sizeof(*a->cache));
  if (!a->cache) {
    art_free(a);
    return -1;
  }
  if (scene == 1 || scene == 2) {
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
  }
  if (scene == 2) {
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
  if (a->scene >= 3) {
    new_scene_frame(a, t);
    return;
  }
  if (a->scene == 0) {
    for (int i = 0; i < 7; i++) {
      double p = t * .28 + i * 3.7, x = a->cols * (.5 + .27 * sin(p * .36 + i)),
             y = a->rows * (.46 + .24 * cos(p * .43 + i * 1.7)),
             dx = .27 * .36 * cos(p * .36 + i),
             dy = -.24 * .43 * sin(p * .43 + i * 1.7);
      fish(a, x, y, atan2(dy * a->ch, dx * a->cw), i, t);
    }
    for (int i = 0; i < 4; i++) {
      double r = fmod(t * 1.4 + i * 6, 22);
      int fade = (int)((1 - r / 22) * 12), g = 44 + (int)((1 - r / 22) * 17),
          b = 39 + (int)((1 - r / 22) * 11);
      ring(a, a->cols * (.37 + i * .12), a->rows * (.28 + (i % 3) * .15), r,
           (uint32_t)((20 + fade) << 16 | g << 8 | b), i);
    }
    return;
  }
  for (int i = 0; i < a->cache->star_count; i++) {
    const Star *star = &a->cache->stars[i];
    double light = .5 + .5 * sin(t * star->speed + star->phase);
    int g = 65 + (int)round(light * 12) * 8;
    a->cells[star->index] = (Cell){
        (uint32_t)((int)(g * .82) << 16 | (int)(g * .94) << 8 | (g + 15)),
        star->c};
  }
  double cycle = fmod(t + 8, 29);
  if (cycle < 1.6) {
    double sx = a->cols * (.18 + cycle * .15),
           sy = a->rows * (.12 + cycle * .07);
    for (int k = 0; k < 13; k++)
      put(a, sx - k, sy - k * .3, k == 0 ? '*' : '.',
          k < 3 ? 0xe0e5d6 : 0x40596c);
  }
  if (a->scene == 2) {
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
        double wave =
            waves[x * 3] * cy + waves[x * 3 + 1] * sy + waves[x * 3 + 2];
        unsigned char noise = a->cache->water_noise[index];
        if ((noise & 1) || wave > 1.55)
          a->cells[index] = (Cell){wave > 1.3   ? 0x47616a
                                   : depth < .2 ? 0x2b4551
                                                : 0x1b343e,
                                   wave > 1.3 ? '~' : '-'};
        if (fabs(x - a->cols * .76) < a->cols * .04 * (depth + .3) &&
            (noise & 2))
          a->cells[index] = (Cell){0x6b7976, '~'};
      }
    }
    for (int b = 0; b < 3; b++)
      for (int x = 0; x < a->cols; x++)
        if (hash(x, b) > .35)
          put(a, x,
              a->rows * (.64 + b * .045) + sin(x * .04 + t * .12 + b) * 1.5,
              hash(x, b) > .7 ? '~' : '.', 0x3b5159);
  }
}
static void rgb(cairo_t *cr, uint32_t color) {
  cairo_set_source_rgb(cr, ((color >> 16) & 255) / 255.,
                       ((color >> 8) & 255) / 255., (color & 255) / 255.);
}
void art_paint(Art *a, cairo_t *cr, Cell *previous) {
  uint32_t bg = art_bg(a);
  cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, a->cw + 1);
  for (int y = 0; y < a->rows; y++)
    for (int x = 0; x < a->cols; x++) {
      int i = y * a->cols + x;
      Cell c = a->cells[i];
      if (previous && c.c == previous[i].c &&
          (!c.c || c.color == previous[i].color))
        continue;
      double px = x * a->cw, py = floor(y * a->ch);
      rgb(cr, bg);
      cairo_rectangle(cr, px, py, a->cw, floor((y + 1) * a->ch) - py);
      cairo_fill(cr);
      if (c.c) {
        char text[2] = {c.c, 0};
        rgb(cr, art_color(a, c.color));
        cairo_move_to(cr, px, py + a->cw);
        cairo_show_text(cr, text);
      }
      if (previous)
        previous[i] = c;
    }
}

static uint32_t *tile(Art *a, Cell cell, int scale) {
  struct ArtCache *cache = a->cache;
  uint32_t key = ((uint32_t)cell.color << 8) | cell.c;
  unsigned int slot = (key * 2654435761u) >> (32 - 9);
  for (int tries = 0; tries < TILE_SLOTS;
       tries++, slot = (slot + 1) & (TILE_SLOTS - 1)) {
    Tile *entry = &cache->tiles[slot];
    if (entry->pixels) {
      if (entry->key == key)
        return entry->pixels;
      continue;
    }
    int width = cache->tile_width, height = cache->tile_height;
    uint32_t *pixels = malloc((size_t)width * height * 4);
    if (!pixels)
      return NULL;
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (unsigned char *)pixels, CAIRO_FORMAT_RGB24, width, height, width * 4);
    cairo_t *cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    rgb(cr, art_bg(a));
    cairo_paint(cr);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, a->cw + 1);
    rgb(cr, art_color(a, cell.color));
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
    entry->pixels = pixels;
    entry->key = key;
    return pixels;
  }
  return NULL;
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
  uint32_t background = 0xff000000u | art_bg(a);
  for (int y = 0; y < a->rows; y++) {
    int py = (int)floor(y * a->ch) * scale;
    int height = (int)floor((y + 1) * a->ch) * scale - py;
    if (py + height > a->height * scale)
      height = a->height * scale - py;
    for (int x = 0; x < a->cols; x++) {
      int index = y * a->cols + x;
      Cell cell = a->cells[index];
      if (previous && cell.c == previous[index].c &&
          (!cell.c || cell.color == previous[index].color))
        continue;
      int px = (int)(x * a->cw) * scale, width = cache->tile_width;
      if (px + width > a->width * scale)
        width = a->width * scale - px;
      uint32_t *source = cell.c ? tile(a, cell, scale) : NULL;
      if (cell.c && !source)
        return -1;
      for (int row = 0; row < height; row++) {
        uint32_t *destination = (uint32_t *)(pixels + (py + row) * stride) + px;
        if (source)
          memcpy(destination, source + row * cache->tile_width,
                 (size_t)width * 4);
        else
          for (int col = 0; col < width; col++)
            destination[col] = background;
      }
      if (previous)
        previous[index] = cell;
    }
  }
  return 0;
}

// Damage is relative to the last presented frame, not to the older shm buffer.
static int same_cell(Cell a, Cell b) {
  return a.c == b.c && (!a.c || a.color == b.color);
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
