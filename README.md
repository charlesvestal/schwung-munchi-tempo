# Munchi Tempo

**CHOMPI TEMPO 1.0 on Ableton Move.** A groovebox: a chromatic sample engine and
a slice engine playing side by side, a pattern generator for each, a clock-synced
delay that blends into diffusion reverb, and A/B snapshots. It runs the actual
TEMPO firmware code.

Munchi Tempo is a [Schwung](https://github.com/charlesvestal/schwung) standalone
tool. Launched from Schwung's Tools menu, it stops Move and runs the whole device
itself (pads, knobs, buttons, lights, screen, audio). Back twice hands Move back.
Its siblings are [Munchi Tape](https://github.com/charlesvestal/schwung-munchi-tape)
(the CHOMPI's sampler and looper) and
[Munchi Wave](https://github.com/charlesvestal/schwung-munchi-wave) (its
wavetable synth).

It is a port of the firmware CHOMPI Club released as open source
([CHOMPI-Club/CHOMPI](https://github.com/CHOMPI-Club/CHOMPI), MIT). It is not an
official CHOMPI Club release; see [Credits](#credits).

**→ [The manual](docs/MANUAL.md)**: a first session, both engines, the pattern
generator, effects, slots and snapshots, every control.

## At a glance

- **CHROMA**: one sample across the keyboard, 8 voices, two octaves below on
  Shift + Down
- **SLICE**: one sample cut into 16 slices on the white keys; black keys are rests
- A pattern generator per engine: arp styles, rest patterns, latch, freestyle,
  key sustain, randomness, clock division; tap tempo, MIDI clock in and out
- Clock-synced delay / diffusion reverb with randomness, feedback, freeze and a
  send per engine; output compressor and saturation
- Record from the mic, line in or Munchi Tempo itself; 14 slots per engine;
  snapshots A and B
- The sample folders, `presets.json` and `options.json` are in TEMPO's own
  formats, so they move to and from a CHOMPI's SD card

## Quick start

| Move | Does |
|---|---|
| Pads | the keyboard, two octaves as piano rows |
| Up / Down | SLICE / CHROMA engine |
| Steps 1–15, jog | slots (15 = the buffer) |
| Step 16 | snapshot A/B (hold: copy) |
| Knobs, page 1 | Speed, Start, End, Attack, Release, Level, Filter, Lofi |
| Knobs, page 2 (Right) | Delay, FX Mix, FX Random, Feedback, Pan, Tempo, Arp Random, Comp |
| Volume knob | Volume (Shift: input gain) |
| Play / Loop | pattern generator / latch (Shift: arp style / rests) |
| Sample | record (Shift: mic / line / resample) |
| Capture / Copy / Delete + step | save the buffer / copy / erase a slot |
| Track 1–4 | tap tempo, sample loop, sustain, input monitoring |
| Mute | freeze the delay |
| Menu | settings |
| Back ×2 | exit |

The mic is only heard on headphones: Move's mic sits next to its speaker.

## Install

From a release: download `munchi-tempo-module.tar.gz` from the
[latest release](https://github.com/charlesvestal/schwung-munchi-tempo/releases/latest/download/munchi-tempo-module.tar.gz)
and install it with the Schwung web manager's custom-module upload, or paste
this repository's URL into its custom install. From a build:

```bash
./scripts/build.sh      # fetches the factory card, cross-compiles in Docker
./scripts/install.sh    # copies dist/munchi-tempo to the Move
```

Then Tools menu (Shift + Volume + Step 13) → **Munchi Tempo**, or pick it as the
boot target on the web manager's Boot page.

## How it is built

| Path | What |
|---|---|
| `src/engine/tempo/` | TEMPO's engines, pattern generator, clock, delay, effects and snapshots, from the firmware. Edited files keep their originals in `tempo/upstream/`; `tempo/shim/` stands in for libDaisy and the CHOMPI board |
| `src/engine/munchi_tempo.*` | the firmware's page logic (`NormalPage`, `MenuPage`, `ui.h`, `MidiManager`'s input), presets, options, the clock timer |
| `src/engine/tempo_samples.cpp` | the sample card: loading (any WAV format, converted to 48 kHz stereo) and a worker for every write |
| `src/engine/resampler.h` | the engine runs at its native 48 kHz; this converts to and from Move's 44.1 kHz |
| `src/standalone/` | the SPI loop, the Move control mapping, lights and screen |
| `scripts/fetch-card.sh` | fetches the factory card from the CHOMPI repo at a pinned commit |
| `tests/` | `render.cpp` (scripted events in, a WAV out) and `surface_test.cpp` (every Move control through the surface, with the screen as ASCII) |

```bash
./scripts/fetch-card.sh build/card
FLAGS="-std=c++17 -O2 -Wno-vla -Isrc -Isrc/engine -Isrc/engine/tempo/shim -Isrc/engine/tempo"
ENGINE="src/engine/munchi_tempo.cpp src/engine/tempo_samples.cpp src/engine/daisysp/*.cpp"
c++ $FLAGS tests/render.cpp $ENGINE -o build-host/render
c++ $FLAGS tests/surface_test.cpp src/standalone/surface.cpp src/standalone/font.cpp $ENGINE -o build-host/surface_test
MUNCHI_CARD=/tmp/scratch-card ./build-host/render build/card tests/scripts/pattern.txt out.wav
./build-host/surface_test build/card /tmp/scratch-card2
```

## Credits

- **CHOMPI TEMPO 1.0:** CHOMPI Club / Chase Bliss. MIT. The CHOMPI name, logo and
  character are CHOMPI Club's trademarks and are not licensed; this port is named
  Munchi Tempo for that reason.
- **DaisySP:** Electrosmith. MIT.
- **Reverb, FX engine, limiter:** Émilie Gillet, Mutable Instruments. MIT.

See [THIRD_PARTY.md](THIRD_PARTY.md). Munchi Tempo itself is MIT ([LICENSE](LICENSE)).
