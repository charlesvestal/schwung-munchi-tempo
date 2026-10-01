#include "surface.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace munchi
{

/* ---- Move control numbers ---------------------------------------------- */
enum
{
    CC_JOG_CLICK = 3,
    CC_JOG       = 14,
    CC_TRACK4    = 40, // reversed: CC 43 is track 1
    CC_TRACK1    = 43,
    CC_SHIFT     = 49,
    CC_MENU      = 50,
    CC_BACK      = 51,
    CC_CAPTURE   = 52,
    CC_DOWN      = 54,
    CC_UP        = 55,
    CC_LOOP      = 58,
    CC_COPY      = 60,
    CC_LEFT      = 62,
    CC_RIGHT     = 63,
    CC_KNOB1     = 71,
    CC_VOLUME    = 79,
    CC_PLAY      = 85,
    CC_REC       = 86,
    CC_MUTE      = 88,
    CC_LINE_IN   = 114, // XMOS jack detect: logged, not obeyed (see Tape)
    CC_LINE_OUT  = 115, // XMOS jack detect: 0 = speakers, 127 = headphones
    CC_SAMPLE    = 118,
    CC_DELETE    = 119,
};

/* ---- palette (schwung src/shared/constants.mjs) --------------------------- */
enum
{
    C_OFF        = 0,
    C_WHITE      = 120,
    C_GREY       = 118,
    C_GREY_DIM   = 123,
    C_GREY_DARK  = 124,
    C_RED        = 127,
    C_GREEN      = 126,
    C_BLUE       = 125,
    C_YELLOW     = 7,
    C_ORANGE     = 3,
    C_PINK       = 25,
    C_PINK_DIM   = 113,
    C_PURPLE     = 23,
    C_PURPLE_DIM = 109,
    C_TEAL       = 15,
    C_TEAL_DIM   = 93,
    C_AZURE      = 16,
};

static const int kNumSettings = 13;

/* the keyboard on the pads: the same layout as Munchi Tape and Wave */
static const int kPadNote[32] = {
    48, 50, 52, 53, 55, 57, 59, 60, //
    -1, 49, 51, -1, 54, 56, 58, -1, //
    60, 62, 64, 65, 67, 69, 71, 72, //
    -1, 61, 63, -1, 66, 68, 70, -1, //
};

static bool is_black(int note)
{
    int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

/* clockManager: freeDivs 24,18,12,8,6 ticks; syncDivs 48,24,12,6,3 clocks */
static const char *kFreeDivNames[5] = {"/2", "/1.5", "X1", "X1.5", "X2"};
static const char *kSyncDivNames[5] = {"/4", "/2", "X1", "X2", "X4"};
static const char *kArpNames[5]     = {"ORDER", "UP", "DOWN", "PING-PONG", "RANDOM"};
static const char *kRestNames[5]    = {"NO RESTS", "3 + REST", "2 + 2 RESTS", "NOTE-REST", "NOTE-REST-2"};
static const char *kRestDots[5]     = {"....", "...-", "..--", ".-.-", ".-..-"};
static const char *kMonNames[3]     = {"DRY", "THRU FX", "SEND/RET"};
static const char *kSrcNames[3]     = {"MIC", "LINE", "RESAMPLE"};
static const char *kDelayDivs[9]    = {"1/8", "1/6", "1/4", "1/3", "3/8", "1/2", "3/4", "1", "2"};

Surface::Surface(MunchiTempo &e) : eng_(e)
{
    InvalidateLeds();
    for(int i = 0; i < 32; i++)
        pad_note_[i] = -1;
}

void Surface::InvalidateLeds()
{
    memset(sent_pad_, 0xFF, sizeof(sent_pad_));
    memset(sent_step_, 0xFF, sizeof(sent_step_));
    memset(sent_cc_, 0xFF, sizeof(sent_cc_));
}

int Surface::PadNote(int pad) const
{
    if(pad < 0 || pad >= 32 || kPadNote[pad] < 0)
        return -1;
    // the octave only means something to the chromatic engine
    int oct = eng_.CurEngine() == CHROMATIC ? octave_ : 0;
    return kPadNote[pad] + 12 * oct;
}

void Surface::Show(const char *name, const char *value)
{
    snprintf(show_name_, sizeof(show_name_), "%s", name);
    snprintf(show_value_, sizeof(show_value_), "%s", value);
    show_t_ = eng_.NowMs() ? eng_.NowMs() : 1;
}

void Surface::SlotName(int slot, char *out)
{
    if(slot == 15)
        snprintf(out, 16, "BUF");
    else
        snprintf(out, 16, "%d", slot);
}

/* ---- input ---------------------------------------------------------------- */

void Surface::HandleInternal(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint8_t type = status & 0xF0;
    if(type == 0x90 || type == 0x80)
    {
        bool on = type == 0x90 && d2 > 0;
        if(d1 <= 9)
            OnKnobTouch(d1, on);
        else if(d1 >= 16 && d1 <= 31)
            OnStep(d1 - 16, on);
        else if(d1 >= 68 && d1 <= 99)
            OnPad(d1 - 68, d2, on);
        return;
    }
    if(type != 0xB0)
        return;
    int delta = d2 < 64 ? d2 : (int)d2 - 128;
    if(d1 >= CC_KNOB1 && d1 < CC_KNOB1 + 8)
        OnKnob(d1 - CC_KNOB1, delta);
    else if(d1 == CC_VOLUME)
        OnKnob(8, delta);
    else if(d1 == CC_JOG)
    {
        if(settings_)
        {
            settings_cursor_ += delta > 0 ? 1 : -1;
            if(settings_cursor_ < 0)
                settings_cursor_ = 0;
            if(settings_cursor_ > kNumSettings - 1)
                settings_cursor_ = kNumSettings - 1;
        }
        else
        {
            int s = NextSlot(eng_.Eng().getVoiceSlot(), delta > 0 ? 1 : -1);
            eng_.SelectSlot(s);
            char v[16];
            SlotName(s, v);
            Show("SLOT", v);
        }
    }
    else if(d1 == CC_LINE_OUT)
        eng_.SetHeadphones(d2 != 0);
    else if(d1 == CC_LINE_IN)
    {
        jack_cc_value_ = d2;
        jack_cc_seen_++;
    }
    else
        OnButton(d1, d2 >= 64);
}

void Surface::OnPad(int pad, int vel, bool on)
{
    (void)vel; // TEMPO plays every note at full level
    if(pad < 0 || pad >= 32)
        return;
    if(on)
    {
        int note = PadNote(pad);
        if(note < 0 || pad_hold_[pad]++ > 0)
            return;
        pad_note_[pad] = note;
        int id         = NoteToKeyId(note);
        if(id >= 0 && KeyIdToNote(id) >= 0)
            eng_.Button(id, true); // the keyboard itself
        else
            eng_.NoteIn(note, true); // an octave off it: the MIDI path
    }
    else if(pad_hold_[pad] > 0 && --pad_hold_[pad] == 0)
    {
        int note = pad_note_[pad];
        int id   = NoteToKeyId(note);
        if(id >= 0 && KeyIdToNote(id) >= 0)
            eng_.Button(id, false);
        else if(note >= 0)
            eng_.NoteIn(note, false);
        pad_note_[pad] = -1;
    }
}

void Surface::OnStep(int step, bool on)
{
    char v[24];
    if(step == 15) // the snapshots
    {
        eng_.SnapshotKey(on);
        if(!on)
        {
            if(eng_.SnapshotCopied())
                Show("SNAPSHOT", eng_.StateIsA() ? "COPIED A > B" : "COPIED B > A");
            else
                Show("SNAPSHOT", eng_.StateIsA() ? "A" : "B");
        }
        return;
    }
    if(!on)
        return;
    const int slot = step + 1; // 15 = the buffer
    SlotName(slot, v);
    if(delete_)
    {
        // the CHOMPI confirms an erase with its key; here, the same step twice
        if(!eng_.SlotValid(slot) || slot == 15)
        {
            Show("CANNOT ERASE", v);
            erase_armed_ = 0;
        }
        else if(erase_armed_ != slot)
        {
            erase_armed_ = slot;
            Show("ERASE SLOT?", "TAP AGAIN");
        }
        else
        {
            Show(eng_.EraseSlot(slot) ? "ERASED" : "CANNOT ERASE", v);
            erase_armed_ = 0;
        }
        return;
    }
    if(capture_)
    {
        Show(eng_.SaveSlot(slot) ? "BUFFER SAVED TO" : "CANNOT SAVE TO", slot == 15 ? "THE BUFFER" : v);
        return;
    }
    if(copy_)
    {
        if(!copy_src_)
        {
            if(eng_.SlotValid(slot))
            {
                copy_src_     = slot;
                copy_src_eng_ = eng_.CurEngine();
                Show("COPY FROM", v);
            }
            else
                Show("COPY FROM", "EMPTY SLOT");
            return;
        }
        char s[16];
        SlotName(copy_src_, s);
        char msg[24];
        snprintf(msg, sizeof(msg), "%s > %s", s, v);
        bool ok = eng_.CopySlot(copy_src_, slot, copy_src_eng_);
        Show(ok ? (copy_src_ == slot && copy_src_eng_ == eng_.CurEngine() ? "COMMITTED" : "COPIED") : "CANNOT COPY", msg);
        copy_src_ = 0;
        return;
    }
    if(eng_.SlotValid(slot))
    {
        eng_.SelectSlot(slot);
        Show("SLOT", v);
    }
}

/* ---- knobs ----------------------------------------------------------------
 * Every sound control has a knob: two pages of eight (Left/Right), plus the
 * volume knob. Each is a CHOMPI encoder on one of its pages, reached the way
 * the play page reached it or the way the shift layer did. Shift keeps only
 * the shift turns that have no knob of their own. */
enum Param
{
    P_SPEED, P_START, P_END, P_ATTACK, P_RELEASE, P_LEVEL, P_FILTER, P_LOFI,
    P_DELAY, P_MIX, P_RANDOM, P_FEEDBACK, P_PAN, P_TEMPO, P_ARPRND, P_COMP,
    P_VOLUME, P_INGAIN, P_SPEED_ST, P_WINDOW, P_ZOOM, P_DIV, P_NONE,
};

struct ParamDef
{
    const char *name, *abbrev;
    int         enc, page;
    bool        menu;
    int         shift;
    int         click; // Delete + touch: the encoder's shift-press, or -1
};

static const ParamDef kParams[] = {
    {"SPEED", "SPD", 0, 0, false, P_SPEED_ST, ENC_4_SW},
    {"START", "STA", 1, 0, false, P_WINDOW, ENC_1_SW},
    {"END", "END", 2, 0, false, P_ZOOM, ENC_2_SW},
    {"ATTACK", "ATK", 1, 1, false, P_NONE, ENC_1_SW},
    {"RELEASE", "REL", 2, 1, false, P_NONE, ENC_2_SW},
    {"LEVEL", "LVL", 0, 1, false, P_NONE, ENC_4_SW},
    {"FILTER", "FLT", 0, 2, false, P_NONE, ENC_4_SW},
    {"LOFI", "LOF", 0, 2, true, P_NONE, ENC_4_SW},
    {"DELAY", "DLY", 3, 0, false, P_NONE, ENC_3_SW},
    {"FX MIX", "MIX", 3, 1, false, P_NONE, ENC_3_SW},
    {"FX RANDOM", "RND", 3, 0, true, P_NONE, ENC_3_SW},
    {"FEEDBACK", "FBK", 3, 1, true, P_NONE, ENC_3_SW},
    {"PAN", "PAN", 0, 1, true, P_NONE, ENC_4_SW},
    {"TEMPO", "BPM", 4, 0, false, P_DIV, -1},
    {"ARP RANDOM", "ARN", 4, 0, true, P_NONE, -1},
    {"COMP", "CMP", 5, 0, true, P_NONE, -1},
    {"VOLUME", "VOL", 5, 0, false, P_INGAIN, -1},
    {"INPUT GAIN", "IN", 5, 1, false, P_NONE, -1},
    {"SPEED STEP", "SPD", 0, 0, true, P_NONE, ENC_4_SW},
    {"MOVE WINDOW", "WIN", 1, 0, true, P_NONE, ENC_1_SW},
    {"WINDOW SIZE", "WIN", 2, 0, true, P_NONE, ENC_2_SW},
    {"DIVISION", "DIV", 4, 0, false, P_NONE, -1},
};

static const int kPage[2][8] = {
    {P_SPEED, P_START, P_END, P_ATTACK, P_RELEASE, P_LEVEL, P_FILTER, P_LOFI},
    {P_DELAY, P_MIX, P_RANDOM, P_FEEDBACK, P_PAN, P_TEMPO, P_ARPRND, P_COMP},
};

int Surface::KnobParam(int knob, bool shift) const
{
    int p = knob == 8 ? P_VOLUME : kPage[page_][knob];
    if(shift && kParams[p].shift != P_NONE)
        p = kParams[p].shift;
    return p;
}

void Surface::OnKnob(int knob, int delta)
{
    if(delta == 0 || knob < 0 || knob > 8)
        return;
    int p = KnobParam(knob, shift_);
    const ParamDef &d = kParams[p];
    if(p == P_DIV)
        eng_.Division(delta);
    else if(p == P_TEMPO)
        eng_.Encoder(4, 0, delta * 2, false); // Munchi: 1 BPM per detent
    else
        eng_.Encoder(d.enc, d.page, delta, d.menu);
    char value[24];
    ParamText(p, value);
    Show(d.name, value);
}

void Surface::OnKnobTouch(int knob, bool on)
{
    if(!on || knob > 8)
        return;
    int p = KnobParam(knob, false);
    const ParamDef &d = kParams[p];
    if(delete_)
    {
        if(d.click < 0)
            return;
        eng_.SetKnobPage(d.enc, d.page);
        eng_.Click(d.click);
        Show(d.click == ENC_3_SW ? "EFFECTS" : d.name, "RESET");
        return;
    }
    p = KnobParam(knob, shift_);
    char value[24];
    ParamText(p, value);
    Show(kParams[p].name, value);
}

void Surface::OnButton(int cc, bool press)
{
    MunchiTempo &e = eng_;
    char         v[24];
    switch(cc)
    {
        case CC_SHIFT: shift_ = press; break;

        case CC_SAMPLE:
            if(shift_)
            {
                if(!press)
                    break;
                OptionsManager &o = e.MutableOpts();
                o.input_source    = (o.input_source + 1) % 3;
                e.OptionsChanged();
                Show("INPUT", kSrcNames[o.input_source]);
                break;
            }
            e.RecordKey(press);
            break;

        case CC_PLAY:
            if(shift_)
            {
                if(press)
                {
                    e.MenuPress(KEY_27);
                    Show("ARP STYLE", kArpNames[e.Arp().getPattern() % 5]);
                }
                break;
            }
            e.Button(KEY_27, press);
            break;

        case CC_LOOP:
        case CC_REC:
            if(shift_)
            {
                if(press)
                {
                    e.MenuPress(KEY_28);
                    Show("RESTS", kRestNames[e.Arp().getRestMode() % 5]);
                }
                break;
            }
            e.Button(KEY_28, press);
            break;

        case CC_DELETE:
            delete_      = press;
            erase_armed_ = 0;
            break;
        case CC_CAPTURE: capture_ = press; break;
        case CC_COPY:
            copy_ = press;
            copy_src_ = 0;
            break;

        case CC_MUTE:
            if(press)
            {
                // the delay must be running to freeze (granularDelay)
                bool was = e.Frozen();
                e.Freeze();
                Show("FREEZE", e.EncValue(0, 3) > .45f && e.EncValue(0, 3) < .55f
                                   ? "TURN DELAY ON FIRST"
                                   : (was ? "OFF" : "ON"));
            }
            break;

        case CC_LEFT:
        case CC_RIGHT:
            if(press)
            {
                page_ = cc == CC_RIGHT ? 1 : 0;
                Show("KNOBS", page_ ? "FX + PATTERN" : "SAMPLE");
            }
            break;

        case CC_UP:
        case CC_DOWN:
            if(!press)
                break;
            if(shift_)
            {
                if(e.CurEngine() != CHROMATIC)
                {
                    Show("OCTAVE", "CHROMA ONLY");
                    break;
                }
                octave_ += cc == CC_UP ? 1 : -1;
                if(octave_ > 0)
                    octave_ = 0; // MIDI notes 24..72: two octaves down, none up
                if(octave_ < -2)
                    octave_ = -2;
                snprintf(v, sizeof(v), "%+d", octave_);
                Show("OCTAVE", v);
            }
            else
            {
                e.SelectEngine(cc == CC_UP ? 1 : 0);
                Show("ENGINE", e.CurEngine() == CHROMATIC ? "CHROMA" : "SLICE");
            }
            break;

        case CC_TRACK1:
            if(!press)
                break;
            if(shift_)
            {
                e.ToggleClockMode();
                Show("CLOCK", e.Clock().getClockMode() == SYNC ? "EXTERNAL MIDI" : "INTERNAL");
            }
            else
            {
                e.TapTempo();
                snprintf(v, sizeof(v), "%d BPM", e.Clock().getTempo() / 2);
                Show("TAP TEMPO", v);
            }
            break;
        case CC_TRACK1 - 1:
            if(press)
            {
                e.ToggleLoop();
                Show("SAMPLE LOOP", e.LoopVal() > .5f ? "ON" : "OFF");
            }
            break;
        case CC_TRACK1 - 2:
            if(press)
            {
                e.ToggleSustain();
                Show("SUSTAIN", e.SustainVal() > .5f ? "ON" : "OFF");
            }
            break;
        case CC_TRACK4:
            if(!press)
                break;
            if(shift_)
            {
                e.MenuPress(ENC_6_SW);
                int m = (int)e.Fx().getMonitorMode() % 3;
                e.MutableOpts().monitor_position = m;
                e.SaveOptions();
                Show("ROUTING", kMonNames[m]);
            }
            else
            {
                e.SetSwitch(!e.GetSwitch());
                if(e.GetSwitch())
                    Show("MONITOR", "OFF");
                else if(e.Opts().input_source == 0 && !e.Fx().GetHeadphones())
                    Show("MONITOR", "MIC: HEADPHONES");
                else
                    Show("MONITOR", "ON");
            }
            break;

        case CC_MENU:
        case CC_JOG_CLICK:
            if(!press)
                break;
            if(settings_ && cc == CC_JOG_CLICK)
                SettingsActivate(shift_ ? -1 : 1);
            else
                settings_ = !settings_;
            exit_prompt_ = false;
            break;

        case CC_BACK:
            if(!press)
                break;
            if(settings_)
            {
                settings_ = false;
                break;
            }
            if(exit_prompt_ && e.NowMs() - exit_prompt_t_ < 3000)
                exit_ = true;
            else
            {
                exit_prompt_   = true;
                exit_prompt_t_ = e.NowMs();
            }
            break;
    }
}

int Surface::NextSlot(int from, int dir)
{
    // slots 1..14 that hold a sample, then 15 (the buffer), wrapping
    int s = from;
    for(int i = 0; i < 15; i++)
    {
        s += dir;
        if(s > 15)
            s = 1;
        if(s < 1)
            s = 15;
        if(eng_.SlotValid(s))
            return s;
    }
    return from;
}

/* ---- settings ------------------------------------------------------------- */

static const char *kSettingNames[kNumSettings] = {
    "INPUT",        "ROUTING",      "RECORD LATCH", "SHIFT SPEED",
    "MIDI IN CH",   "CHROMA OUT CH", "SLICE OUT CH", "CLOCK OUT",
    "CC IN",        "CC OUT",       "START/STOP",   "UNFREEZE MUTE",
    "EXIT",
};

void Surface::SettingsActivate(int dir)
{
    OptionsManager &o = eng_.MutableOpts();
    switch(settings_cursor_)
    {
        case 0: o.input_source = (o.input_source + 3 + dir) % 3; break;
        case 1:
            o.monitor_position = (o.monitor_position + 3 + dir) % 3;
            eng_.Fx().setMonitorMode(o.monitor_position);
            break;
        case 2: o.record_latch = !o.record_latch; break;
        case 3: o.pitch_shift_quantization = !o.pitch_shift_quantization; break;
        case 4: o.midi_ch_in = (o.midi_ch_in + 16 + dir) % 16; break;
        case 5: o.midi_ch_out_chroma = (o.midi_ch_out_chroma + 16 + dir) % 16; break;
        case 6: o.midi_ch_out_slice = (o.midi_ch_out_slice + 16 + dir) % 16; break;
        case 7: o.midi_clock_out = !o.midi_clock_out; break;
        case 8: o.midi_cc_in = !o.midi_cc_in; break;
        case 9: o.midi_cc_out = !o.midi_cc_out; break;
        case 10: o.transport_type = (o.transport_type + 3 + dir) % 3; break;
        case 11: o.delay_mute = !o.delay_mute; break;
        case 12: exit_ = true; return;
    }
    eng_.OptionsChanged();
}

/* ---- knob text ------------------------------------------------------------ */

static void pct(char *out, float v) { snprintf(out, 24, "%d%%", (int)lrintf(v * 100.f)); }

/* sampleEngine::setGlobalPitchFree, the speed it gives */
static float speed_of(float val)
{
    val       = val < .5f ? (.5f - val) * -2.f : (val - .5f) * 2.f;
    float inv = val < 0.f ? -1.f : 1.f, p;
    if(fabsf(val) < .33f)
        p = val * 1.484848f + .01f * inv;
    else if(fabsf(val) < .66f)
        p = (val - .33f * inv) * 1.515151f + .5f * inv;
    else
        p = (val - .66f * inv) * 2.941176f + 1.f * inv;
    return p;
}

static int delay_interval(float v)
{
    if(v < .4f)
        return (int)(v * 20.0f + 0.5f);
    if(v > .6f)
        return (int)((1.0f - v) * 20.0f + 0.5f);
    return 8;
}

void Surface::ParamText(int p, char *value)
{
    MunchiTempo &e = eng_;
    switch(p)
    {
        case P_SPEED:
        case P_SPEED_ST:
        {
            float s = speed_of(e.EncValue(0, 0));
            snprintf(value, 24, "%s%.2fX", s < 0 ? "REV " : "", fabsf(s));
            return;
        }
        case P_START: pct(value, e.EncValue(0, 1)); return;
        case P_END: pct(value, e.EncValue(0, 2)); return;
        case P_WINDOW:
        case P_ZOOM:
            snprintf(value, 24, "%d-%d%%", (int)lrintf(e.EncValue(0, 1) * 100),
                     (int)lrintf(e.EncValue(0, 2) * 100));
            return;
        case P_ATTACK:
        {
            float v = e.EncValue(1, 1);
            float s = e.CurEngine() == CHROMATIC ? (0.001f + v * v * v * .999f) * 5.f : v * 5.f;
            snprintf(value, 24, "%.2fS", s);
            return;
        }
        case P_RELEASE: snprintf(value, 24, "%.2fS", e.EncValue(1, 2)); return;
        case P_LEVEL: pct(value, e.EncValue(1, 0)); return;
        case P_FILTER:
        {
            float v = e.EncValue(2, 0);
            if(v > .45f && v < .55f)
                strcpy(value, "OPEN");
            else
                snprintf(value, 24, "%s %d", v < .5f ? "LP" : "HP",
                         (int)lrintf(fabsf(v - .5f) * 200.f));
            return;
        }
        case P_LOFI: pct(value, e.Reduce()); return;
        case P_DELAY:
        {
            float v = e.EncValue(0, 3);
            if(v >= .45f && v <= .55f)
                strcpy(value, "OFF");
            else
                snprintf(value, 24, "%s %s", v < .5f ? "DELAY" : "VERB",
                         kDelayDivs[delay_interval(v)]);
            return;
        }
        case P_MIX: pct(value, e.EncValue(1, 3)); return;
        case P_RANDOM: pct(value, e.Randomness()); return;
        case P_FEEDBACK: pct(value, e.Feedback()); return;
        case P_PAN:
        {
            float v = e.Pan();
            if(fabsf(v - .5f) < .01f)
                strcpy(value, "C");
            else
                snprintf(value, 24, "%s%d", v < .5f ? "L" : "R", (int)lrintf(fabsf(v - .5f) * 200.f));
            return;
        }
        case P_TEMPO:
            if(e.Clock().getClockMode() == SYNC)
                snprintf(value, 24, "EXT %s", kSyncDivNames[e.Clock().getClockDivPos(e.CurEngine()) % 5]);
            else
                snprintf(value, 24, "%d BPM", e.Clock().getTempo() / 2);
            return;
        case P_DIV:
        {
            const char **names = e.Clock().getClockMode() == SYNC ? kSyncDivNames : kFreeDivNames;
            strcpy(value, names[e.Clock().getClockDivPos(e.CurEngine()) % 5]);
            return;
        }
        case P_ARPRND: pct(value, e.ArpRandomness()); return;
        case P_COMP: pct(value, e.FinalComp()); return;
        case P_VOLUME: pct(value, e.EncValue(0, 5)); return;
        case P_INGAIN: pct(value, e.EncValue(1, 5)); return;
    }
    value[0] = 0;
}

float Surface::ParamValue(int p)
{
    MunchiTempo &e = eng_;
    switch(p)
    {
        case P_SPEED: return e.EncValue(0, 0);
        case P_START: return e.EncValue(0, 1);
        case P_END: return e.EncValue(0, 2);
        case P_ATTACK: return e.EncValue(1, 1);
        case P_RELEASE: return e.EncValue(1, 2);
        case P_LEVEL: return e.EncValue(1, 0);
        case P_FILTER: return e.EncValue(2, 0);
        case P_LOFI: return e.Reduce();
        case P_DELAY: return e.EncValue(0, 3);
        case P_MIX: return e.EncValue(1, 3);
        case P_RANDOM: return e.Randomness();
        case P_FEEDBACK: return e.Feedback();
        case P_PAN: return e.Pan();
        case P_TEMPO: return (e.Clock().getTempo() - 160) / 320.f;
        case P_ARPRND: return e.ArpRandomness();
        case P_COMP: return e.FinalComp();
    }
    return 0.f;
}

/* ---- LEDs ----------------------------------------------------------------- */

uint8_t Surface::KeyColor(int pad)
{
    int note = PadNote(pad);
    if(note < 0)
        return C_OFF;
    int id = NoteToKeyId(note);
    if(id < 0)
        return C_OFF;
    ArpeggiatorSequencer &arp   = eng_.Arp();
    const int             eng   = eng_.CurEngine();
    const bool            black = is_black(kPadNote[pad]);
    // NormalPage::Draw: playing = white; latched = red (chroma) / yellow
    // (slice); the slice engine's black keys are rests
    if(arp.isKeyPlaying(id) || (arp.isRestPlaying(id) && arp.getPlay(1) && eng == SLICE))
        return C_WHITE;
    if(arp.getLatch() && arp.isKeyInSeq(id, eng))
        return eng == CHROMATIC ? C_RED : C_YELLOW;
    if(eng == SLICE)
        return black ? C_OFF : C_GREY_DIM;
    if(octave_ != 0)
        return black ? C_TEAL_DIM : C_TEAL;
    return black ? C_GREY_DARK : C_GREY_DIM;
}

void Surface::ComputeLeds()
{
    MunchiTempo          &e   = eng_;
    ArpeggiatorSequencer &arp = e.Arp();
    const uint32_t        now = e.NowMs();
    const bool            blink = (now / 250) & 1;
    const int             eng = e.CurEngine();

    for(int p = 0; p < 32; p++)
        want_pad_[p] = e.Ready() ? KeyColor(p) : C_OFF;

    for(int s = 0; s < 15; s++)
    {
        int     slot  = s + 1;
        bool    valid = e.Ready() && e.SlotValid(slot);
        uint8_t c     = C_OFF;
        if(delete_)
            c = erase_armed_ == slot ? C_RED
                : valid && slot < 15 ? (blink ? C_RED : C_OFF) : C_OFF;
        else if(capture_)
            c = slot < 15 ? (blink ? C_BLUE : (valid ? C_PURPLE_DIM : C_OFF)) : C_OFF;
        else if(copy_)
            c = copy_src_ == slot ? C_GREEN
                : copy_src_ ? (slot < 15 ? (blink ? C_BLUE : (valid ? C_PURPLE_DIM : C_OFF)) : C_OFF)
                            : (valid ? (blink ? C_GREEN : C_PURPLE_DIM) : C_OFF);
        else if(e.Ready() && slot == e.Eng().getVoiceSlot())
            c = C_WHITE;
        else if(slot == 15)
            c = valid ? C_PINK_DIM : C_OFF;
        else if(valid)
            c = eng == CHROMATIC ? C_PURPLE_DIM : C_ORANGE;
        want_step_[s] = c;
    }
    want_step_[15] = e.StateIsA() ? C_GREEN : C_BLUE;

    memset(want_cc_, 0, sizeof(want_cc_));
    auto set = [&](int cc, uint8_t v) {
        want_cc_[cc] = v;
        cc_used_[cc] = true;
    };
    // play: the pattern generator; flashes on the clock (left/right lights)
    size_t  pt   = arp.getPlayType();
    uint8_t play = pt == 2 ? (arp.getLeftLights() ? C_TEAL : C_GREEN) : pt == 1 ? C_TEAL_DIM : C_OFF;
    // loop: sustain orange, latch red (chroma) / yellow (slice)
    uint8_t loop = arp.getSustain() ? C_ORANGE
                   : arp.getLatch() ? (eng == CHROMATIC ? C_RED : C_YELLOW)
                                    : C_OFF;
    set(CC_PLAY, play);
    set(CC_LOOP, loop);
    set(CC_REC, loop);
    set(CC_SAMPLE, e.Recording() ? C_RED : C_GREY_DARK);
    set(CC_MUTE, e.Frozen() ? (blink ? C_WHITE : C_GREY_DARK) : C_GREY_DARK);
    {
        float beat_ms = 60000.f / (e.Clock().getTempo() / 2.f);
        bool  ext     = e.Clock().getClockMode() == SYNC;
        set(CC_TRACK1, fmodf((float)now, beat_ms) < 60.f ? (ext ? C_PURPLE : C_WHITE) : C_GREY_DARK);
    }
    set(CC_TRACK1 - 1, e.LoopVal() > .5f ? C_WHITE : C_GREY_DARK);
    set(CC_TRACK1 - 2, e.SustainVal() > .5f ? C_WHITE : C_GREY_DARK);
    {
        uint8_t mon = C_GREY_DARK;
        if(!e.GetSwitch())
            mon = (e.Opts().input_source == 0 && !e.Fx().GetHeadphones()) ? C_ORANGE : C_WHITE;
        set(CC_TRACK4, mon);
    }
    set(CC_LEFT, page_ == 1 ? C_WHITE : C_OFF);
    set(CC_RIGHT, page_ == 0 ? C_WHITE : C_OFF);
    set(CC_UP, eng == SLICE ? C_WHITE : C_GREY_DARK);
    set(CC_DOWN, eng == CHROMATIC ? C_WHITE : C_GREY_DARK);
    set(CC_MENU, settings_ ? C_WHITE : C_GREY_DARK);
    set(CC_BACK, exit_prompt_ ? C_WHITE : C_GREY_DARK);
    set(CC_DELETE, delete_ ? C_RED : C_GREY_DARK);
    set(CC_COPY, copy_ ? C_GREEN : C_GREY_DARK);
    set(CC_CAPTURE, capture_ ? C_BLUE : C_GREY_DARK);
    set(CC_SHIFT, shift_ ? C_WHITE : C_GREY_DARK);
}

static inline bool put_pkt(uint8_t *spi, int *n, uint8_t cin_cable, uint8_t st,
                           uint8_t d1, uint8_t d2)
{
    if(*n >= SCHWUNG_MIDI_OUT_MAX)
        return false;
    uint8_t *p = spi + SCHWUNG_OFF_OUT_MIDI + (*n) * 4;
    p[0] = cin_cable;
    p[1] = st;
    p[2] = d1;
    p[3] = d2;
    (*n)++;
    return true;
}

void Surface::Tick(uint8_t *spi)
{
    tick_++;
    int n = 0;

    // MIDI out to USB-A (cable 2): clock and transport are 1-byte system
    // messages (CIN 0xF), notes and CCs their channel CIN
    Hardware::Msg m;
    while(n < 8 && eng_.PopMidiOut(&m))
    {
        if(m.len == 1)
            put_pkt(spi, &n, 0x2F, m.bytes[0], 0, 0);
        else
            put_pkt(spi, &n, (uint8_t)(0x20 | (m.bytes[0] >> 4)), m.bytes[0], m.bytes[1],
                    m.bytes[2]);
    }

    if((tick_ & 7) == 0)
        ComputeLeds();

    refresh_i_ = (refresh_i_ + 1) % (32 + 16 + 128);
    if(refresh_i_ < 32)
        sent_pad_[refresh_i_] = 0xFF;
    else if(refresh_i_ < 48)
        sent_step_[refresh_i_ - 32] = 0xFF;
    else
        sent_cc_[refresh_i_ - 48] = 0xFF;

    for(int p = 0; p < 32 && n < SCHWUNG_MIDI_OUT_MAX; p++)
        if(want_pad_[p] != sent_pad_[p]
           && put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(68 + p), want_pad_[p]))
            sent_pad_[p] = want_pad_[p];
    for(int s = 0; s < 16 && n < SCHWUNG_MIDI_OUT_MAX; s++)
        if(want_step_[s] != sent_step_[s]
           && put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(16 + s), want_step_[s]))
            sent_step_[s] = want_step_[s];
    for(int c = 0; c < 128 && n < SCHWUNG_MIDI_OUT_MAX; c++)
        if(cc_used_[c] && want_cc_[c] != sent_cc_[c]
           && put_pkt(spi, &n, 0x0B, 0xB0, (uint8_t)c, want_cc_[c]))
            sent_cc_[c] = want_cc_[c];

    if(disp_.AtFrameStart())
        Draw();
    disp_.PushSlice(spi);
}

int Surface::WriteAllOff(uint8_t *spi, int start)
{
    int n = 0, i = start;
    for(; i < 32 + 16 + 128 && n < SCHWUNG_MIDI_OUT_MAX; i++)
    {
        if(i < 32)
            put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(68 + i), 0);
        else if(i < 48)
            put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(16 + i - 32), 0);
        else if(cc_used_[i - 48])
            put_pkt(spi, &n, 0x0B, 0xB0, (uint8_t)(i - 48), 0);
    }
    return i >= 32 + 16 + 128 ? -1 : i;
}

/* ---- screen --------------------------------------------------------------- */

void Surface::Draw()
{
    disp_.Clear();
    if(exit_prompt_ && eng_.NowMs() - exit_prompt_t_ > 3000)
        exit_prompt_ = false;
    if(exit_prompt_)
    {
        disp_.TextCentered(18, "EXIT MUNCHI TEMPO?");
        disp_.TextCentered(34, "BACK AGAIN TO EXIT");
        disp_.TextCentered(46, "UNSAVED BUFFER IS LOST");
        return;
    }
    if(settings_)
        DrawSettings();
    else
        DrawMain();
}

void Surface::DrawSettings()
{
    disp_.Text(0, 0, "SETTINGS");
    disp_.HLine(0, 9, 128);
    const OptionsManager &o = eng_.Opts();
    int first = settings_cursor_ - 4 < 0 ? 0 : settings_cursor_ - 4;
    static const char *kTransport[3] = {"IN + OUT", "OUT ONLY", "IN ONLY"};
    for(int row = 0; row < 5; row++)
    {
        int i = first + row;
        if(i > kNumSettings - 1)
            break;
        char v[16] = "";
        switch(i)
        {
            case 0: strcpy(v, kSrcNames[o.input_source % 3]); break;
            case 1: strcpy(v, kMonNames[o.monitor_position % 3]); break;
            case 2: strcpy(v, o.record_latch ? "ON" : "OFF"); break;
            case 3: strcpy(v, o.pitch_shift_quantization ? "STEPPED" : "FREE"); break;
            case 4: snprintf(v, sizeof(v), "%d", o.midi_ch_in + 1); break;
            case 5: snprintf(v, sizeof(v), "%d", o.midi_ch_out_chroma + 1); break;
            case 6: snprintf(v, sizeof(v), "%d", o.midi_ch_out_slice + 1); break;
            case 7: strcpy(v, o.midi_clock_out ? "ON" : "OFF"); break;
            case 8: strcpy(v, o.midi_cc_in ? "ON" : "OFF"); break;
            case 9: strcpy(v, o.midi_cc_out ? "ON" : "OFF"); break;
            case 10: strcpy(v, kTransport[o.transport_type % 3]); break;
            case 11: strcpy(v, o.delay_mute ? "ON" : "OFF"); break;
        }
        int y = 12 + row * 10;
        disp_.Text(2, y, kSettingNames[i]);
        disp_.TextRight(126, y, v);
        if(i == settings_cursor_)
            disp_.Invert(0, y - 1, 128, 9);
    }
}

/* the current sample, its window, and (Slice) the 16 slices */
void Surface::DrawSample(int y0, int h)
{
    MunchiTempo   &e    = eng_;
    const int      eng  = e.CurEngine();
    const int      slot = e.Eng().getVoiceSlot();
    sampleManager &sm   = e.Samples();
    const int16_t *pcm  = (const int16_t *)sm.getNextSample(slot - 1, eng);
    const size_t   len  = pcm ? sm.getNextSampleSize(slot - 1, eng) : 0;
    if(e.Recording())
    {
        disp_.Text(0, y0 + 2, "REC");
        disp_.Frame(20, y0 + 2, 106, 7);
        disp_.Rect(21, y0 + 3, (int)(104 * e.RecordFill()), 5);
        return;
    }
    if(!len)
    {
        disp_.TextCentered(y0 + 3, "EMPTY");
        return;
    }
    if(pcm != peaks_src_ || len != peaks_len_)
    {
        peaks_src_ = pcm;
        peaks_len_ = len;
        for(int x = 0; x < 128; x++)
        {
            size_t a = len * x / 128, b = len * (x + 1) / 128;
            size_t step = (b - a) / 48 + 1;
            int    pk   = 0;
            for(size_t i = a; i < b; i += step)
            {
                int v = pcm[i * 2];
                v     = v < 0 ? -v : v;
                if(v > pk)
                    pk = v;
            }
            peaks_[x] = (uint8_t)(pk * (h / 2) / 32768);
        }
    }
    const int mid = y0 + h / 2;
    for(int x = 0; x < 128; x++)
        disp_.VLine(x, mid - peaks_[x], 2 * peaks_[x] + 1);
    int xs = (int)(e.EncValue(0, 1) * 127.f), xe = (int)(e.EncValue(0, 2) * 127.f);
    // dim what is outside the window by inverting a dotted band over it
    for(int x = 0; x < 128; x++)
        if(x < xs || x > xe)
            for(int y = y0; y < y0 + h; y += 2)
                disp_.Pixel(x, y + (x & 1), 0);
    disp_.VLine(xs, y0, h);
    disp_.VLine(xe, y0, h);
    if(eng == SLICE)
        for(int i = 1; i < 16; i++)
        {
            int x = xs + (xe - xs) * i / 16;
            disp_.Pixel(x, y0 + h);
        }
}

void Surface::DrawMain()
{
    MunchiTempo          &e   = eng_;
    ArpeggiatorSequencer &arp = e.Arp();
    char                  buf[40];

    if(!e.Ready())
    {
        disp_.TextCentered(22, "MUNCHI TEMPO");
        disp_.TextCentered(36, "LOADING SAMPLES");
        return;
    }

    // header: engine, slot, snapshot | tempo
    const int slot = e.Eng().getVoiceSlot();
    char      s[16];
    SlotName(slot, s);
    snprintf(buf, sizeof(buf), "%s %s %s", e.CurEngine() == CHROMATIC ? "CHROMA" : "SLICE", s,
             e.StateIsA() ? "A" : "B");
    disp_.Text(0, 0, buf);
    if(e.Clock().getClockMode() == SYNC)
        snprintf(buf, sizeof(buf), "EXT %s", kSyncDivNames[e.Clock().getClockDivPos(e.CurEngine()) % 5]);
    else
        snprintf(buf, sizeof(buf), "%d %s", e.Clock().getTempo() / 2,
                 kFreeDivNames[e.Clock().getClockDivPos(e.CurEngine()) % 5]);
    disp_.TextRight(128, 0, buf);

    DrawSample(10, 13);

    const uint32_t now = e.NowMs();
    int            y   = 26;
    if(copy_ || capture_ || delete_)
    {
        const char *l1 = copy_ ? (copy_src_ ? "COPY: PICK DESTINATION" : "COPY: PICK SOURCE")
                         : capture_ ? "SAVE BUFFER: PICK A SLOT"
                                    : (erase_armed_ ? "ERASE: TAP THE STEP AGAIN" : "ERASE: PICK A SLOT");
        disp_.Text(0, y, l1);
        disp_.Text(0, y + 10, copy_ ? "SAME STEP = COMMIT KNOBS" : "STEP BUTTONS 1-14");
    }
    else if(show_t_ && now - show_t_ < 1500)
    {
        disp_.Text(0, y, show_name_);
        disp_.Text(0, y + 10, show_value_, 2);
    }
    else
    {
        for(int i = 0; i < 8; i++)
        {
            int p  = kPage[page_][i];
            int cx = (i % 4) * 32, cy = y + (i / 4) * 13;
            disp_.Text(cx, cy, kParams[p].abbrev);
            disp_.Frame(cx, cy + 8, 28, 3);
            disp_.HLine(cx, cy + 9, 1 + (int)(ParamValue(p) * 27));
        }
    }

    // pattern line
    const char *state = arp.getSustain()                     ? "SUSTAIN"
                        : arp.getPlay(e.CurEngine())          ? (arp.getLatch() ? "PLAY+LATCH" : "PLAY")
                        : arp.getLatch()                      ? "LATCH"
                                                              : "KEYS";
    snprintf(buf, sizeof(buf), "%s %s", state, kArpNames[arp.getPattern() % 5]);
    disp_.Text(0, 56, buf);
    disp_.TextRight(128, 56, kRestDots[arp.getRestMode() % 5]);
}

} // namespace munchi
