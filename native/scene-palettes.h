// Scene palettes are explicit artwork choices, not colour remaps. The pond,
// sky and forest are night pieces; the rain garden and dunes are drawn for
// daylight. Each palette starts with the five vertical backdrop stops used by
// art_background(): y = 0, .26, .46, .70 and 1.
#ifndef STILLWATER_SCENE_PALETTES_H
#define STILLWATER_SCENE_PALETTES_H
#include <stdint.h>

// Mountains and forest keep their original Midnight artwork and material
// remapping table.
typedef struct {
  uint32_t bg[5];
  uint32_t texture, distant, middle, foreground;
  uint32_t accent, highlight, foliage, shimmer;
} ScenePalette;

typedef struct {
  uint32_t bg[5];
  uint32_t star_dim, star, star_warm;
  uint32_t moon, moon_shade, moon_halo;
  uint32_t far_ridge, tree_ridge, rim, mist;
  uint32_t water_glint, water_dim, ripple;
  uint32_t bank, moss, grass, reed;
  uint32_t pad, pad_deep, pad_rim, vein;
  uint32_t petal[4], stamen;
  uint32_t stone[4];
  uint32_t lantern_stone, lantern_light, lantern_warm, lantern_glow;
  uint32_t koi[3][4]; // body, patch, fin edge
  uint32_t eye;
} PondPalette;

typedef struct {
  uint32_t bg[5];
  uint32_t star_dim, star, star_bright, star_warm;
  uint32_t dust, dust_dim;
  uint32_t moon, moon_shade, moon_halo;
  uint32_t cloud, cloud_lit, island;
  uint32_t sea_rim, wave, wave_lit, glint;
  uint32_t boat, sail, boat_light, boat_reflection;
  uint32_t meteor, meteor_tail;
  uint32_t cliff, cliff_lit, pine, constellation;
} SkyPalette;

typedef struct {
  uint32_t bg[5];
  uint32_t far[3], bamboo[3], leaf[2], node;
  uint32_t wall, wall_shade, cap, cap_shade, moss, moss_deep;
  uint32_t stone[4], path[3], wet, puddle[3], ripple, splash;
  uint32_t bloom[5], bloom_center, leaf_deep;
  uint32_t maple, maple_light;
  uint32_t rain, rain_dim, eave[2], drip, bird;
} RainPalette;

typedef struct {
  uint32_t bg[5];
  uint32_t sun, sun_inner, sun_halo, cloud;
  uint32_t mesa[2], haze;
  uint32_t dune[4][5]; // lightest to deepest, far to near
  uint32_t rim_lit, rim_shade;
  uint32_t ripple_dark, ripple_light, sand_glint;
  uint32_t grass[2], camel[2], blanket, tree[2], pebble[2], bird;
} DunesPalette;

static const ScenePalette scene_palettes[ART_SCENES] = {
    {.bg = {0}, .texture = 0}, // Pond uses its own night palette.
    {.bg = {0}, .texture = 0}, // Sky uses its own night palette.
    {{0x0a1520, 0x0a1520, 0x0a1520, 0x0a1520, 0x0a1520}, 0x4e6879,
     0x354f60, 0x233d4b, 0x142c37, 0xd1d5b4, 0xa0b2b6, 0x091c23, 0x47616a},
    {{0x0c231e, 0x0c231e, 0x0c231e, 0x0c231e, 0x0c231e}, 0x2e473a,
     0x1b352c, 0x2a4a39, 0x41634a, 0xcbd099, 0xe4d393, 0x547144, 0x728451},
    {.bg = {0}, .texture = 0}, // Rain uses its own daylight palette.
    {.bg = {0}, .texture = 0}, // Dunes uses its own daylight palette.
};

// Night: a still garden pond with moon glitter, lily pads, koi and a stone
// lantern. Cool indigo-black above, deep green water below.
static const PondPalette pond_colors = {
    .bg = {0x04060e, 0x071321, 0x0a1c22, 0x0a1d1b, 0x061110},
    .star_dim = 0x4d6072,
    .star = 0xdfe7e4,
    .star_warm = 0xf1dcae,
    .moon = 0xf4e5c0,
    .moon_shade = 0xe0cfa6,
    .moon_halo = 0x16222c,
    .far_ridge = 0x07131a,
    .tree_ridge = 0x061017,
    .rim = 0x16292a,
    .mist = 0x0d1c22,
    .water_glint = 0xd6cda2,
    .water_dim = 0x1d3a33,
    .ripple = 0x2f4f41,
    .bank = 0x0c1f1a,
    .moss = 0x14291f,
    .grass = 0x24402c,
    .reed = 0x0b1b17,
    .pad = 0x23482c,
    .pad_deep = 0x16301d,
    .pad_rim = 0x4f7a44,
    .vein = 0x0e2214,
    .petal = {0xf6ebe4, 0xf0c8cc, 0xe3a3b1, 0xfbf4e4},
    .stamen = 0xeac36f,
    .stone = {0x101917, 0x19251f, 0x26362e, 0x3a4a3d},
    .lantern_stone = 0x2d332c,
    .lantern_light = 0xf2bd76,
    .lantern_warm = 0xb97f45,
    .lantern_glow = 0x4b3a28,
    .koi = {{0xd29455, 0xf3d8b0, 0xa05a32, 0x5f3222},
            {0xe9e1d0, 0xc8503c, 0xd9ccb7, 0x7c5c4d},
            {0xbc8445, 0x6f4326, 0x946331, 0x4e2c1c}},
    .eye = 0x10100d,
};

// Night: an open sea horizon under a dense star field, with a crescent moon,
// a distant island and a single lantern-lit sailboat.
static const SkyPalette sky_colors = {
    .bg = {0x02030a, 0x060b1a, 0x0b152c, 0x12213a, 0x0a1526},
    .star_dim = 0x3f4d6a,
    .star = 0xa8b4cf,
    .star_bright = 0xf2f4ff,
    .star_warm = 0xffe6b0,
    .dust = 0x1a2745,
    .dust_dim = 0x101a30,
    .moon = 0xf2edd9,
    .moon_shade = 0xc9c2a9,
    .moon_halo = 0x25324a,
    .cloud = 0x24324d,
    .cloud_lit = 0x4a5a76,
    .island = 0x050a13,
    .sea_rim = 0x1b2e46,
    .wave = 0x1b3048,
    .wave_lit = 0x2c4863,
    .glint = 0xd9d2ac,
    .boat = 0x080d16,
    .sail = 0x2b3747,
    .boat_light = 0xffcb85,
    .boat_reflection = 0x6e5a39,
    .meteor = 0xf5f1df,
    .meteor_tail = 0x7b86a3,
    .cliff = 0x070d16,
    .cliff_lit = 0x1c2a3c,
    .pine = 0x060b12,
    .constellation = 0x2b3a58,
};

// Daylight: a rain-soaked garden in pale papery mist. Bamboo, a plaster wall,
// a stone basin, hydrangeas and puddles, all under a soft rain.
static const RainPalette rain_colors = {
    .bg = {0xe4eef2, 0xe4ece0, 0xdde8d9, 0xd5e2d0, 0xcedcca},
    .far = {0xd3e0cf, 0xc1d2bc, 0xaec3a9},
    .bamboo = {0x80976b, 0x5d7757, 0x3f5b41},
    .leaf = {0x6b875e, 0x4c6a49},
    .node = 0x3c5340,
    .wall = 0xf0e7cd,
    .wall_shade = 0xd9cfb3,
    .cap = 0x879584,
    .cap_shade = 0x647264,
    .moss = 0x719a4c,
    .moss_deep = 0x537437,
    .stone = {0xb3b7a7, 0x989e8b, 0x7c8473, 0x626b5c},
    .path = {0xb9b5a1, 0xa19e8a, 0x7d7c6c},
    .wet = 0xaebbb0,
    .puddle = {0x82a9a3, 0x6f9691, 0x5d827e},
    .ripple = 0x7d9c96,
    .splash = 0xf4f8f3,
    .bloom = {0x7f9fd6, 0x9dbbe8, 0xb29ed2, 0xd7a8b8, 0xf2f4ea},
    .maple = 0xc2604c,
    .maple_light = 0xe08a63,
    .bloom_center = 0xf4f0d2,
    .leaf_deep = 0x4c6a49,
    .rain = 0xf8fbf7,
    .rain_dim = 0xb9ccca,
    .eave = {0x6d5a45, 0x4c3d2f},
    .drip = 0xd3e0da,
    .bird = 0x4e4f47,
};

// Daylight: high sun, layered ochre dunes, a slow camel caravan and dry
// grasses. Warm cream highlights against cool violet shadows.
static const DunesPalette dunes_colors = {
    .bg = {0x7fb2d2, 0xa9cbd8, 0xe4ddbe, 0xf3dcab, 0xefcf99},
    .sun = 0xffdf9e,
    .sun_inner = 0xfff0c4,
    .sun_halo = 0xf8d79a,
    .cloud = 0xf4ead2,
    .mesa = {0xd2b79a, 0xb28d6c},
    .haze = 0xe8d6b4,
    .dune = {{0xf0d9a8, 0xdcbd85, 0xc1965e, 0x9d7546, 0x7c5a36},
             {0xf4e0b0, 0xe0c089, 0xc69a62, 0xa37a48, 0x805c37},
             {0xf7e5b8, 0xe6c88f, 0xcda065, 0xaa7f4a, 0x855f38},
             {0xfbeec6, 0xecd39b, 0xd3a76a, 0xb07e46, 0x8a6038}},
    .rim_lit = 0xfff3cc,
    .rim_shade = 0x8f6a3e,
    .ripple_dark = 0xbb8d55,
    .ripple_light = 0xf2d9a3,
    .sand_glint = 0xfff1c9,
    .grass = {0x9c8e57, 0x7d7047},
    .camel = {0x6f4c31, 0x4b3220},
    .blanket = 0xb85b3d,
    .tree = {0x8a764c, 0x645337},
    .pebble = {0xdcc79e, 0xb89a70},
    .bird = 0x7b6047,
};

#endif
