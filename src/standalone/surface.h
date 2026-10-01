/* surface.h -- Move's controls, played as a CHOMPI running TEMPO.
 *
 * Every Move control is turned into the CHOMPI key or encoder it stands for;
 * munchi_tempo.cpp (the firmware's own page logic) decides what that means.
 * This file owns the translation, the lights and the screen. In short:
 *
 *   pads          the 25-key keyboard, two octaves as piano rows (in Slice,
 *                 the white keys are the 16 slices and the black keys rests)
 *   Sample        the CHOMPI key in record mode: hold to record the buffer
 *   Shift+Sample  input: mic / line / resample
 *   Play / Loop   the pattern generator's keys (Rec is a second Loop);
 *                 Shift: arp style / rest pattern
 *   knobs 1-8     page 1: Speed, Start, End, Attack, Release, Level, Filter,
 *                 Lofi; page 2 (Right): Delay, Mix, Random, Feedback, Pan,
 *                 Tempo, Arp random, Compressor
 *   volume knob   Volume; Shift: input gain
 *   Shift+knob    the CHOMPI's shift turns that have no knob of their own:
 *                 stepped speed, move / halve-double the window, division
 *   Delete+touch  an encoder's shift-press (reset that page)
 *   Track 1-4     tap tempo, sample loop, sustain, input monitor
 *                 (Shift+Track 1: external clock; Shift+Track 4: routing)
 *   Mute          freeze the delay buffer
 *   steps 1-15    slots 1-14 and the buffer; step 16 snapshot A/B
 *                 hold Capture + step: save the buffer there
 *                 hold Copy + step, step: copy (the same step: commit)
 *                 hold Delete + step, same step again: erase
 *   Up / Down     Slice / Chroma engine; Shift: keyboard octave
 *   Menu / jog click: settings;  jog: slots;  Back x2: exit
 */
#pragma once
#include <stdint.h>
#include "display.h"
#include "../engine/munchi_tempo.h"

namespace munchi
{

class Surface
{
  public:
    explicit Surface(MunchiTempo &e);

    void HandleInternal(uint8_t status, uint8_t d1, uint8_t d2);
    void Tick(uint8_t *spi);
    void InvalidateLeds();
    bool WantsExit() const { return exit_; }
    /** Tests: draw the current screen and return its pixels (128 x 64). */
    const uint8_t *TestScreen() { Draw(); return disp_.Pixels(); }
    int  WriteAllOff(uint8_t *spi, int start);

    /* diagnostics: what Move's line-in detect CC said, for the log */
    int jack_cc_value_ = -1;
    int jack_cc_seen_  = 0;

  private:
    int  PadNote(int pad) const; // with the octave
    void OnPad(int pad, int vel, bool on);
    void OnStep(int step, bool on);
    void OnKnob(int knob, int delta);
    void OnKnobTouch(int knob, bool on);
    void OnButton(int cc, bool press);
    void Show(const char *name, const char *value);
    int   KnobParam(int knob, bool shift) const;
    void  ParamText(int param, char *value);
    float ParamValue(int param);
    void SettingsActivate(int dir);
    int  NextSlot(int from, int dir);
    void SlotName(int slot, char *out);

    void    ComputeLeds();
    uint8_t KeyColor(int pad);

    void Draw();
    void DrawMain();
    void DrawSample(int y0, int h);
    void DrawSettings();

    MunchiTempo &eng_;
    Display      disp_;

    bool shift_ = false, delete_ = false, copy_ = false, capture_ = false;
    int  copy_src_  = 0, copy_src_eng_ = 0;
    int  erase_armed_ = 0;
    int  page_      = 0;
    int  octave_    = 0;
    int  pad_note_[32];
    int  pad_hold_[32] = {0};

    char     show_name_[24] = {0}, show_value_[24] = {0};
    uint32_t show_t_ = 0;

    bool     settings_ = false;
    int      settings_cursor_ = 0;
    bool     exit_prompt_ = false;
    uint32_t exit_prompt_t_ = 0;
    bool     exit_ = false;

    /* the sample drawing: peaks, cached per sample */
    const void *peaks_src_ = nullptr;
    size_t      peaks_len_ = 0;
    uint8_t     peaks_[128];

    uint8_t  want_pad_[32], sent_pad_[32];
    uint8_t  want_step_[16], sent_step_[16];
    uint8_t  want_cc_[128], sent_cc_[128];
    bool     cc_used_[128] = {false};
    int      refresh_i_ = 0;
    uint32_t tick_ = 0;
};

} // namespace munchi
