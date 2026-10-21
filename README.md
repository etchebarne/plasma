# Plasma

A spectral-warping wavetable synthesizer in the style of Serum, packaged as a **CLAP** plugin (plus VST3) for Bitwig and other hosts.

Plasma is built on the GPLv3 source of [Vital](https://github.com/mtytel/vital) by Matt Tytel. Per that project's terms, this build does not use the Vital name or branding, does not talk to any vital.audio service, and ships none of Vital's factory presets.

## Building (Linux)

Requirements: a C++17 compiler, CMake ≥ 3.15, Ninja, and the dev headers for freetype2, X11 (Xext, Xinerama, Xrandr, Xcursor) and OpenGL. ALSA, JACK and libcurl are not needed.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Outputs are in `build/Plasma_artefacts/Release/`:

- `CLAP/Plasma.clap`
- `VST3/Plasma.vst3`
- `Standalone/Plasma`: UI only, with no audio backend compiled in. It's useful for checking the interface.

## Installing

```bash
cp build/Plasma_artefacts/Release/CLAP/Plasma.clap ~/.clap/
```

In Bitwig, rescan plugins (Settings → Plug-ins). Plasma shows up as an instrument from "Plasma".

User data (presets, wavetables, LFO shapes, skins) is stored in `~/.local/share/plasma/` (or `$XDG_DATA_HOME/plasma`). Vital-format files (`.vital`, `.vitaltable`, `.vitalbank`, `.vitallfo`) load as-is. To use a Vital library you already own, copy or symlink its folders in there.

## Layout

- `src/`: synth engine, UI and plugin code (from Vital, with Plasma changes)
- `third_party/JUCE`: the CMake tooling from JUCE 6.0.5, with Vital's patched JUCE modules
- `third_party/clap-juce-extensions`: CLAP wrapper (MIT)
- `CMakeLists.txt`: the build; the Projucer `.jucer` files and old Makefiles are kept for reference only

## Changes from upstream Vital

- **Hyper/Dimension effect** (Serum-style, last slot in the FX rack by default; drag it to reorder):
  - **Hyper**: 2–7 unison voices built from modulated short delays, spread across the stereo field. Controls: Unison, Rate, Detune, Mix. Detune is a pitch-deviation amount, so it sounds the same at any rate.
  - **Dimension**: adds decorrelated side content from two short delays of the mid signal. It widens the sound and cancels in mono, so mono playback is unaffected. Controls: Size, Mix.

  Every Hyper/Dimension knob can be modulated. Presets made without Hyper load with their effect order unchanged and Hyper off.
- **Sub oscillator** (Voice tab, SUB section beside SMP):
  - Waveforms: Sine, Triangle, Saw, Square, 25% Pulse and Rounded. The hard edges are antialiased with PolyBLEP.
  - Pitch: transpose (default −12 semitones) and fine tune.
  - Level, Pan, and routing to Filter 1, Filter 2, both, Effects or Direct Out.
  - The phase restarts on every note so the low end hits the same each time. All controls can be modulated.
- **Arpeggiator** (Advanced tab → ARPEGGIATOR):
  - Modes: Up, Down, Up/Down, Down/Up, As Played, Random, Chord.
  - Rate: 1/1 to 1/64, including dotted and triplet values.
  - Octaves 1–4, plus Gate, Swing and Latch.
  - While the host is playing, steps lock to its beat grid; otherwise the arp starts on the first key press. All settings save with the preset and can be automated from the host.
- CMake build with a CLAP target.
- Host parameter fixes:
  - a text lookup that went out of bounds (crashed on parameter display);
  - text → value parsing now inverts the display scaling and normalizes;
  - the host is told to rescan values after a state load;
  - corrupt host state no longer opens a blocking modal dialog.
- Rebranded as Plasma: new logo, name and data directory.

## Tests

```bash
cmake --build build --target plasma_hyper_test plasma_arp_test plasma_sub_test
./build/plasma_hyper_test && ./build/plasma_arp_test && ./build/plasma_sub_test
```

- `plasma_hyper_test` renders a note with Hyper/Dimension off and on. It checks that the output stays finite, that each stage widens the stereo image, and that pre-Hyper presets keep their effect order.
- `plasma_sub_test` plays the sub alone and checks its pitch, each waveform's harmonic ratios, panning, and save/reload.
- `plasma_arp_test` checks the arp's patterns and sample-exact timing (gate, swing, latch, host sync, pass-through) and plays it through the plugin.

`tools/ui_screenshot.py` clicks through the standalone UI on a private X server and saves a screenshot (usage is in the file).

## License

GPLv3. See `LICENSE`.
