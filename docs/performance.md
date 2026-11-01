# Renderer performance

The optimized renderer uses cached colored glyph tiles, precomputed star positions and water data, four-byte character cells, and partial compositor damage. The frame rate and all three scenes are preserved.

Paired 1920×1080 benchmarks, median of seven runs per scene, 240 frames per run:

| Scene | First native renderer | Optimized renderer | Less drawing time |
|---|---:|---:|---:|
| Koi pond | 0.396 ms/frame | 0.145 ms/frame | 63.4% |
| Night sky | 0.464 ms/frame | 0.107 ms/frame | 76.9% |
| Mountains | 0.685 ms/frame | 0.147 ms/frame | 78.5% |

These measurements cover renderer drawing time, not total compositor CPU/GPU work. They are reference measurements from one Linux workstation, not a performance guarantee.

Across 2,400 sampled frames per scene, reported damage averaged 1.19–1.46% of a 1080p screen per frame. The first native implementation reported the entire screen as damaged. This is changed area sent to the compositor; compositor savings were not measured.

Thirty comparison PNGs across three scenes, two resolutions, and five animation timestamps were byte-identical to the first native renderer. Memory use remained broadly similar; the improvement mainly reduces drawing and screen update work.
