# SpectraDrive

A harmonic distortion / saturation effect (VST3) for FL Studio 20+ and any other VST3 host.
It's built on the same stack as SpectraShell: JUCE 8, CMake, and a DSP engine that doesn't
depend on JUCE and has its own tests.

SpectraDrive pushes audio through one of five saturation curves, from warm tube to wavefolder.
Displays show the curve, where your signal sits on it, and which harmonics it is adding.

## How it works

```
in ─┬───────────────────────────────────────────── dry (delay-aligned) ──┐
    └─ crossover ─┬─ lows (kept clean, delay-aligned) ──────────────┐    │
                  └─ highs × drive + bias → oversample → shaper     ├────┴─ mix → output
                       → downsample → DC block → tone → auto gain ──┘
```

- **Even vs. odd harmonics.** The symmetric curves (Tape, Diode, Hard, Fold) produce odd
  harmonics only. The **Even** knob adds a DC bias before the shaper, which makes the curve
  asymmetric and adds even harmonics (2nd, 4th, …). The bias offset is subtracted back out
  and a DC blocker follows the shaper. Tube is asymmetric already, so it has even harmonics at 0 %.
- **Oversampling.** Linear-phase polyphase half-band FIR filters (Kaiser-windowed, about
  90 dB stopband). In the tests, 4x lowers aliasing from a hard-driven 7 kHz tone in Tape
  mode from −18 dB to −86 dB.
- **Clean Lows.** A Linkwitz-Riley crossover sends the lows around the shaper. With the
  drive at 0 the two bands sum back to flat. Bass stays tight, and the kick and sub don't
  turn into mush.
- **Auto Gain.** Tracks the loudness of the shaped band and matches it to the input, so
  you can compare Drive settings by tone rather than by volume. Both channels share one gain.
- **Latency.** The latency is a whole number of samples in every mode, and SpectraDrive
  reports it to the host, so FL Studio's delay compensation lines it up:
  1x = 0, 2x = 39, 4x = 45 samples.
- **Efficiency.** Coefficients are calculated once per 64-sample chunk, and parameters
  ramp smoothly within each chunk. Measured on x86-64 at 48 kHz stereo:
  **≈0.25 %** of one core at 1x, **≈0.5 %** at 2x, **≈0.65 %** at 4x.

## Controls

| Control | What it does |
|---|---|
| **Mode** | `Tube` asymmetric, warm, even + odd · `Tape` smooth soft saturation · `Diode` firmer knee, more bite · `Hard` hard clipper · `Fold` sine wavefolder: metallic, synth-like at high drive. |
| **Drive** | Gain into the shaper, 0 to +40 dB. |
| **Even** | Asymmetry: blends in even harmonics. |
| **Clean Lows** | Crossover frequency (`Off`, then 20–800 Hz). Frequencies below it skip the distortion. |
| **Tone** | Tilt EQ after the shaper, pivoting at 1 kHz: dark (−) ↔ bright (+), ±6 dB at the extremes. |
| **Auto Gain** | Keeps the output about as loud as the input. |
| **Mix** | Dry/wet balance for parallel saturation. The dry path is delay-aligned, so there's no comb filtering. |
| **Output** | Final gain, −24 to +12 dB. |
| **Quality** | `1x` zero latency · `2x` / `4x` oversampling, which reduces aliasing (use 4x on bright material or for mixdown). |

**Displays:** *Transfer* shows the curve, with dots at the current signal peak. *Harmonics*
shows the H1–H10 spectrum and THD for a −6 dBFS sine at the current settings: amber bars
are odd harmonics, pink bars are even. *Level* shows input/output peaks and the current
auto-gain amount.

## Getting the plugin

### Easiest: let GitHub build it
Every push to `main` runs `.github/workflows/build.yml`, which builds Windows and macOS
VST3s and runs the engine tests. Open the repo on GitHub → **Actions** → the latest
**Build** run → download **SpectraDrive-Windows-VST3**.

### Build it yourself
You need CMake ≥ 3.22, Git, and a C++17 compiler (Visual Studio 2022 or newer on Windows,
Xcode on macOS). JUCE is downloaded automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The plugin is created at `build/SpectraDrive_artefacts/Release/VST3/SpectraDrive.vst3`.

## Installing in FL Studio 20

1. Copy `SpectraDrive.vst3` to `C:\Program Files\Common Files\VST3\`
   (macOS: `/Library/Audio/Plug-Ins/VST3/`).
2. In FL Studio: **Options → Manage plugins → Find plugins**.
3. Load **SpectraDrive** on a mixer insert.

FL Studio picks up the latency SpectraDrive reports automatically. If you switch Quality
during playback, FL may need a moment, or a stop/start of playback, to re-align.

### Linux (Wine / Proton / Lutris)

Use the Windows VST3 and install it into the Wine prefix's
`drive_c/Program Files/Common Files/VST3/`. SpectraDrive includes the same Wine fixes as SpectraShell:

- Under Wine the window uses JUCE's software renderer, because Wine can't display
  Direct2D and the window would stay black. Real Windows still uses Direct2D.
- The UI uses its own embedded font (DejaVu Sans, `Resources/Fonts/`), so text still
  renders when the "Bitstream Vera Sans" font that JUCE asks for under Wine is missing.
- The JUCE 8.0.4 text-shaping crash (access violation reading `FFFFFFFFFFFFFFE9`) is patched.

The JUCE patches are applied automatically (`cmake/PatchJuceWine.cmake`).

## Tests

The DSP engine (`Source/DriveEngine.h`) does not depend on JUCE. The tests cover latency
accuracy, transparency at 0 dB drive, harmonic character per mode, aliasing per quality
setting, stability under extreme automation, auto-gain accuracy, Clean Lows and CPU cost:

```bash
cmake -S . -B build-test -DSPECTRADRIVE_PLUGIN=OFF -DSPECTRADRIVE_BUILD_TESTS=ON
cmake --build build-test && ./build-test/engine_test
```

## License note

SpectraDrive is built on [JUCE](https://juce.com) 8. JUCE is free for personal use and
small businesses under its Starter license, or available under AGPLv3. Check its terms
before you distribute the plugin.
