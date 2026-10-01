# Tests

`render.cpp` drives the engine offline: it loads a card, runs a script of
key/knob/menu events in engine time, and writes the output WAV.
`surface_test.cpp` drives every Move control through the surface and checks
that the parameter it is labelled with moved; it prints the screen as ASCII.
See the README for the build lines.

Key ids are CHOMPI `Hardware::SwId` values (src/engine/munchi_tempo.h):
15 = C3, 18 = C4 (the sample's own pitch), 28 = C5, 33 = Play, 34 = Loop.
Scripts: `slot N`, `engine 0|1`, `rec 1|0`, `save N`, `copy A B`, `erase N`,
`snap 1|0`, `tap`, `freeze`, `switch 0|1` (0 = monitor), `hp 0|1`,
`src mic|line|resample`, `tone <hz>` (a sine into the input), `wait <ms>`
(real time, for the card worker).
