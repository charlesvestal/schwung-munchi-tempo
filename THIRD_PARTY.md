# Third-party work in Munchi Tempo

Munchi Tempo is a port of **CHOMPI TEMPO 1.0** to Ableton Move. Everything it is
built from is MIT-licensed; the notices below travel with every copy.

| Component | Copyright | License | Where |
|---|---|---|---|
| CHOMPI TEMPO 1.0 firmware (sample engines, pattern generator, clock, delay, UI logic) | © CHOMPI Club | MIT | `src/engine/tempo/` (originals of edited files in `src/engine/tempo/upstream/`) |
| CHOMPI TEMPO 1.0 factory card (14 chromatic + 14 slice samples, the boot buffer, presets.json, options.json) | © CHOMPI Club | MIT | fetched at build time into `card/` from [CHOMPI-Club/CHOMPI](https://github.com/CHOMPI-Club/CHOMPI) @ `a73d7326` |
| DaisySP (ADSR, SVF, DC block, delay line, sample-rate reducer, dsp utilities) | © Electrosmith, Corp. | MIT | `src/engine/daisysp/` (its `LICENSE` beside it) |
| `reverb.h`, `fx_engine.h`, `limiter.h` | © Émilie Gillet (Mutable Instruments) | MIT | `src/engine/tempo/` — original notices kept in the files |
| 5x7 font | Schwung standalone example | MIT | `src/standalone/font.cpp` |

Authorship of the firmware, per the CHOMPI repository's own `THIRD_PARTY.md`
(copied to `docs/CHOMPI-THIRD_PARTY.md`): Electrosmith engineered the original
platform; TAPE 2.0, TEMPO and WAVE were written at Chase Bliss after CHOMPI Club
became part of Chase Bliss. CHOMPI Club releases them under the MIT license.

## Trademarks

The CHOMPI name, logo, character and related marks are trademarks of CHOMPI
Club and are **not** covered by the MIT license (`docs/CHOMPI-TRADEMARKS.md`).
Munchi Tempo is an independent port, not an official CHOMPI Club release; it is
named differently for that reason and uses no CHOMPI artwork.
