# Munchi Tempo — Manual

Munchi Tempo turns Ableton Move into a **CHOMPI running TEMPO**: a groovebox with
two sample engines that play at the same time, a pattern generator for each, and
a clock-synced delay that turns into a diffusion reverb. It runs the actual CHOMPI
TEMPO 1.0 firmware code, so it behaves like the hardware. Where this manual uses
a name in capitals (CHROMA, SLICE, the Magic Wand, snapshots), it is the name
Chase Bliss's own TEMPO guidebook uses.

1. [What it is](#1-what-it-is)
2. [Starting and stopping](#2-starting-and-stopping)
3. [A first session](#3-a-first-session)
4. [The pads](#4-the-pads)
5. [The two engines](#5-the-two-engines)
6. [Recording](#6-recording)
7. [The sound](#7-the-sound)
8. [Effects](#8-effects)
9. [The pattern generator](#9-the-pattern-generator)
10. [Tempo and clock](#10-tempo-and-clock)
11. [Slots: save, copy, erase](#11-slots-save-copy-erase)
12. [Snapshots A and B](#12-snapshots-a-and-b)
13. [Input and monitoring](#13-input-and-monitoring)
14. [The card](#14-the-card)
15. [Settings](#15-settings)
16. [MIDI](#16-midi)
17. [The screen and the lights](#17-the-screen-and-the-lights)
18. [Control reference](#18-control-reference)
19. [Differences from a real CHOMPI](#19-differences-from-a-real-chompi)

---

## 1. What it is

- **CHROMA engine.** One sample played chromatically across the keyboard, eight
  voices.
- **SLICE engine.** One sample cut into 16 equal slices, one per white key. The
  black keys are rests.
- Both engines run **in parallel**, each with its own **pattern generator**
  (an arpeggiator that latches into a simple sequencer), its own clock division
  and its own MIDI channel.
- **Effects:** a clock-synced dual delay (left of centre) that blends into a
  diffusion reverb (right of centre), with randomness, feedback, a freeze, and a
  send level per engine. Then an output compressor that turns into saturation.
- **14 slots per engine** plus the **buffer**, the sample you just recorded.
- **Snapshots A and B:** two complete states of everything, switched instantly.

## 2. Starting and stopping

Open Schwung's Tools menu (**Shift + Volume + Step 13**) and choose **Munchi
Tempo**. Move stops and Munchi Tempo takes over the whole device. The first launch
copies the factory card (28 samples, about 30 MB) into place, so the screen says
LOADING SAMPLES for a few seconds. For the first second and a half after that the
pads are ignored, as on the hardware.

**Booting straight into it.** Munchi Tempo is also a boot target: choose it in the
Schwung web manager's **Boot** page (`http://move.local:7700/boot`) and Move
starts in Munchi Tempo. Leaving it then starts Schwung as usual.

To leave, press **Back**, then **Back** again within three seconds. Move
restarts. The buffer, the patterns and the snapshots are lost on exit (as when
the CHOMPI is switched off); saved slots and settings are kept.

## 3. A first session

1. **Play.** Press the pads. You start on the **buffer**, holding the factory
   boot sample, in the CHROMA engine.
2. **Pick a sample.** Press **step buttons 1–14**, or turn the **jog wheel**.
   Step 15 is the buffer.
3. **Make a pattern.** Press **Play** (the pattern generator runs) and **Loop**
   (latch). Tap a few pads: they light red and play in the order you entered
   them, in time. Tap a lit pad again to take it out. **Loop** again clears the
   pattern; **Play** stops it.
4. **Change the speed.** Press **Right** for knob page 2; **knob 6 is Tempo**.
   Or tap **Track 1** twice or more: tap tempo.
5. **Add space.** On page 2, **knob 1 (Delay)**: left of centre is a clocked
   echo, right of centre the echo melts into reverb. Press **Mute** to freeze it.
6. **Add a beat.** Press **Up** for the SLICE engine and pick **step 8** (an
   808 kit). Press **Loop**, then tap: white pads are slices, black pads are
   rests. Press **Play**. Both engines now run together.
7. **Record your own.** Hold **Sample**, make a sound, let go. It is in the
   buffer and plays straight away (the mic is recorded but only heard on
   headphones; see [13](#13-input-and-monitoring)).
8. **Keep it.** Hold **Capture** and press a step 1–14: the buffer is saved into
   that slot.

## 4. The pads

The pads are the CHOMPI's two-octave keyboard, laid out as piano rows, the same
as in Munchi Tape and Munchi Wave:

```
row 4   .  C#4 D#4  .  F#4 G#4 A#4  .
row 3  C4  D4  E4  F4  G4  A4  B4  C5
row 2   .  C#3 D#3  .  F#3 G#3 A#3  .
row 1  C3  D3  E3  F3  G3  A3  B3  C4
```

In CHROMA, **C4 plays the sample at its own pitch**; the keyboard spans an
octave either side. **Shift + Down** moves the keyboard down an octave (twice,
to two octaves down, for bass); **Shift + Up** moves it back. The pads light
teal while shifted.

In SLICE, the fifteen white keys are slices 1–15. The last one, **C5, is the
combo slice**: it alternates between slices 15 and 16 each time you press it,
and in a pattern it has four states (15, 16, alternating, off). The black keys
make no sound; in a pattern each one is a rest.

## 5. The two engines

**Up** selects SLICE, **Down** selects CHROMA. Knobs, slots and the pattern keys
always act on the engine you are looking at; the other one keeps playing its
pattern in the background. Each engine remembers its own slot and settings.

## 6. Recording

Hold **Sample** to record into the **buffer**; let go to stop (with Settings →
**Record latch** on, tap once to start and again to stop). The new sample is
loaded into the engine you are looking at and its knobs reset, ready to play.
The buffer holds 10 seconds; recording stops by itself when it is full.

**Shift + Sample** chooses what is recorded: **mic**, **line** in, or
**resample** (Munchi Tempo's own output). The buffer is temporary: save it to a
slot to keep it.

## 7. The sound

Knob page 1 (**Left**) is the sample. These settings are stored per slot.

| Knob | Name | What it does |
|---|---|---|
| 1 | **Speed** | Playback speed and direction. Centre is stopped, right is forwards up to 2×, left is reverse. **Shift**: in musical steps of fifths and octaves |
| 2 | **Start** | Where the sample starts. **Shift**: move the whole window, keeping its length |
| 3 | **End** | Where it ends. **Shift**: each step halves (left) or doubles (right) the window |
| 4 | **Attack** | Fade-in time |
| 5 | **Release** | Fade-out time |
| 6 | **Level** | The sample's volume |
| 7 | **Filter** | Centre is open; left is low-pass, right is high-pass, with resonance near the edges of the centre |
| 8 | **Lofi** | Sample-rate reduction, before the filter |

**Track 2** switches **sample loop** (the sample repeats from Start once it
reaches End); **Track 3** switches **sustain** (off: a note plays only its
envelope however long the key is held, good for short percussive patterns).

**Pan** is knob 5 on page 2.

**Try this: a sample as an oscillator.** With loop on, close **Start** and
**End** right together (Shift + End halves the window quickly). A tiny window
looping is a waveform: play it chromatically, then filter and crush it.

## 8. Effects

Knob page 2 (**Right**):

| Knob | Name | What it does |
|---|---|---|
| 1 | **Delay** | The Magic Wand. Centre is off. Left: a clock-synced echo, its interval getting shorter the further you go. Right: the same echo increasingly blended with diffusion reverb |
| 2 | **FX Mix** | How much of the current engine goes to the effects, from dry to fully wet. Each engine has its own |
| 3 | **FX Random** | Probability of random events in the delay: octave up/down and reverse on the left side; octave up and random panning on the right |
| 4 | **Feedback** | Repeats. Past about 60 %, new material ducks the delay line |
| 5 | **Pan** | |
| 6 | **Tempo** | See [10](#10-tempo-and-clock) |
| 7 | **Arp Random** | Probability of octave jumps and shuffled order in the patterns |
| 8 | **Comp** | Output compressor; past half, saturation blends in |

**Mute** freezes the delay buffer: what is in it keeps looping, and you can still
move the Delay knob through the intervals. Mute again releases it. (The delay
must be on to freeze.)

The **volume knob** is the master volume; **Shift + volume** is input gain.

**Delete + touch a knob** resets that control's page, as Shift + press on the
CHOMPI's encoder does: Delete + Speed resets the speed, Delete + Delay resets the
delay and its randomness, and so on.

## 9. The pattern generator

- **Play** starts and stops the pattern generator. Keys you hold are played on
  the clock grid; let go and they stop.
- **Loop** latches: keys you press join the pattern and stay. Press a latched
  key again to take it out. **Loop** again clears the pattern.
- Press **Loop** first and then **Play** to build a pattern step by step before
  it runs.
- **Shift + Play**: arp style — note order, up, down, ping-pong, random.
- **Shift + Loop**: rest patterns — none, or one of four rhythms of notes and
  rests (shown as dots and dashes on the screen).
- **Hold Play** (over a second) while a pattern runs: **freestyle** — the engine
  you are looking at leaves the pattern and the keys play freely, while the
  other engine keeps going. Press Play to rejoin.
- **Hold Loop** (over a second) while stopped: **key sustain** — any note you
  play keeps sounding. Loop lights orange. Tap Loop to end it.

Patterns are per engine: build a CHROMA line, switch to SLICE (**Up**) and build
a beat; both run together.

## 10. Tempo and clock

- **Tempo** (page 2, knob 6): 80–240 BPM, 1 BPM per detent.
- **Track 1**: tap tempo (two or more taps). It flashes on the beat.
- **Shift + Tempo knob**: clock division for the current engine (÷2, ÷1.5, ×1,
  ×1.5, ×2) — run the engines at different rates.
- **Shift + Track 1**: external MIDI clock on/off. With it on, Munchi Tempo
  follows clock from a device on Move's USB-A port, Track 1 flashes purple, and
  the Tempo knob sets the ratio to the incoming clock.

## 11. Slots: save, copy, erase

The step buttons are the slots of the engine you are looking at. Lit steps hold a
sample; the current one is white; step 15 (pink) is the buffer.

| To | Do this |
|---|---|
| Choose a slot | Press its step (or turn the jog) |
| **Save** the buffer into a slot | Hold **Capture**, press a step 1–14 |
| **Copy** a slot | Hold **Copy**, press the source step, then the destination |
| **Commit** your knob changes to a slot | Hold **Copy**, press the slot's step twice |
| **Erase** a slot | Hold **Delete**, press the step, press it again |

A slot keeps both the sample and its sound settings (speed, start, end, attack,
release, level, filter, pan, lofi, loop, sustain). Choosing a slot always brings
back those saved settings, so tweaking freely is safe: to keep a tweak, copy the
slot onto itself.

To move a sample between engines, start a copy, switch engine with **Up/Down**
while still holding Copy, and press the destination.

## 12. Snapshots A and B

**Step 16** is the snapshot key. A snapshot is every setting of both engines,
their patterns and the effects. You start in **A**; it always tracks what you do.

- **Tap step 16**: switch between A and B instantly.
- **Hold step 16** for a second: copy the current snapshot over the other one.

Copy A to B, change B, then flip between them like two decks, or keep A as a safe
copy while you experiment. Snapshots last until you leave Munchi Tempo.

## 13. Input and monitoring

**Shift + Sample** (or Settings → **Input**) chooses the source: **mic**, **line**
in, or **resample**.

**Track 4** switches **input monitoring** (the CHOMPI's mode switch: up records
and monitors). Lit white: you hear the input. **The mic is only ever heard on
headphones.** Move's mic sits beside its speaker, so monitoring it there would
feed back; on the speaker Track 4 lights orange, and the mic is still recorded,
just not heard. Line in and resampling are heard on either.

**Shift + Track 4** cycles the **routing** (the CHOMPI's routing position):

- **Dry**: the input is heard as it comes in.
- **Thru FX**: the input goes through the engines' effects path.
- **Send/Ret**: the mic goes into the effects while monitoring; line in is heard
  directly.

## 14. The card

The card lives in
`/data/UserData/UserLibrary/Samples/Schwung/Munchi Tempo/` (on a computer:
Move's **Samples / Schwung / Munchi Tempo**, or through the Schwung web manager's
Files page):

```
chromatic/chroma_a1.wav … chroma_a14.wav   CHROMA slots 1-14
slice/slice_a1.wav … slice_a14.wav         SLICE slots 1-14
buffer/buffer.wav                          the sample loaded into the buffer at start
presets.json                               each slot's sound settings
options.json                               the settings
```

Any WAV works: 16/24/32-bit or float, mono or stereo, any sample rate. It is
converted to 48 kHz stereo on loading and cut at 10 seconds. For SLICE, lay 16
sounds out evenly in one file (16 equal grid spots in a DAW, exported as one
file).

The factory card is copied in on the first launch and never over a file that is
already there. Delete `.munchi-card` from the folder to have missing factory
files copied back next time.

## 15. Settings

**Menu** (or a jog click) opens Settings. Turn the jog to move, click to change
(Shift + click changes backwards), **Back** or Menu to close.

| Setting | |
|---|---|
| **Input** | Mic / line / resample |
| **Routing** | Dry / thru FX / send-return (see 13) |
| **Record latch** | Off: hold Sample to record. On: tap to start, tap to stop |
| **Shift speed** | Stepped: Shift + Speed moves in fifths and octaves. Free: fine |
| **MIDI in ch** | |
| **Chroma out ch / Slice out ch** | Each engine sends its notes on its own channel |
| **Clock out** | Send MIDI clock while the pattern generator runs |
| **CC in / CC out** | |
| **Start/Stop** | In + out / out only / in only: MIDI start and stop messages |
| **Unfreeze mute** | Mute the delay briefly when unfreezing |
| **Exit** | |

## 16. MIDI

MIDI goes in and out through Move's **USB-A** port.

- **Notes in** (MIDI in channel): notes 24–72, played on the engine you are
  looking at. 48–72 are the keyboard (48 is its low C, 60 the sample's own
  pitch); 24–47 reach the two octaves below.
- **Notes out**: what you play and what the patterns play, on the engine's
  channel.
- **Clock**: out while playing (internal clock); in with external clock on.
- **CC in**: 20–25 the six CHOMPI encoders on their current page (Speed, Start,
  End, Delay, Tempo, Volume); 14 the record key; 15 Loop.
- **CC out**: every knob as its CHOMPI page sends it (20–31), 14 record, 15 Loop.

## 17. The screen and the lights

The top line shows the engine, slot (BUF is the buffer) and snapshot, and on the
right the tempo and clock division (EXT with external clock). Below it, the
current sample with its window marked (SLICE adds tick marks for the slices) — or
a REC bar while recording. Then the knob page, or the value of the knob you are
touching. The bottom line is the pattern state, arp style and rest pattern.

| Light | Meaning |
|---|---|
| Pads | White: playing. Red (CHROMA) / yellow (SLICE): in the pattern. Teal: keyboard shifted an octave |
| Steps 1–14 | White: current slot. Purple (CHROMA) / orange (SLICE): holds a sample |
| Step 15 | Pink: the buffer holds a sample |
| Step 16 | Green: snapshot A. Blue: snapshot B |
| Play | Flashing: running. Dim: the other engine is running |
| Loop | Red / yellow: latched. Orange: key sustain |
| Sample | Red: recording |
| Mute | Blinking: frozen |
| Track 1 | Flashes on the beat (purple: external clock) |
| Track 2 / 3 | Loop / sustain on |
| Track 4 | White: monitoring. Orange: monitoring the mic on the speaker (muted) |
| Up / Down | The current engine |

## 18. Control reference

| Control | Does | With Shift |
|---|---|---|
| Pads | The keyboard | |
| Sample | Record the buffer | Input: mic / line / resample |
| Play | Pattern generator on/off (hold: freestyle) | Arp style |
| Loop (Rec) | Latch (hold while stopped: key sustain) | Rest pattern |
| Knobs, page 1 | Speed, Start, End, Attack, Release, Level, Filter, Lofi | Speed in steps; move window; halve/double window |
| Knobs, page 2 | Delay, FX Mix, FX Random, Feedback, Pan, Tempo, Arp Random, Comp | Tempo knob: clock division |
| Volume knob | Volume | Input gain |
| Left / Right | Knob page | |
| Up / Down | SLICE / CHROMA | Keyboard octave |
| Track 1 | Tap tempo | External clock |
| Track 2 / 3 | Sample loop / sustain | |
| Track 4 | Input monitoring | Routing |
| Mute | Freeze the delay | |
| Steps 1–15 | Slots, buffer | |
| Step 16 | Snapshot A/B (hold: copy) | |
| Capture + step | Save the buffer to the slot | |
| Copy + step, step | Copy a slot (same step twice: commit) | |
| Delete + step, step | Erase a slot | |
| Delete + touch a knob | Reset that page | |
| Jog | Slots | |
| Menu / jog click | Settings | |
| Back, Back | Exit | |

## 19. Differences from a real CHOMPI

- **No Shift + key menu.** The CHOMPI reaches engines, inputs, snapshots and
  erase / copy / save through its keyboard while the CHOMPI key is held; Move has
  dedicated buttons for all of them (Up/Down, Shift + Sample, step 16, Capture /
  Copy / Delete + step), so the pads always play.
- **Every encoder page has its own knob.** No page-cycling by pressing an
  encoder; Shift only adds the CHOMPI's shift turns that have no knob of their
  own.
- **The mic is only heard on headphones** (see 13).
- **Fixes:** a full buffer now loads the take, as letting go of the key does; a
  copy made in SLICE lands in a SLICE slot (the firmware wrote it to CHROMA); a
  recalled snapshot whose slot was erased falls back to the buffer; the routing
  setting in options.json is read back. Out-of-range reads the hardware got away
  with are guarded.
- **Audio:** the engines run at their native 48 kHz, resampled to Move's 44.1 kHz.
- **Tempo knob:** 1 BPM per detent (the CHOMPI's is half that).
