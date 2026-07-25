# TIVTC — field matching and decimation for VapourSynth

A VapourSynth port of tritical's TIVTC. **TFM** is a field matching filter and **TDecimate** is a
decimation filter; used together they perform an inverse telecine (IVTC), and either can be used on
its own.

**Notes**
* Native VapourSynth API 4 plugin. Namespace `tivtc`, identifier `com.nodame.tivtc`.
* Supports 8-16 bit integer planar YUV in 4:2:0, 4:2:2 and 4:4:4. Width and height must be even,
  and each plane needs at least 8 lines.
* All processing is plain C++. The original hand-written MMX/SSE2 paths and the YUY2 code have been
  removed; the `opt` parameter is accepted for script compatibility but does nothing.
* Communication between TFM and TDecimate uses frame properties, not the pixel-embedded hints the
  AviSynth version used.
* `debug=True` writes to the VapourSynth log (`mtInformation`), so it appears wherever your
  frontend surfaces log messages.

**Note on `std.Cache`:** none is inserted. API 4 adds and sizes caches automatically, so the
explicit `std.Cache` call the API 3 version made is neither needed nor allowed.

## Table of Contents
* [Installation](#installation)
* [Quick start](#quick-start)
* [Function Documentation](#function-documentation)
  * [TFM](#tfm)
    * [TFM match codes](#tfm-match-codes)
    * [TFM overrides file](#tfm-overrides-file)
  * [TDecimate](#tdecimate)
    * [TDecimate modes](#tdecimate-modes)
    * [TDecimate overrides file](#tdecimate-overrides-file)
* [Frame properties](#frame-properties)
* [Building](#building)
* [References](#references)

## Installation

Drop the built library into a directory VapourSynth autoloads, or load it explicitly:

```py
core.std.LoadPlugin("/path/to/libtivtc.so")   # or tivtc.dll
```

## Quick start

The common case — 29.97fps hard-telecined NTSC film back to 23.976fps:

```py
clip = core.tivtc.TFM(clip)
clip = core.tivtc.TDecimate(clip)
```

Two-pass, which is slower but makes better decisions because TDecimate can see the whole clip's
metrics before deciding what to drop:

```py
# pass 1 - write out matches and metrics, output is discarded
core.tivtc.TDecimate(core.tivtc.TFM(clip, output="matches.txt"),
                     mode=4, output="metrics.txt").set_output()

# pass 2 - reuse them
clip = core.tivtc.TFM(clip, input="matches.txt")
clip = core.tivtc.TDecimate(clip, mode=1, input="metrics.txt", tfmIn="matches.txt")
```

## Function Documentation

### TFM

Field matching. For each frame TFM builds candidate frames by weaving the current field with a
field from the previous, current or next frame, and keeps whichever combination is not combed.
Frames that still comb after matching can be post-processed with `PP`.

```py
core.tivtc.TFM(clip clip[, int order = -1, int field = -1, int mode = 1, int PP = 6,
               string ovr = "", string input = "", string output = "", string outputC = "",
               int debug = 0, int display = 0, int slow = 1, int mChroma = 1, int cNum = 15,
               int cthresh = 9, int MI = 80, int chroma = 0, int blockx = 16, int blocky = 16,
               int y0 = 0, int y1 = 0, int mthresh = 5, clip clip2 = None, string d2v = "",
               int ovrDefault = 0, int flags = 4, float scthresh = 12.0, int micout = 0,
               int micmatching = 1, string trimIn = "", int hint = 1, int metric = 0,
               int batch = 0, int ubsco = 1, int mmsco = 1, int opt = 4])
```

| Parameter | Type | Options (Default) | Description |
| --- | --- | --- | --- |
| clip | 8-16 bit integer YUV 4:2:0/4:2:2/4:4:4 | | Clip to process |
| order | int | -1, 0, 1 (-1) | Field order. -1 reads the `_FieldBased` frame property and errors if it is absent, 0 is bottom field first, 1 is top field first. |
| field | int | -1, 0, 1 (-1) | Which field to match from. -1 means the same as `order`. Changing it can improve matching on some sources. |
| mode | int | 0-7 (1) | Which matches to try. 0 = p/c. 1 = p/c, plus n if combed. 2 = p/c, plus u. 3 = p/c, plus n, plus u/b. 4 = p/c/n. 5 = p/c/n, plus u/b. 6 = p/c, plus u, n, b. 7 = no search; picks the field based on the previous frame's result. |
| PP | int | 0-7 (6) | What to do with frames that are still combed. 0 = nothing. 1 = only mark them. 2/3/4 = deinterlace the whole frame by blend / cubic / modified ELA. 5/6/7 = the same three, but motion-adaptive so only moving areas are touched. |
| slow | int | 0-2 (1) | Matching accuracy. 0 is fastest and least accurate, 2 is slowest and most accurate. |
| mChroma | int | 0, 1 (1) | Use chroma when comparing matches. Turn off for sources with rainbowing or other chroma artifacts. |
| cthresh | int | -1 to 255 (9) | Combing detection threshold. 8-12 is the useful range; lower detects more. -1 marks everything combed. |
| MI | int | 0 to blockx*blocky (80) | How many combed pixels must fall inside one `blockx` x `blocky` block before the frame counts as combed. |
| chroma | int | 0, 1 (0) | Include chroma in combing detection. Usually best left off. |
| blockx | int | 4-2048, power of two (16) | Combing detection block width. |
| blocky | int | 4-2048, power of two (16) | Combing detection block height. |
| metric | int | 0, 1 (0) | Spatial combing metric. 0 is the original five-pixel vertical test, 1 is Donald Graft's FieldDeinterlace metric. |
| y0, y1 | int | (0, 0) | Ignore scan lines `y0` through `y1` when matching, for burned-in subtitles or logos. Set them equal to disable. |
| mthresh | int | (5) | Motion threshold for the motion-adaptive `PP` modes (5-7). Pixels differing by less than this count as static and are left alone. |
| micmatching | int | 0-4 (1) | Use combing counts (mics) to second-guess the match. 0 = off. 1 = switch to a clear outlier. 2 = only consider matches the mode would have tried. 3 = both. 4 = post-check only. |
| micout | int | 0-2 (0) | Force mic calculation for matches that would otherwise be skipped. 1 = p/c/n, 2 = p/c/n/b/u. Needed for meaningful `output` mic columns. |
| ubsco | int | 0, 1 (1) | Only allow u/b matches at scene changes. |
| mmsco | int | 0, 1 (1) | Only apply `micmatching` at scene changes. |
| scthresh | float | 0.0-100.0 (12.0) | Scene change threshold, as a percentage of the maximum possible luma change. Used by `ubsco`, `mmsco` and `flags=5`. |
| clip2 | clip | (None) | Take post-processed frames from this clip instead of deinterlacing internally. Must match the input's format, dimensions and length. |
| ovr | string | ("") | Overrides file. See [TFM overrides file](#tfm-overrides-file). |
| ovrDefault | int | 0-2 (0) | What to assume for frames the overrides file does not mention. 0 = decide normally, 1 = treat as clean, 2 = treat as combed. |
| input | string | ("") | Read matches from a file previously written with `output` instead of searching. |
| output | string | ("") | Write the chosen match, combed flag and mic values for every frame. |
| outputC | string | ("") | Write only the frame ranges that matched 'c' at least `cNum` times in a row, which is a decent progressive-section detector. |
| cNum | int | (15) | How many consecutive 'c' matches a range needs before `outputC` records it. |
| trimIn | string | ("") | Frame ranges that were trimmed away before TFM, either `"start,end"` inline or a path to a file of such lines. Only meaningful with `d2v`. Negative indices count from the end. |
| d2v | string | ("") | A DGIndex/DVD2AVI project file. TFM validates its field transitions, can take the field order from it, and can use its repeat-field flags to guide matching. |
| flags | int | 0-5 (4) | How the `d2v` data is used. 0 = check and pass on. 1 = trust the repeat flags for film sections. 2 = trust them everywhere. 3 = check only. 4 = check and validate against combing. 5 = as 4, but only at scene changes. |
| hint | int | 0, 1 (1) | Write the match, combed status and d2v film flag into frame properties for TDecimate to read. |
| batch | int | 0, 1 (0) | Skip the CRC check that ties an `input` file to the clip it was made from. |
| display | int | 0, 1 (0) | Overlay the match, mode, field, order, mics and combed status on each frame. |
| debug | int | 0, 1 (0) | Log the same information to the VapourSynth log instead of drawing it. |
| opt | int | 0-4 (4) | Accepted and ignored. There are no SIMD paths left to select. |

#### TFM match codes

A match names one field of one frame, relative to the field order in force.

| Code | Meaning |
| --- | --- |
| p | field from the previous frame, opposite parity |
| c | field from the current frame, opposite parity |
| n | field from the next frame, opposite parity |
| b | field from the previous frame, same parity |
| u | field from the next frame, same parity |

`l` and `h` also appear in files written by `output`: they mean the frame was left combed and
deinterlaced with the bottom or top field respectively.

#### TFM overrides file

Blank lines and lines starting with `#` or `;` are ignored. An optional first line
`field = top` or `field = bottom` states which field order the file's match codes were recorded
under, so a file made under one order still applies correctly under the other.

```
# force a match on one frame, and on a range
7 c
12,19 p

# repeat a pattern across a range
0,100 pcpcbuu

# force frames combed (+) or clean (-)
25 +
60,80 -

# change a parameter for a frame or range: f field, o order, m mode, P PP, i MI
30 f 1
35,40 m 2
```

Raising `PP` above 1 from an overrides file has no effect if the filter was created with `PP<=1`,
because no post-processing filter was added to the graph; selecting mode 7 from an overrides file
when the filter was not created with `mode=7` makes the result depend on frame request order.
TFM logs a warning for both.

### TDecimate

Decimation. In its usual mode it examines each cycle of `cycle` frames and drops the `cycleR` that
are most redundant, turning 29.97fps telecined video back into 23.976fps.

```py
core.tivtc.TDecimate(clip clip[, int mode = 0, int cycleR = 1, int cycle = 5,
                     float rate = 23.976, float dupThresh, float vidThresh,
                     float sceneThresh = 15.0, int hybrid = 0, int vidDetect = 3,
                     int conCycle, int conCycleTP, string ovr = "", string output = "",
                     string input = "", string tfmIn = "", string mkvOut = "", int nt = 0,
                     int blockx = 32, int blocky = 32, int debug = 0, int display = 0,
                     int vfrDec = 1, int batch = 0, int tcfv1 = 1, int se = 0,
                     int chroma = 1, int exPP = 0, int maxndl = -200, int m2PA = 0,
                     int denoise = 0, int noblend = 1, int ssd = 0, int hint = 1,
                     clip clip2 = None, int sdlim = 0, int opt = 4, string orgOut = ""])
```

| Parameter | Type | Options (Default) | Description |
| --- | --- | --- | --- |
| clip | 8-16 bit integer YUV | | Clip to process |
| mode | int | 0-7 (0) | Which decimation algorithm to use. See [TDecimate modes](#tdecimate-modes). |
| cycle | int | 2 to clip length (5) | The N in "drop M of every N frames". |
| cycleR | int | 1 to cycle-1 (1) | The M in "drop M of every N frames". Must be 1 when `hybrid` is non-zero. |
| rate | float | (23.976) | Target frame rate for modes 2 and 7. Must be lower than the input rate. |
| dupThresh | float | (1.1 chroma / 1.4 no chroma; 0.4 / 0.5 in mode 7) | Below this metric a frame counts as a duplicate, as a percentage of the maximum block change. |
| vidThresh | float | (1.1 chroma / 1.4 no chroma; 3.5 / 4.0 in mode 7) | Metric above which a cycle looks like real video rather than telecined film. Used when `hybrid` is non-zero. |
| sceneThresh | float | 0.0-100.0 (15.0) | Scene change threshold as a percentage of the maximum luma change. |
| hybrid | int | 0-3 (0) | How to handle sections that are genuinely video rather than film. 0 = nothing. 1 = blend 30p down into 24p. 2 = variable frame rate, for modes 3 and 5. 3 = blend 24p up into 30p. |
| vidDetect | int | 0-4 (3) | What evidence marks a cycle as video. 0 = field matches. 1 = metrics. 2 = either. 3 = both. 4 = matches, and either metrics or a local minimum. |
| conCycle | int | 1, 2 (1 if vidDetect>=3 else 2) | How many consecutive video-looking cycles are needed before a section is treated as video. |
| conCycleTP | int | (1 if vidDetect>=3 else 2) | As `conCycle`, but for mode 5. Not capped at 2. |
| vfrDec | int | 0, 1 (1) | How film sections are decimated in modes 3-5. 0 = drop the most similar frame, 1 = drop from the longest run of duplicates. |
| sdlim | int | (0) | Minimum spacing between dropped frames when `cycleR` > 1. Negative values let the limit relax when it cannot be met. |
| noblend | int | 0, 1 (1) | Do not replace a duplicate with a blend of its neighbours when two duplicates are found in a cycle. |
| nt | int | (0) | Noise threshold. Pixel differences at or below this are treated as zero when computing metrics. 1-2 helps on noisy sources. |
| blockx | int | 4-2048, power of two (32) | Metric block width. Larger blocks are less noise-sensitive but miss small movement. |
| blocky | int | 4-2048, power of two (32) | Metric block height. |
| chroma | int | 0, 1 (1) | Include chroma in the metrics. Turning it off is faster. |
| ssd | int | 0, 1 (0) | Use sum of squared differences rather than sum of absolute differences. SAD is usually the better choice here. |
| denoise | int | 0, 1 (0) | Denoise frames before measuring them. Slower, but steadies duplicate detection on grainy or dot-crawling sources. |
| maxndl | int | (-200) | Mode 2 only. Maximum non-duplicate run length, which trades sync accuracy for smoother spacing when duplicates are unevenly distributed. |
| m2PA | int | 0, 1 (0) | Mode 2 only. Removes the 100-frame lookahead cap to match two-pass accuracy. Can stall for minutes at a cycle boundary. |
| ovr | string | ("") | Overrides file. See [TDecimate overrides file](#tdecimate-overrides-file). Not supported in mode 7. |
| input | string | ("") | Metrics file from a previous `mode=4` pass, so metrics are not recomputed. Required by modes 5 and 6. |
| output | string | ("") | Write per-frame metrics to a file. |
| tfmIn | string | ("") | A file written by TFM's `output`, giving matches and combed flags. Required by mode 5. |
| mkvOut | string | ("") | Write an MKV timecodes file. Modes 3, 5 and 6. |
| tcfv1 | int | 0, 1 (1) | Timecode file format. 1 writes v1 (frame rate ranges), 0 writes v2 (one timestamp per frame). |
| orgOut | string | ("") | Mode 5 only. Log the original frame number behind each output frame, so a bob deinterlacer can follow the decimated timeline. |
| exPP | int | 0, 1 (0) | Set when using `tfmIn` from a TFM run with `PP=1` and deinterlacing separately, so TDecimate knows the marked frames were handled. |
| se | int | 0, 1 (0) | Mode 3 only. Raise an error once the padding frames begin, as a signal that real output has ended. Do not use for batch encoding. |
| clip2 | clip | (None) | Return frames from this clip while measuring and deciding on the input clip. Must have the same length. |
| hint | int | 0, 1 (1) | Read TFM's frame properties. |
| batch | int | 0, 1 (0) | Skip file consistency checks. |
| display | int | 0, 1 (0) | Overlay the cycle metrics and the decimation decision on each frame. |
| debug | int | 0, 1 (0) | Log the same information to the VapourSynth log. |
| opt | int | 0-4 (4) | Accepted and ignored. |

#### TDecimate modes

| Mode | Description |
| --- | --- |
| 0 | Drop the `cycleR` most similar frames from each cycle of `cycle`. The general-purpose mode. |
| 1 | Drop from the longest run of duplicates instead, using `dupThresh`. Better for animation and anything with genuinely repeated frames. |
| 2 | Reach an arbitrary `rate` using a different algorithm. Improves markedly with a two-pass `input` metrics file. `hybrid` is not allowed. |
| 3 | Single-pass variable frame rate with an MKV timecodes file. Needs `hybrid=2` and `cycleR=1`. The frame count cannot change, so the output is padded with black frames. |
| 4 | No decimation. Only measures the clip and writes metrics, which is the first pass for mode 5. |
| 5 | Two-pass variable frame rate. Needs both `input` and `tfmIn`. Unlike mode 3 it seeks correctly and produces the right frame count. Uses `conCycleTP`. |
| 6 | 120fps to variable frame rate, dropping only bit-identical frames, targeting 119.880, 59.940, 39.960, 29.970 or 23.976fps. Needs a mode 4 metrics file. |
| 7 | Arbitrary `rate` driven by `dupThresh` and `vidThresh`. Handles unevenly distributed duplicates better than mode 2. No overrides file. |

#### TDecimate overrides file

Blank lines and lines starting with `#` or `;` are ignored.

```
# force a frame to be dropped (-) or kept (+)
5 -
10 +

# declare a range film (f) or video (v)
20,29 f
30,39 v
```

## Frame properties

TFM sets these on every output frame when `hint=True` or `PP>=2`:

| Property | Type | Description |
| --- | --- | --- |
| `TFMMatch` | int | The match code that was used, 0-4 for p/c/n/b/u |
| `TFMField` | int | The field that was matched from |
| `TFMMics` | int[] | Combing counts for p/c/n/b/u; -20 where not measured |
| `TFMPP` | int | The `PP` value in force for this frame |
| `TFMD2VFilm` | int | Whether the d2v data marks this frame as a film duplicate |
| `_Combed` | int | Whether the frame is still combed after matching |

TDecimate sets these:

| Property | Type | Description |
| --- | --- | --- |
| `TDecimateOriginalFrame` | int | Which source frame this output frame came from. Set on every frame. |
| `TDecimateCycleStart` | int | First source frame of the cycle. Only on the first output frame of each cycle. |
| `TDecimateCycleMaxBlockDiff` | int[] | The cycle's per-frame metrics. Only on the first output frame of each cycle. |
| `_DurationNum`, `_DurationDen` | int | Per-frame duration, for the variable frame rate modes. |

Both filters write their `display=True` overlay text into `TFMDisplay` / `TDecimateDisplay`, which
the plugin renders through `text.Text`.

## Building

Requires meson >= 1.2.3 and a C++20 compiler.

```sh
meson setup build
meson compile -C build
```

The headers are located by asking the VapourSynth Python module for them:

```py
import vapoursynth as vs; print(vs.get_include())
```

so the only requirement is that the `vapoursynth` module is importable by the Python meson picks
up — no pkg-config and no separate SDK install. If you have several Python installations, point
meson at the right one with `meson setup build -Dpython.name=/path/to/python`.

The result is `libtivtc.so` on Linux and macOS, `tivtc.dll` on Windows.

## References

This is a port, and it would not exist without the work behind it.

* tritical's original TIVTC for AviSynth: http://bengal.missouri.edu/~kes25c/
* pinterf's maintained AviSynth fork, and the documentation this README draws on:
  https://github.com/pinterf/TIVTC
* Filter descriptions on the AviSynth wiki: http://avisynth.nl/index.php/TIVTC
* dubhater's VapourSynth port, which this repository continues:
  https://github.com/dubhater/vapoursynth-tivtc
