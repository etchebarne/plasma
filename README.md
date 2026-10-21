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

- CMake build with a CLAP target.
- Host parameter fixes:
  - a text lookup that went out of bounds (crashed on parameter display);
  - text → value parsing now inverts the display scaling and normalizes;
  - the host is told to rescan values after a state load;
  - corrupt host state no longer opens a blocking modal dialog.
- Rebranded as Plasma: new logo, name and data directory.

## License

GPLv3. See `LICENSE`.
