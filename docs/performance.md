# Renderer performance

Stillwater draws into an eight-byte character cell: a 24-bit ground colour, a
24-bit ink colour and a glyph. Grounds let scenes paint solid planes for dunes,
water, walls and foliage; inks draw texture and detail on top. Tiles are
rasterised once per (ground, ink, glyph, scale) combination and blitted into
shared memory. Static scenery, sprite stamps and the vertical backdrop gradient
are part of the cached base image, so a frame only repaints cells that moved.

Shading uses ordered dithering inside a narrow seam between neighbouring tones.
Large faces therefore stay flat and readable instead of turning into speckled
gradients, while curved ridges and foliage still shade smoothly.

Current reference measurements on one Linux workstation, 1920×1080, 240 frames
per run, median of five runs:

| Landscape | Appearance | Drawing time | Est. one-core use at 12 fps |
|---|---:|---:|---:|
| Koi pond | Night | 0.571 ms/frame | 0.69% |
| Night sky | Night | 0.523 ms/frame | 0.63% |
| Mountain lake | Night | 0.127 ms/frame | 0.15% |
| Firefly forest | Night | 0.076 ms/frame | 0.09% |
| Rain garden | Day | 0.150 ms/frame | 0.18% |
| Quiet dunes | Day | 0.091 ms/frame | 0.11% |

The pond and sky do more work because koi, stars and water are painted
every frame; mountains, forest and the rain garden keep most scenery in the
cached base layer. Peak resident memory
in the render path measured 20–22 MB at 1080p, including the Cairo surface used
by `--render` and `--benchmark`.

These numbers cover renderer drawing time, not total compositor CPU/GPU work.
They are reference measurements from one workstation, not a performance
guarantee. Hardware, output count, resolution, scaling and compositor policy all
matter.

The renderer still reports damage only for cells that changed since the last
presented frame. The Wayland fixture and renderer tests rebuild the image from
those partial updates and alternating shared-memory buffers, and compare it
byte-for-byte with a complete redraw at three sizes and both integer scales.
That is the property to preserve when changing a scene: animated detail should
touch a small number of cells, and static scenery belongs in the base layer.
