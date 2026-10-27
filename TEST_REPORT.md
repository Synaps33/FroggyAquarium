# Test Report — FroggyAquarium (GB300 / SF2000)

Date: 2026-10-26
Goal: port the screensaver to a handheld without an FPU, 320x240, verifying
rendering, audio and input on the test runner before building the console core.

## Method

1. **Reference**: the original `djparentx/...` port (SDL2 + GLES2) built on the
   host and run in `--shot` mode at 320x240 (same projection — `MatProj` uses
   aspect 0.75 for both 320x240 and 640x480). Deterministic seed
   `AQUARIUM_SEED=12345`, fixed fish set (`assets/aquarium_test.ini`).
2. **Port**: `runner/host_runner` — same scene code + new software rasterizer,
   no SDL, same seed/config, 16 ms per frame.
3. **MIPS**: `runner/libretro_runner_mips` (musl, `-msoft-float`, static) under
   `qemu-mipsel`. Compared pixel by pixel with the host.
4. **libretro core**: `runner/libretro_runner` drives the core like the console
   frontend: start-up, D-Pad light changes, bubble toggle, audio, shutdown.

## Image fidelity

| Frame | Mean absolute error vs GLES2 (0–255) |
| --- | --- |
| 001 | 14.36 |
| 030 | 14.23 |
| 060 | 14.25 |
| 120 | 14.30 |

After removing sampling noise (Gaussian blur 2 px): **4.08**.
RGB565 quantization alone accounts for a baseline of **2.08**.

The remaining difference comes from:

* **nearest-neighbour** texture filtering (the GLES port uses `GL_LINEAR`),
* 4x4 dithering (deliberately ±4 per channel, hides 565 banding),
* affine UV mapping (PS1-style) instead of perspective-correct.

Geometry, texture orientation and UVs match the reference — confirmed visually
on close-ups (yellow tang, corals, background). See `screenshots/` and
`docs/screenshot.png`.

## MIPS / soft-float

* `objdump` on both console binaries (`dist/aquarium_SF2000.sf2k`,
  `dist/aquarium_GB300V2.gb3k`): **no FPU instructions** (`mul.s`, `cvt.s.w`,
  `mtc1`, ...). All floating point goes through libgcc soft-float.
* MIPS runner under qemu: output is pixel-identical to the host. The only
  exceptions are frames with the scrolling bubble texture, where the libm
  `cos/sin` rounding differs between glibc (host) and musl (MIPS) — a handful
  of pixels, invisible in practice.
* Audio: 72,765 frames for 99 video frames (735/frame at 44.1 kHz),
  126,530 non-zero samples.
* Exit: START issues `RETRO_ENVIRONMENT_SHUTDOWN` (`quit_seen=1`).

## Timings after the integer rewrite

Host x86 (`-O2`): **6.25 ms/frame** (400 frames).

`qemu-mipsel`, 40 frames, host under load (~2.3):

| Stage | ms/frame |
| --- | --- |
| scene simulation | 8.9 |
| TL background + textures/state | 4.3 |
| vertices (transform / lighting / projection) | 9.7 |
| raster (pixel loop) | 34.5 |
| **total** | **~57** |

The first version (soft-float triangle setup and vertex lighting) measured
**~395 ms/frame** in the same conditions: the integer pipeline is about
**7x faster**. The user measured ~10 fps on the GB300 with the old build; with
the same device/qemu ratio the new build should reach 30–60 fps (the frontend
caps at 60).

Remaining optimization ideas: cache normalized normals and static geometry,
16-bit textures to halve texture bandwidth, integer simulation loops.

## Functional tests (libretro runner)

| Test | Result |
| --- | --- |
| start-up and scene rendering | OK (`lr_02_scene`) |
| D-Pad RIGHT (brighter background) | OK, visible change |
| D-Pad DOWN (darker foreground) | OK |
| A (bubbles off/on) | OK (`lr_06`, `lr_07`) |
| START (exit) | OK |
| non-zero audio | OK |
| host vs MIPS reproducibility | OK (identical apart from bubble libm rounding) |

## Installation

The GB300 core was installed on the SD card used for testing
(`/system/Deimos/cores/aquarium.sf2k`, `ROMS/aquarium/` with the stub, ini,
original data and thumbnail). Launch: FrogUI → `ROMS/aquarium/Aquarium.gba`.
