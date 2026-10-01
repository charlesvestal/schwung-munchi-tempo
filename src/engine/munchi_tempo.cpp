/* munchi_tempo.cpp -- see munchi_tempo.h.
 *
 * The control code is NormalPage.h, MenuPage.h, ui.h, MidiManager.h,
 * PresetManager.h and OptionsManager.h from CHOMPI TEMPO 1.0 (originals in
 * tempo/upstream/), ported with their structure intact. LEDs are
 * surface.cpp's; what the pages' Draw() did to the ENGINE is done here, in
 * UiTick(). Changes from the firmware are marked "Munchi:".
 */
#include "munchi_tempo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sched.h>
#include <algorithm>
#include <chrono>
#include <vector>

uint32_t daisy::System::now_ms = 0;

namespace munchi
{

/* ---- firmware tables ----------------------------------------------------- */

// NormalPage.h
static const uint8_t cc_map[3][6] = {{20, 21, 22, 23, 24, 25},
                                     {26, 27, 28, 29, 0, 30},
                                     {31, 0, 0, 0, 0, 0}};
static const uint8_t key_map[40] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x15, 0x00, 0x31, 0x32, 0x34,
    0x35, 0x37, 0x33, 0x36, 0x38, 0x30, 0x39, 0x3b, 0x3c, 0x3e,
    0x40, 0x3a, 0x3d, 0x3f, 0x41, 0x43, 0x45, 0x47, 0x48, 0x42,
    0x44, 0x46, 0x05, 0x17, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const float kEncoderFineStep   = .003f;
static const float kEncoderTempoStep  = .003125f;
static const float kEncoderCoarseStep = .01f;
static const float kEncoderCycleStep  = 1.f / 33.f;

// ui.h
static const float enc_defaults[3][6] = {
    {.83f, 0.f, 1.f, .5f, .5f, .6f}, // page 1
    {.6f, 0.f, 0.f, .5f, 0.f, .75f}, // page 2
    {.5f, 0.f, 0.f, .5f, 0.f, 0.f},  // page 3
};
static const uint8_t midi2key[49] = {
    44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, //
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, //
    15, 7,  8,  12, 9,  10, 13, 11,                 //
    14, 16, 21, 17, 18, 22, 19, 23,                 //
    20, 24, 29, 25, 30, 26, 31, 27, 28};

static constexpr size_t kBufferSize = 480000; // 10 s @ 48 kHz (chompi_main.cpp)

int KeyIdToNote(int id)
{
    if(id < KEY_16 || id > KEY_25)
        return -1;
    return key_map[id];
}

int NoteToKeyId(int note)
{
    int k = note - 24;
    if(k < 0 || k > 48)
        return -1;
    return midi2key[k]; // 7..31 on the keyboard, 32..55 off it
}

int KeyToSlot(int buttonID) // MenuPage::KeyToSlot
{
    if(buttonID == 7)
        return kSlotNone;
    else if(buttonID < 12)
        return buttonID - 6;
    else if(buttonID < 15)
        return kSlotNone;
    else if(buttonID == 15)
        return 1;
    else if(buttonID < 21)
        return buttonID - 10;
    else if(buttonID < 24)
        return kSlotNone;
    else if(buttonID < 29)
        return buttonID - 13;
    return kSlotNone;
}

/* ---- presets.json (PresetManager.h) --------------------------------------- */

void TempoPresets::Init(const float *defaults)
{
    for(int m = 0; m < kModes; m++)
        for(int s = 0; s < kSlots; s++)
            for(int c = 0; c < kControls; c++)
                presetValues[m][s][c] = defaults[c];
    for(int c = 0; c < kControls; c++)
        preset_defaults[c] = presetValues[0][0][c];
    updated = false;
}

float TempoPresets::GetValue(size_t mode, size_t slot, size_t control)
{
    if(mode >= (size_t)kModes || slot >= (size_t)kSlots || control >= (size_t)kControls)
        return preset_defaults[control < (size_t)kControls ? control : 0];
    return presetValues[mode][slot][control];
}

void TempoPresets::SetValue(float value, size_t mode, size_t slot, size_t control)
{
    slot -= 1;
    if(mode >= (size_t)kModes || slot >= (size_t)kSlots || control >= (size_t)kControls)
        return;
    presetValues[mode][slot][control] = value;
}

/* [[[11 ints] x 14] x 2], values x 1000; pitch rounded down a hair */
bool TempoPresets::Parse(const char *json)
{
    std::vector<int> nums;
    std::vector<int> depths;
    int              depth = 0, max_depth = 0;
    for(const char *p = json; *p; p++)
    {
        if(*p == '[')
            max_depth = std::max(max_depth, ++depth);
        else if(*p == ']')
            depth--;
        else if(*p == '-' || (*p >= '0' && *p <= '9'))
        {
            char *e;
            long  v = strtol(p, &e, 10);
            nums.push_back((int)v);
            depths.push_back(depth);
            p = e - 1;
        }
    }
    if(max_depth != 3 || nums.size() < (size_t)(kModes * kSlots * kControls))
        return false;
    size_t i = 0;
    for(int m = 0; m < kModes; m++)
        for(int s = 0; s < kSlots; s++)
            for(int c = 0; c < kControls; c++, i++)
                presetValues[m][s][c] = c == 0 ? nextafterf(nums[i] * 0.001f, -INFINITY)
                                               : .001f * nums[i];
    return true;
}

std::string TempoPresets::Serialize() const
{
    std::string out = "[";
    char        tmp[24];
    for(int m = 0; m < kModes; m++)
    {
        out += "[";
        for(int s = 0; s < kSlots; s++)
        {
            out += "[";
            for(int c = 0; c < kControls; c++)
            {
                snprintf(tmp, sizeof(tmp), c + 1 < kControls ? "%d," : "%d",
                         int(presetValues[m][s][c] * 1000));
                out += tmp;
            }
            out += s + 1 < kSlots ? "]," : "]";
        }
        out += m + 1 < kModes ? "]," : "]";
    }
    out += "]";
    return out;
}

/* ---- lifecycle ------------------------------------------------------------ */

MunchiTempo::MunchiTempo() {}

MunchiTempo::~MunchiTempo()
{
    StopCard();
    delete reverb_;
    free(granular_buf_);
    free(frozen_buf_);
}

int MunchiTempo::Init()
{
    reverb_       = new daisysp::Reverb();
    granular_buf_ = (float *)calloc(kBufferSize * 2, sizeof(float));
    frozen_buf_   = (float *)calloc(kBufferSize * 2, sizeof(float));
    if(!reverb_ || !granular_buf_ || !frozen_buf_ || !sm_.Init(48000.f))
        return -1;

    daisy::System::now_ms = 0;

    // chompi_main.cpp main(), in its order
    clock_.Init(&timer_, 120000000); // tick rate = 240 MHz / 240 = 1 MHz
    reverb_->Init(48000.f);
    fx_.Init(&sm_, 48000.f, &delay_, reverb_, opts_.monitor_position);
    delay_.Init(granular_buf_, frozen_buf_, kBufferSize, &clock_, opts_.delay_mute);
    for(int i = 0; i < 2; i++)
        engines_[i]->Init(&sm_, 48000.f);

    // ui.h InitFromPresets
    float defaults[11] = {enc_defaults[0][0], enc_defaults[1][0], enc_defaults[2][0],
                          enc_defaults[0][1], enc_defaults[1][1], enc_defaults[0][2],
                          enc_defaults[1][2], .5f, 0.f, 1.f, 1.f};
    presets_.Init(defaults);

    // NormalPage::Init
    for(int k = 0; k < 6; k++)
        for(int p = 0; p < 3; p++)
            enc_values_[p][k] = enc_defaults[p][k];

    // MenuPage::Init
    pre_quantized_amount_   = .5f;
    window_encoder_counter_ = 0;
    final_comp_             = 0.f;
    randomness_             = 0.f;
    feedback_               = .3f;
    pan_                    = .5f;
    sustain_                = 1.f;
    loop_                   = 1.f;
    reduce_                 = 0.f;
    arp_randomness_         = 0.f;
    fx_.setGranularFeedback(feedback_);
    InitStatesFromDefault();

    arp_.Init(engines_, &clock_, &hw_, &opts_);
    hw_.setMidiCCOut(opts_.midi_cc_out);
    state_.Init();
    fx_.SetInputSource((InputSource)opts_.input_source);

    up_.Init(44100.0, 48000.0);
    down_.Init(48000.0, 44100.0);
    out44_count_ = 160;
    memset(out44_l_, 0, sizeof(out44_l_));
    memset(out44_r_, 0, sizeof(out44_r_));
    memset(out48_, 0, sizeof(out48_));
    init_time_ = 0;
    return 0;
}

void MunchiTempo::OptionsChanged()
{
    hw_.setMidiCCOut(opts_.midi_cc_out);
    arp_.setMidiOptions(opts_.midi_ch_out_chroma, opts_.midi_ch_out_slice, opts_.transport_type);
    delay_.setMuteOption(opts_.delay_mute);
    fx_.SetInputSource((InputSource)opts_.input_source);
    SaveOptions();
}

/* ---- card ----------------------------------------------------------------- */

static bool is_file(const std::string &p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool is_dir(const std::string &p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static void mkdirs(const std::string &path)
{
    std::string p;
    for(size_t i = 0; i < path.size(); i++)
    {
        p += path[i];
        if(path[i] == '/' && p.size() > 1)
            mkdir(p.c_str(), 0775);
    }
    mkdir(path.c_str(), 0775);
}

static int copy_file(const std::string &from, const std::string &to)
{
    FILE *in = fopen(from.c_str(), "rb");
    if(!in)
        return -1;
    FILE *out = fopen((to + ".tmp").c_str(), "wb");
    if(!out)
    {
        fclose(in);
        return -1;
    }
    char   buf[65536];
    size_t n;
    while((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fflush(out);
    fsync(fileno(out));
    fclose(out);
    return rename((to + ".tmp").c_str(), to.c_str());
}

/* the factory card, file by file, never over anything already there */
static void seed_tree(const std::string &from, const std::string &to)
{
    mkdirs(to);
    DIR *d = opendir(from.c_str());
    if(!d)
        return;
    while(struct dirent *e = readdir(d))
    {
        std::string n = e->d_name;
        if(n[0] == '.')
            continue;
        std::string src = from + "/" + n, dst = to + "/" + n;
        if(is_dir(src))
            seed_tree(src, dst);
        else if(!is_file(dst))
            copy_file(src, dst);
    }
    closedir(d);
}

static std::string read_text(const std::string &p)
{
    std::string s;
    if(FILE *f = fopen(p.c_str(), "rb"))
    {
        char   buf[4096];
        size_t n;
        while((n = fread(buf, 1, sizeof(buf), f)) > 0)
            s.append(buf, n);
        fclose(f);
    }
    return s;
}

void MunchiTempo::StartCard(const std::string &card_dir, const std::string &factory_dir)
{
    card_dir_ = card_dir;
    loader_   = std::thread([this, factory_dir] { LoaderMain(factory_dir); });
}

/* off the SPI loop: the first run copies ~30 MB of factory samples */
void MunchiTempo::LoaderMain(std::string factory_dir)
{
#ifdef __linux__
    struct sched_param sp = {};
    sched_setscheduler(0, SCHED_OTHER, &sp);
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    sched_setaffinity(0, sizeof(set), &set);
#endif
    mkdirs(card_dir_);
    const std::string marker = card_dir_ + "/.munchi-card";
    if(!is_file(marker) && !factory_dir.empty())
    {
        seed_tree(factory_dir, card_dir_);
        if(FILE *f = fopen(marker.c_str(), "wb"))
        {
            fputs("Munchi Tempo card. Delete this file to re-copy missing factory files.\n", f);
            fclose(f);
        }
    }
    std::string pj = read_text(card_dir_ + "/presets.json");
    if(!pj.empty())
        presets_.Parse(pj.c_str());
    sm_.LoadCard(card_dir_);
    sm_.StartWorker();
    loaded_.store(true, std::memory_order_release);

    writer_run_.store(true);
    while(writer_run_.load())
    {
        WriterTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static void write_atomic(const std::string &p, const std::string &s)
{
    std::string tmp = p + ".tmp";
    if(FILE *f = fopen(tmp.c_str(), "wb"))
    {
        fwrite(s.data(), 1, s.size(), f);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
        rename(tmp.c_str(), p.c_str());
    }
}

void MunchiTempo::WriterTick()
{
    if(presets_ready_.load(std::memory_order_acquire))
    {
        write_atomic(card_dir_ + "/presets.json", presets_snapshot_);
        presets_ready_.store(false, std::memory_order_release);
    }
    if(options_ready_.load(std::memory_order_acquire))
    {
        write_atomic(card_dir_ + "/options.json", options_snapshot_);
        options_ready_.store(false, std::memory_order_release);
    }
}

void MunchiTempo::StopCard()
{
    writer_run_.store(false);
    if(loader_.joinable())
        loader_.join();
    if(!loaded_.load())
        return;
    if(presets_.updated && !presets_ready_.load())
    {
        presets_snapshot_ = presets_.Serialize();
        presets_.updated  = false;
        presets_ready_.store(true);
    }
    WriterTick();
    sm_.StopWorker(); // finishes the sample writes
}

/* options.json, TEMPO's format */
static bool json_value(const std::string &s, const char *name, std::string *out)
{
    size_t p = s.find(std::string("\"") + name + "\"");
    if(p == std::string::npos || (p = s.find("\"value\"", p)) == std::string::npos
       || (p = s.find(':', p)) == std::string::npos)
        return false;
    p++;
    while(p < s.size() && strchr(" \t\r\n", s[p]))
        p++;
    size_t e = p;
    while(e < s.size() && !strchr(",}\r\n", s[e]))
        e++;
    *out = s.substr(p, e - p);
    return true;
}

void MunchiTempo::LoadOptions(const std::string &path)
{
    std::string s = read_text(path), v;
    auto ch = [&](const char *name, uint8_t *dst) {
        if(json_value(s, name, &v))
        {
            int c = atoi(v.c_str()) - 1;
            if(c >= 0 && c < 16)
                *dst = (uint8_t)c;
        }
    };
    if(json_value(s, "Record Latch", &v))
        opts_.record_latch = v == "true";
    ch("Midi In Channel", &opts_.midi_ch_in);
    ch("Midi Out Channel Chromatic", &opts_.midi_ch_out_chroma);
    ch("Midi Out Channel Slice", &opts_.midi_ch_out_slice);
    if(json_value(s, "MIDI Clock Out", &v))
        opts_.midi_clock_out = v != "false";
    // Munchi: the firmware tested the wrong field and never read this back
    if(json_value(s, "Monitor Position", &v))
    {
        int m = atoi(v.c_str()) - 1;
        if(m >= 0 && m < 3)
            opts_.monitor_position = m;
    }
    if(json_value(s, "Pitch Quantize In Shift Menu", &v))
        opts_.pitch_shift_quantization = v != "false";
    if(json_value(s, "MIDI CC In", &v))
        opts_.midi_cc_in = v != "false";
    if(json_value(s, "MIDI CC Out", &v))
        opts_.midi_cc_out = v != "false";
    if(json_value(s, "Midi Start-Stop Message Behavior", &v))
    {
        int t = atoi(v.c_str()) - 1;
        if(t >= 0 && t < 3)
            opts_.transport_type = t;
    }
    if(json_value(s, "Delay Buffer Unfreeze Mute", &v))
        opts_.delay_mute = v == "true";
    if(json_value(s, "Input Source", &v)) // Munchi
    {
        int i = atoi(v.c_str());
        if(i >= 0 && i < 3)
            opts_.input_source = i;
    }
}

void MunchiTempo::SaveOptions()
{
    if(options_ready_.load())
        return;
    char buf[2048];
    auto item = [](char *b, size_t n, const char *name, const char *val, bool last) {
        return snprintf(b, n, "\t\t{\n\t\t\t\"name\": \"%s\",\n\t\t\t\"value\": %s\n\t\t}%s\n",
                        name, val, last ? "" : ",");
    };
    char in[8], oc[8], os[8], mp[8], tt[8], is[8];
    snprintf(in, sizeof(in), "%d", opts_.midi_ch_in + 1);
    snprintf(oc, sizeof(oc), "%d", opts_.midi_ch_out_chroma + 1);
    snprintf(os, sizeof(os), "%d", opts_.midi_ch_out_slice + 1);
    snprintf(mp, sizeof(mp), "%d", (int)opts_.monitor_position + 1);
    snprintf(tt, sizeof(tt), "%d", (int)opts_.transport_type + 1);
    snprintf(is, sizeof(is), "%d", opts_.input_source);
    const char *T = "true", *F = "false";
    int n = snprintf(buf, sizeof(buf), "{\n\t\"chompi\": [\n");
    n += item(buf + n, sizeof(buf) - n, "Record Latch", opts_.record_latch ? T : F, false);
    n += item(buf + n, sizeof(buf) - n, "Midi In Channel", in, false);
    n += item(buf + n, sizeof(buf) - n, "Midi Out Channel Chromatic", oc, false);
    n += item(buf + n, sizeof(buf) - n, "Midi Out Channel Slice", os, false);
    n += item(buf + n, sizeof(buf) - n, "MIDI Clock Out", opts_.midi_clock_out ? T : F, false);
    n += item(buf + n, sizeof(buf) - n, "Monitor Position", mp, false);
    n += item(buf + n, sizeof(buf) - n, "Pitch Quantize In Shift Menu",
              opts_.pitch_shift_quantization ? T : F, false);
    n += item(buf + n, sizeof(buf) - n, "MIDI CC In", opts_.midi_cc_in ? T : F, false);
    n += item(buf + n, sizeof(buf) - n, "MIDI CC Out", opts_.midi_cc_out ? T : F, false);
    n += item(buf + n, sizeof(buf) - n, "Midi Start-Stop Message Behavior", tt, false);
    n += item(buf + n, sizeof(buf) - n, "Delay Buffer Unfreeze Mute", opts_.delay_mute ? T : F, false);
    n += item(buf + n, sizeof(buf) - n, "Input Source", is, true);
    snprintf(buf + n, sizeof(buf) - n, "\t]\n}");
    options_snapshot_ = buf;
    options_ready_.store(true, std::memory_order_release);
}

/* ---- audio ---------------------------------------------------------------- */

void MunchiTempo::ProcessHostBlock(const int16_t *in, int16_t *out, int frames)
{
    for(int i = 0; i < frames; i++)
    {
        up_.Push(in[i * 2] * (1.f / 32768.f), in[i * 2 + 1] * (1.f / 32768.f));
        while(up_.CanPull())
        {
            float l, r;
            up_.Pull(&l, &r);
            // CHOMPI inputs: 0 = mic, 1 = unused, 2/3 = aux L/R. Move has one
            // stereo input that is the mic until a cable is in; both feeds
            // read it. Move's internal mic arrives on the left channel only;
            // a sum keeps a centred source whole either way.
            in48_[0][in48_count_] = l + r;
            in48_[1][in48_count_] = 0.f;
            in48_[2][in48_count_] = l;
            in48_[3][in48_count_] = r;
            if(++in48_count_ == kEngineBlock)
            {
                RunEngineBlock();
                in48_count_ = 0;
            }
        }
    }
    int n = frames < out44_count_ ? frames : out44_count_;
    for(int i = 0; i < n; i++)
    {
        out[i * 2]     = (int16_t)f2s16(out44_l_[i]);
        out[i * 2 + 1] = (int16_t)f2s16(out44_r_[i]);
    }
    for(int i = n; i < frames; i++)
        out[i * 2] = out[i * 2 + 1] = 0;
    memmove(out44_l_, out44_l_ + n, (out44_count_ - n) * sizeof(float));
    memmove(out44_r_, out44_r_ + n, (out44_count_ - n) * sizeof(float));
    out44_count_ -= n;
}

/* chompi_main MainLoop, once the card is in */
void MunchiTempo::OnReady()
{
    for(int i = 0; i < 2; ++i)
        engines_[i]->setSample(14);
    ready_     = true;
    init_time_ = daisy::System::now_ms;
}

void MunchiTempo::RunEngineBlock()
{
    if(!ready_ && loaded_.load(std::memory_order_acquire))
        OnReady();
    if(!ready_)
    {
        for(int i = 0; i < kEngineBlock; i++)
            down_.Push(0.f, 0.f);
    }
    else
    {
        UiTick();
        clock_.averageMidiClock();

        // MidiClockCallback (TIM16): tempo * 12 / 60 Hz, between blocks
        const double hz = 1000000.0 / (double)(timer_.period + 1);
        timer_phase_ += hz * kEngineBlock / 48000.0;
        while(timer_phase_ >= 1.0)
        {
            timer_phase_ -= 1.0;
            if(arp_.getPlay(0) && opts_.midi_clock_out && clock_.getClockMode() == FREE)
                hw_.queueMidiClock(0);
            clock_.incrementCounters();
            delay_.setClockPulse();
        }

        // AudioCallback
        if(clock_.checkIntervalExpired(0))
        {
            arp_.setClockEdge(0);
            delay_.setClockEdge();
        }
        if(clock_.checkIntervalExpired(1))
            arp_.setClockEdge(1);
        clock_.checkIntervalExpired(2);
        arp_.Prepare();
        for(int i = 0; i < 2; ++i)
            engines_[i]->Prepare();

        const float *ins[4]   = {in48_[0], in48_[1], in48_[2], in48_[3]};
        float       *outs[4]  = {out48_[0], out48_[1], out48_[2], out48_[3]};
        float       *chroma[2] = {chroma48_[0], chroma48_[1]};
        float       *slice[2]  = {slice48_[0], slice48_[1]};
        engines_[0]->Process(ins, chroma, kEngineBlock);
        engines_[1]->Process(ins, slice, kEngineBlock);
        for(int i = 0; i < kEngineBlock; ++i)
        {
            out48_[0][i] = chroma48_[0][i] + slice48_[0][i];
            out48_[1][i] = chroma48_[1][i] + slice48_[1][i];
        }
        fx_.Process(ins, outs, kEngineBlock, chroma, slice);
        fx_.ApplyOutputFX(ins, outs, kEngineBlock);

        // Munchi: a full buffer stops the recording inside fxEngine, which
        // skipped what releasing the CHOMPI key does -- load the take
        if(was_recording_ && !fx_.getRecording())
            StopRecordingAndLoad();
        was_recording_ = fx_.getRecording();

        // channels 0/1 are the CHOMPI's headphone out, which carries the
        // input monitor in every mode; Move has one output
        for(int i = 0; i < kEngineBlock; i++)
            down_.Push(daisysp::SoftLimit(out48_[0][i]), daisysp::SoftLimit(out48_[1][i]));
    }
    while(down_.CanPull() && out44_count_ < 1024)
    {
        down_.Pull(&out44_l_[out44_count_], &out44_r_[out44_count_]);
        out44_count_++;
    }
    daisy::System::now_ms++;
}

void MunchiTempo::UiTick()
{
    const uint32_t now = daisy::System::now_ms;
    if(init_ignore_ && now - init_time_ > 1500)
        init_ignore_ = false;

    // NormalPage::Draw: the switch decides input monitoring
    fx_.SetInputMonitor(!switch_state_);
    frozen_ = delay_.getBufferLock();

    // ui.h GenerateEvents
    if(menu_active_ && MenuIsClosable())
        menu_active_ = false;
    if(record_reset_)
    {
        // MenuPage::resetAfterRecording
        pan_    = .5f;
        reduce_ = 0.f;
        Eng().setPan(pan_);
        Eng().setSampleReducer(reduce_);
        record_reset_ = false;
    }

    // MenuPage::Draw: holding the current state's key 1 s copies A <-> B
    if(copy_pressed_ && now - copy_time_ > 1000)
    {
        StateSaver::State src = state_.getState();
        saveState(fx_.getEngine(), state_.getState());
        size_t dst = ((fx_.getEngine() << 1) | state_.getState()) ^ 2;
        state_.setPlay(dst, arp_.getPlay(fx_.getEngine() ^ 1));
        state_.Copy(src);
        copy_pressed_    = false;
        snapshot_copied_ = true;
    }

    // TestPresets() every 5 s, written by the worker
    if(now - presets_check_t_ > 5000)
    {
        presets_check_t_ = now;
        if(presets_.updated && !presets_ready_.load() && writer_run_.load())
        {
            presets_snapshot_ = presets_.Serialize();
            presets_.updated  = false;
            presets_ready_.store(true, std::memory_order_release);
        }
    }
}

/* ---- NormalPage ----------------------------------------------------------- */

void MunchiTempo::Button(int id, bool rising)
{
    if(menu_active_ && MenuButton(id, rising))
        return;
    NormalButton(id, rising);
}

void MunchiTempo::StopRecordingAndLoad()
{
    // NormalPage KEY_26, the stop half
    fx_.StopRecording();
    was_recording_ = false;
    engines_[fx_.getEngine()]->setSample(14);
    if(fx_.getEngine() == CHROMATIC && engines_[SLICE]->getVoiceSlot() == 15)
        engines_[SLICE]->setSample(14);
    enc_values_[0][0] = enc_defaults[0][0];
    enc_values_[1][0] = enc_defaults[1][0];
    enc_values_[0][1] = enc_defaults[0][1];
    enc_values_[0][2] = enc_defaults[0][2];
    enc_values_[2][0] = enc_defaults[2][0];
    for(size_t i = 0; i < kNumEngines; ++i)
    {
        // (the firmware's loop reset the CURRENT engine twice; kept)
        BaseEngine *e = engines_[fx_.getEngine()];
        e->resetGlobalPitchQuant();
        e->setGlobalPitchFree(enc_values_[0][0]);
        e->setSampleVolume(enc_values_[1][0]);
        e->setStartPoint(enc_values_[0][1]);
        e->setEndPoint(enc_values_[0][2]);
        e->setMasterCutoff(enc_values_[2][0]);
    }
    record_reset_ = true;
}

void MunchiTempo::NormalButton(int buttonID, bool rising)
{
    if(init_ignore_ || !ready_)
        return;
    const size_t eng = fx_.getEngine();
    switch(buttonID)
    {
        case NC_1:
        case NC_2:
        case NC_3:
        case NC_4:
        case NC_5:
        case ENC_1_SW:
        case ENC_2_SW:
        case ENC_4_SW:
        case ENC_6_SW:
        case SW_TOG: break;

        case ENC_3_SW: // the magic knob: freeze (short press)
            if(!rising)
            {
                if(knob_page_[3] == 0)
                    fx_.toggleGranularFreeze();
                else
                    knob_page_[3] = 0;
            }
            break;

        case ENC_5_SW:
            if(!rising)
            {
                hw_.queueMidiCC(opts_.midi_ch_out_chroma + 0 * eng, cc_map[0][4],
                                (uint8_t)(enc_values_[0][4] * 127.f));
                transport_held_ = false;
            }
            else
            {
                enc_values_[0][4] = clock_.processTapClock(enc_values_[0][4]);
                transport_held_   = true;
            }
            break;

        case KEY_27: // play
            arp_.setPlay(rising);
            break;

        case KEY_28: // loop
            arp_.setLatch(rising);
            hw_.queueMidiCC(eng ? opts_.midi_ch_out_slice : opts_.midi_ch_out_chroma, 15,
                            rising ? 127 : 0);
            break;

        case KEY_26:
            chompi_key_pressed_ = rising;
            if(!switch_state_)
            {
                hw_.queueMidiCC(eng ? opts_.midi_ch_out_slice : opts_.midi_ch_out_chroma, 14,
                                rising ? 127 : 0);
                if(!fx_.getRecording() && rising)
                {
                    fx_.StartNewRecording();
                    was_recording_ = true;
                }
                else if(fx_.getRecording()
                        && ((!rising && !opts_.record_latch) || (rising && opts_.record_latch)))
                    StopRecordingAndLoad();
            }
            break;

        default:
        {
            if(buttonID < 0 || buttonID >= 64)
                break;
            // Munchi: ids 40..55 are the MIDI path's off-keyboard keys
            const float   nn  = buttonID < 40 ? (float)(key_map[buttonID] - 60) : 0.f;
            const uint8_t ch  = eng ? opts_.midi_ch_out_slice : opts_.midi_ch_out_chroma;
            const float   tnn = eng == CHROMATIC ? nn : 0.f;
            if(buttonID < 40 && KeyIdToNote(buttonID) < 0)
                break;
            if(rising)
            {
                if(arp_.getPlay(eng))
                    arp_.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, nn, buttonID, 127.f));
                else if(arp_.getLatch())
                {
                    if(!(arp_.getSustain() && arp_.isKeyInSeq(buttonID, eng)))
                        engines_[eng]->request_fifo.PushBack(
                            KeyRequest(KeyRequest::Type::START, tnn, buttonID, 127.f));
                    arp_.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, nn, buttonID, 127.f));
                }
                else
                    engines_[eng]->request_fifo.PushBack(
                        KeyRequest(KeyRequest::Type::START, tnn, buttonID, 127.f));
                if(!arp_.getPlay(eng))
                    hw_.queueMidiNote(ch, (uint8_t)(nn + 60), 127, NoteOn);
            }
            else
            {
                if(!arp_.getSustain() && !arp_.checkNotePlaying(nn))
                    engines_[eng]->request_fifo.PushBack(
                        KeyRequest(KeyRequest::Type::STOP, tnn, buttonID, 127.f));
                if(arp_.getPlay(eng) || (arp_.getLatch() && !arp_.getSustain()))
                    arp_.request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, nn, buttonID, 127.f));
                if(!arp_.getPlay(eng) && !arp_.getSustain())
                    hw_.queueMidiNote(ch, (uint8_t)(nn + 60), 127, NoteOff);
            }
            break;
        }
    }
}

void MunchiTempo::Encoder(int knob, int page, int turns, bool menu)
{
    if(knob < 0 || knob > 5 || turns == 0)
        return;
    knob_page_[knob] = (uint8_t)page;
    int t = (knob == 0 || knob == 4) ? turns : turns * 3;
    if(menu)
    {
        if(!menu_active_)
            MenuFocusGained(); // the menu's state, without opening the page
        MenuEncoder(knob, t);
    }
    else
        NormalEncoder(knob, t, 0);
}

void MunchiTempo::NormalEncoder(int encoderID, int turns, int stepsPerRevolution)
{
    if(init_ignore_ || !ready_)
        return;
    const size_t eng  = fx_.getEngine();
    const int    page = knob_page_[encoderID];
    if(stepsPerRevolution > 0)
    {
        enc_values_[page][encoderID] = turns / 127.f;
        if(encoderID == 4)
        {
            size_t tempo = 160 + enc_values_[0][4] * 320;
            clock_.setTempo(tempo);
            return;
        }
    }
    else
    {
        const bool quantized = !opts_.pitch_shift_quantization; // NormalPage's sense
        float      inc       = turns * kEncoderCoarseStep;
        if((encoderID == 0 && page == 0 && !quantized) || (encoderID == 3 && page == 0))
            inc = turns * kEncoderFineStep;
        else if(encoderID == 4 && page == 0)
            inc = turns * kEncoderTempoStep;
        else if(encoderID == 0 && page == 1)
            inc = turns * kEncoderCycleStep;
        else if((encoderID == 1 && page == 0) || (encoderID == 2 && page == 0))
        {
            if(eng == CHROMATIC)
            {
                float range = enc_values_[0][2] - enc_values_[0][1];
                float step;
                if(range > .05f)
                    step = kEncoderFineStep;
                else
                {
                    float t      = range / 0.05f;
                    float curved = powf(t, 3.5f);
                    step         = curved * kEncoderCoarseStep;
                }
                step = daisysp::fclamp(step, 0.0001f, 1.f);
                inc  = turns * step;
            }
            else
                inc = turns * kEncoderFineStep;
        }
        else if(encoderID == 0 && page == 0 && quantized)
            inc = 0.f;
        enc_values_[page][encoderID] += inc;
    }
    enc_values_[page][encoderID] = daisysp::fclamp(enc_values_[page][encoderID], 0.f, 1.f);

    BaseEngine *e = engines_[eng];
    switch(encoderID)
    {
        case 0:
            if(page == 0)
            {
                if(!opts_.pitch_shift_quantization)
                    enc_values_[0][0] = e->setGlobalPitchQuantized(turns, enc_values_[0][0]);
                else
                    e->setGlobalPitchFree(enc_values_[0][0]);
            }
            else if(page == 1)
                e->setSampleVolume(powf(enc_values_[1][0], 2.0f));
            else
                e->setMasterCutoff(enc_values_[2][0]);
            break;
        case 1:
            if(page == 0)
            {
                enc_values_[0][1]
                    = daisysp::fclamp(enc_values_[0][1], 0, enc_values_[0][2] - kEncoderFineStep);
                e->setStartPoint(enc_values_[0][1]);
            }
            else
                e->setAttack(enc_values_[1][1]);
            break;
        case 2:
            if(page == 0)
            {
                enc_values_[0][2]
                    = daisysp::fclamp(enc_values_[0][2], enc_values_[0][1] + kEncoderFineStep, 1.f);
                e->setEndPoint(enc_values_[0][2]);
            }
            else
                e->setRelease(enc_values_[1][2]);
            break;
        case 3:
            if(page == 0)
                fx_.setGranularMain(enc_values_[0][3]);
            else
                fx_.setGranularMix(enc_values_[1][3], eng);
            break;
        case 4:
            if(clock_.getClockMode() == FREE)
            {
                if(transport_held_)
                    clock_.changeDiv(turns, eng);
                else
                    clock_.changeTempo(turns);
            }
            else
                clock_.changeDiv(turns, eng);
            break;
        case 5:
            if(page == 0)
                fx_.setGain(enc_values_[0][5]);
            else
                fx_.setInputGain(enc_values_[1][5]);
            break;
    }
    if(stepsPerRevolution == 0)
        hw_.queueMidiCC(eng ? opts_.midi_ch_out_slice : opts_.midi_ch_out_chroma,
                        cc_map[page][encoderID], (uint8_t)(enc_values_[page][encoderID] * 127));
}

/* ---- Munchi entry points -------------------------------------------------- */

void MunchiTempo::MenuKey(bool rising)
{
    // ui.h opened the menu page on a CHOMPI-key press in play mode and then
    // delivered that press to it; the menu's KEY_26 returns false, so the
    // play page saw it too
    if(rising && !menu_active_)
    {
        menu_active_ = true;
        MenuFocusGained();
    }
    if(menu_active_ && MenuButton(KEY_26, rising))
        return;
    bool saved    = switch_state_;
    switch_state_ = true;
    NormalButton(KEY_26, rising);
    switch_state_ = saved;
}

void MunchiTempo::RecordKey(bool rising)
{
    // NormalPage KEY_26 with the switch in record mode
    bool saved    = switch_state_;
    switch_state_ = false;
    NormalButton(KEY_26, rising);
    switch_state_ = saved;
}

void MunchiTempo::MenuPress(int sw)
{
    if(!menu_active_)
        MenuFocusGained();
    MenuButton(sw, true);
    MenuButton(sw, false);
}

void MunchiTempo::Click(int sw)
{
    MenuPress(sw);
}

void MunchiTempo::TapTempo()
{
    NormalButton(ENC_5_SW, true);
    NormalButton(ENC_5_SW, false);
}

void MunchiTempo::Freeze()
{
    knob_page_[3] = 0;
    NormalButton(ENC_3_SW, true);
    NormalButton(ENC_3_SW, false);
}

void MunchiTempo::Division(int turns)
{
    if(!ready_ || turns == 0)
        return;
    clock_.changeDiv(turns, fx_.getEngine());
}

void MunchiTempo::ToggleClockMode()
{
    clock_.toggleClockMode();
}

void MunchiTempo::ToggleLoop()
{
    loop_ = loop_ > .5f ? 0.f : 1.f;
    Eng().setLoop(loop_);
}

void MunchiTempo::ToggleSustain()
{
    sustain_ = sustain_ > .5f ? 0.f : 1.f;
    Eng().setSustain(sustain_);
}

void MunchiTempo::SetSwitch(bool play)
{
    // leaving record mode mid-take ends the take, as the CHOMPI key would
    if(play && !switch_state_ && fx_.getRecording())
        StopRecordingAndLoad();
    switch_state_ = play;
}

void MunchiTempo::SelectSlot(int slot)
{
    if(slot < 1 || slot > 15 || !Eng().isValidSample(slot))
        return;
    selected_slot_ = (uint8_t)slot;
    SetVoiceSlot(slot - 1);
}

/* ---- MenuPage ------------------------------------------------------------- */

void MunchiTempo::MenuFocusGained()
{
    menu_chompi_pressed_ = true;
    copy_src_            = kSlotNone;
    preset_mode_         = PM_NONE;
}

bool MunchiTempo::MenuIsClosable()
{
    return daisy::System::now_ms - blink_startt_ > 1000
           && (preset_mode_ == PM_SAVING || preset_mode_ == PM_COPYING
               || preset_mode_ == PM_ERASING
               || (preset_mode_ == PM_NONE && !menu_chompi_pressed_));
}

void MunchiTempo::MenuEncoder(int encoderID, int turns)
{
    if(!ready_ || preset_mode_ != PM_NONE)
        return;
    const int   page = knob_page_[encoderID];
    BaseEngine *e    = engines_[fx_.getEngine()];
    float       inc  = turns * kEncoderCoarseStep;
    const bool  quantized = opts_.pitch_shift_quantization; // MenuPage's sense
    if(encoderID == 0 && page == 0 && !quantized)
        inc = turns * kEncoderFineStep;

    switch(encoderID)
    {
        case 0:
            if(page == 0) // stepped pitch
            {
                if(quantized)
                    enc_values_[0][0] = e->setGlobalPitchQuantized(turns, enc_values_[0][0]);
                else
                {
                    enc_values_[0][0] = daisysp::fclamp(enc_values_[0][0] + inc, 0.f, 1.f);
                    e->setGlobalPitchFree(enc_values_[0][0]);
                }
            }
            else if(page == 1) // pan
            {
                pan_ = daisysp::fclamp(pan_ + inc, 0.f, 1.f);
                e->setPan(pan_);
            }
            else // sample rate reducer
            {
                reduce_ = daisysp::fclamp(reduce_ + inc, 0.f, 1.f);
                e->setSampleReducer(reduce_);
            }
            break;
        case 1:
            if(page == 0) // slide the window
            {
                float pre_range = enc_values_[0][2] - enc_values_[0][1];
                float range     = fminf(pre_range, .01f) * turns;
                enc_values_[0][1] += range;
                enc_values_[0][2] += range;
                enc_values_[0][1] = daisysp::fclamp(enc_values_[0][1], 0.f, 1.f - pre_range);
                enc_values_[0][2] = daisysp::fclamp(enc_values_[0][2], pre_range, 1.f);
                e->setStartPoint(enc_values_[0][1]);
                e->setEndPoint(enc_values_[0][2]);
            }
            else
            {
                loop_ = daisysp::fclamp(loop_ + inc, 0.f, 1.f);
                e->setLoop(loop_);
            }
            break;
        case 2:
            if(page == 0) // halve / double the window
            {
                window_encoder_counter_ += turns;
                if(window_encoder_counter_ < -6 || window_encoder_counter_ > 6)
                {
                    window_encoder_counter_ = 0;
                    float start = enc_values_[0][1], end = enc_values_[0][2];
                    float window = end - start;
                    if(turns < 0)
                        enc_values_[0][2] = start + window * 0.5f;
                    else
                        enc_values_[0][2] = daisysp::fclamp(start + window * 2.0f,
                                                            start + kEncoderFineStep, 1.0f);
                    enc_values_[0][2]
                        = daisysp::fclamp(enc_values_[0][2], start + kEncoderFineStep, 1.f);
                    e->setEndPoint(enc_values_[0][2]);
                }
            }
            else
            {
                sustain_ = daisysp::fclamp(sustain_ + inc, 0.f, 1.f);
                e->setSustain(sustain_);
            }
            break;
        case 3:
            if(page == 0) // granular randomness
            {
                randomness_ = daisysp::fclamp(randomness_ + inc, 0.f, 1.f);
                fx_.setGranularAlt(randomness_);
            }
            else
            {
                feedback_ = daisysp::fclamp(feedback_ + inc, 0.f, 1.f);
                fx_.setGranularFeedback(feedback_);
            }
            break;
        case 4:
            arp_randomness_ = daisysp::fclamp(arp_randomness_ + inc, 0.f, 1.f);
            arp_.setRandomness(arp_randomness_, fx_.getEngine());
            break;
        case 5:
            final_comp_ = daisysp::fclamp(final_comp_ + inc, 0.f, 1.f);
            fx_.setFinalComp(final_comp_);
            break;
    }
}

void MunchiTempo::DumpValuePresets(uint8_t slot)
{
    size_t mode = fx_.getEngine();
    presets_.SetValue(enc_values_[0][0], mode, slot, 0); // pitch
    presets_.SetValue(enc_values_[1][0], mode, slot, 1); // sample volume
    presets_.SetValue(enc_values_[2][0], mode, slot, 2); // filter
    presets_.SetValue(enc_values_[0][1], mode, slot, 3); // sample start
    presets_.SetValue(enc_values_[1][1], mode, slot, 4); // attack
    presets_.SetValue(enc_values_[0][2], mode, slot, 5); // sample end
    presets_.SetValue(enc_values_[1][2], mode, slot, 6); // release
    presets_.SetValue(pan_, mode, slot, 7);
    presets_.SetValue(reduce_, mode, slot, 8);
    presets_.SetValue(loop_, mode, slot, 9);
    presets_.SetValue(sustain_, mode, slot, 10);
}

void MunchiTempo::SetVoiceSlot(size_t slot)
{
    size_t mode = fx_.getEngine();
    engines_[mode]->setSample(slot);
    auto v = [&](int c) {
        return slot == 14 ? presets_.getDefault(c) : presets_.GetValue(mode, slot, c);
    };
    enc_values_[0][0] = v(0);
    enc_values_[1][0] = v(1);
    enc_values_[2][0] = v(2);
    enc_values_[0][1] = v(3);
    enc_values_[1][1] = v(4);
    enc_values_[0][2] = v(5);
    enc_values_[1][2] = v(6);
    pan_              = v(7);
    reduce_           = v(8);
    loop_             = v(9);
    sustain_          = v(10);
    BaseEngine *e     = engines_[mode];
    e->setGlobalPitchFree(enc_values_[0][0]);
    e->setSampleVolume(enc_values_[1][0]);
    e->setMasterCutoff(enc_values_[2][0]);
    e->setStartPoint(enc_values_[0][1]);
    e->setAttack(enc_values_[1][1]);
    e->setEndPoint(enc_values_[0][2]);
    e->setRelease(enc_values_[1][2]);
    e->setPan(pan_);
    e->setSampleReducer(reduce_);
    e->setLoop(loop_);
    e->setSustain(sustain_);
}

void MunchiTempo::SwitchEngine(engineSelection to)
{
    // MenuPage KEY_16 / KEY_17
    const engineSelection from = to == CHROMATIC ? SLICE : CHROMATIC;
    fx_.setEngine(to);
    saveState(from, state_.getState());
    recallState(to, state_.getState(), false);
    if(!arp_.getPlay(from) && !arp_.getEngineSustain(from))
    {
        const uint8_t ch = from == SLICE ? opts_.midi_ch_out_slice : opts_.midi_ch_out_chroma;
        for(size_t i = 0; i < NUM_VOICES; ++i)
        {
            float nn = engines_[from]->chokeVoice(i);
            if(nn != -99.f)
                hw_.queueMidiNote(ch, (uint8_t)(nn + 60), 127, NoteOff);
        }
    }
    arp_.setEngine(to == CHROMATIC);
}

bool MunchiTempo::MenuButton(int buttonID, bool rising)
{
    if(!ready_)
        return true;
    BaseEngine *e = engines_[fx_.getEngine()];
    switch(buttonID)
    {
        case NC_1:
        case NC_2:
        case NC_3:
        case NC_4:
        case NC_5: break;

        case ENC_4_SW: // pitch knob: reset this page
            if(rising)
            {
                const int page = knob_page_[0];
                if(page == 0)
                {
                    enc_values_[0][0] = enc_defaults[0][0];
                    e->setGlobalPitchFree(enc_values_[0][0]);
                    e->resetGlobalPitchQuant();
                    pre_quantized_amount_ = .5f;
                }
                else if(page == 1)
                {
                    enc_values_[1][0] = enc_defaults[1][0];
                    pan_              = .5f;
                    e->setSampleVolume(enc_values_[1][0]);
                    e->setPan(pan_);
                }
                else
                {
                    enc_values_[2][0] = enc_defaults[2][0];
                    reduce_           = 0.f;
                    e->setMasterCutoff(enc_values_[2][0]);
                    e->setSampleReducer(reduce_);
                }
            }
            break;

        case ENC_5_SW: break; // held 1 s: ToggleClockMode()

        case ENC_1_SW:
            if(knob_page_[1] == 0)
            {
                enc_values_[0][1] = enc_defaults[0][1];
                e->setStartPoint(enc_values_[0][1]);
            }
            else
            {
                enc_values_[1][1] = enc_defaults[1][1];
                e->setAttack(enc_values_[1][1]);
            }
            break;

        case ENC_2_SW:
            if(knob_page_[2] == 0)
            {
                enc_values_[0][2] = enc_defaults[0][2];
                e->setEndPoint(enc_values_[0][2]);
            }
            else
            {
                enc_values_[1][2] = enc_defaults[1][2];
                e->setRelease(enc_values_[1][2]);
            }
            break;

        case ENC_6_SW: // volume knob: the monitor mode
            if(rising)
                fx_.incrementMonitorMode();
            break;

        case ENC_3_SW: // magic wand: reset the effects
            if(rising)
            {
                if(knob_page_[3] == 0)
                {
                    enc_values_[0][3] = enc_defaults[0][3];
                    fx_.setGranularMain(enc_values_[0][3]);
                    randomness_ = 0.f;
                    fx_.setGranularAlt(randomness_);
                }
                else
                {
                    enc_values_[1][3] = enc_defaults[1][3];
                    fx_.setGranularMix(enc_values_[1][3], fx_.getEngine());
                    feedback_ = .3f;
                    fx_.setGranularFeedback(feedback_);
                }
                fx_.setBufferFreeze(false);
            }
            break;

        case SW_TOG: break;

        case KEY_26: // chompi: confirm
            menu_chompi_pressed_ = rising;
            if(rising && selected_slot_ != kSlotNone)
            {
                const size_t cur = fx_.getEngine();
                if(preset_mode_ == PM_SAVE_SEL)
                {
                    blink_startt_ = daisy::System::now_ms;
                    preset_mode_  = PM_SAVING;
                    DumpValuePresets(selected_slot_);
                    presets_.Save();
                    e->saveFileToSlot(selected_slot_);
                    e->setSample(selected_slot_ - 1);
                }
                else if(preset_mode_ == PM_COPY_DEST)
                {
                    blink_startt_ = daisy::System::now_ms;
                    preset_mode_  = PM_COPYING;
                    DumpValuePresets(selected_slot_);
                    presets_.Save();
                    // Munchi: the firmware passed destination engine 0, so a
                    // copy made in Slice landed in a CHROMATIC slot. The
                    // destination is the engine now showing.
                    if(copy_src_ != selected_slot_ || cs_engine_ != cur)
                        e->copySlot(copy_src_, selected_slot_, cs_engine_, cur);
                }
                else if(preset_mode_ == PM_ERASE_SEL)
                {
                    preset_mode_  = PM_ERASING;
                    blink_startt_ = daisy::System::now_ms;
                    e->deleteSlot(selected_slot_);
                    e->setSample(14);
                }
            }
            return false;

        case KEY_27: // play: the arp pattern
            if(rising)
                arp_.incrementPattern();
            break;

        case KEY_28: // loop: the rest pattern
            if(rising)
                arp_.incrementRestMode();
            break;

        case KEY_16: // chromatic
        case KEY_17: // slice
            if(rising)
            {
                engineSelection to = buttonID == KEY_16 ? CHROMATIC : SLICE;
                if(to != (engineSelection)fx_.getEngine())
                    SwitchEngine(to);
                // Munchi: the firmware re-ran the switch on every press
                // ("This will happen repeatedly (bad)"); pressing the engine
                // already showing does nothing here
            }
            else
                return false;
            break;

        case KEY_18: // mic
        case KEY_19: // line
        case KEY_20: // resample
            if(rising)
            {
                InputSource source = buttonID == KEY_18   ? InputSource::MIC
                                     : buttonID == KEY_19 ? InputSource::LINE_IN
                                                          : InputSource::RESAMPLE;
                fx_.SetInputSource(source);
                opts_.input_source = (int)source;
                SaveOptions();
            }
            else
                return false;
            break;

        case KEY_21: // A
        case KEY_22: // B
        {
            const bool              a    = buttonID == KEY_21;
            const StateSaver::State want = a ? StateSaver::State::A : StateSaver::State::B;
            if(rising)
            {
                if(state_.getState() != want)
                {
                    arp_.setTempNote();
                    saveState(fx_.getEngine(), a ? StateSaver::State::B : StateSaver::State::A);
                    size_t dst = ((fx_.getEngine() << 1) | state_.getState()) ^ 0b10;
                    state_.setPlay(dst, arp_.getPlay(fx_.getEngine() ^ 1));
                    recallState(fx_.getEngine(), want, true);
                    state_.setState(a);
                    arp_.checkTempNote();
                }
                else
                {
                    copy_time_    = daisy::System::now_ms;
                    copy_pressed_ = true;
                }
            }
            else
                copy_pressed_ = false;
            break;
        }

        case KEY_23: // erase
            if(rising)
            {
                if(preset_mode_ == PM_NONE)
                    preset_mode_ = PM_ERASE_SEL;
                else if(preset_mode_ == PM_ERASE_SEL)
                    preset_mode_ = PM_NONE;
                else
                    break;
                selected_slot_ = kSlotNone;
            }
            else
                return false;
            break;

        case KEY_24: // copy
            if(rising)
            {
                if(preset_mode_ == PM_NONE)
                    preset_mode_ = PM_COPY_SRC;
                else if(preset_mode_ == PM_COPY_SRC || preset_mode_ == PM_COPY_DEST)
                    preset_mode_ = PM_NONE;
                else
                    break;
                copy_src_      = kSlotNone;
                selected_slot_ = kSlotNone;
                cs_engine_     = kSlotNone;
            }
            else
                return false;
            break;

        case KEY_25: // save
            if(rising)
            {
                if(preset_mode_ == PM_NONE)
                    preset_mode_ = PM_SAVE_SEL;
                else if(preset_mode_ == PM_SAVE_SEL)
                    preset_mode_ = PM_NONE;
                else
                    break;
                selected_slot_ = kSlotNone;
            }
            else
                return false;
            break;

        default: // white keys
            if(rising)
            {
                int slot_req = KeyToSlot(buttonID);
                if(slot_req == kSlotNone)
                {
                }
                else if(preset_mode_ == PM_COPY_SRC && e->isValidSample(slot_req))
                {
                    copy_src_    = slot_req;
                    cs_engine_   = fx_.getEngine();
                    preset_mode_ = PM_COPY_DEST;
                }
                else if(preset_mode_ == PM_COPY_DEST)
                {
                    // Munchi: 15 is the record buffer, which a copy cannot
                    // write; the firmware let it be picked and then wrote a
                    // sixteenth slot past the end of its table
                    if(slot_req != 15)
                        selected_slot_ = slot_req;
                }
                else if(preset_mode_ == PM_NONE && e->isValidSample(slot_req))
                {
                    selected_slot_ = slot_req;
                    SetVoiceSlot(selected_slot_ - 1);
                }
                else if(preset_mode_ == PM_ERASE_SEL && slot_req != 15 && e->isValidSample(slot_req))
                    selected_slot_ = slot_req;
                else if(preset_mode_ == PM_SAVE_SEL && slot_req != 15)
                    selected_slot_ = slot_req;
            }
            else if(buttonID < 29)
                return false; // allow releasing notes in the menu
            break;
    }
    return true;
}

/* ---- A/B states (MenuPage) ------------------------------------------------ */

void MunchiTempo::InitStatesFromDefault()
{
    SavedState d;
    for(size_t page = 0; page < 2; ++page)
        for(size_t enc = 0; enc < 4; ++enc)
            d.enc_values[page][enc] = enc_values_[page][enc];
    d.filter         = enc_values_[2][0];
    d.pan            = pan_;
    d.redux          = reduce_;
    d.loop           = true;
    d.sustain        = true;
    d.randomness     = randomness_;
    d.feedback       = feedback_;
    d.out_gain       = enc_values_[0][5];
    d.comp           = final_comp_;
    d.in_gain        = enc_values_[1][5];
    d.sample_slot    = 15;
    d.monitor_mode   = fx_.getMonitorMode();
    d.pattern        = 0;
    d.rest_pattern   = 0;
    d.clock_div      = clock_.getClockDivPos(0);
    d.play           = false;
    d.latch_state    = 0;
    d.arp_randomness = arp_randomness_;
    d.sequence       = {};
    for(size_t i = 0; i < StateSaver::StateSlot::LAST; ++i)
        state_.Save(i, &d, nullptr);
}

void MunchiTempo::saveState(size_t eng, StateSaver::State st)
{
    SavedState s;
    for(size_t page = 0; page < 2; ++page)
        for(size_t enc = 0; enc < 4; ++enc)
            s.enc_values[page][enc] = enc_values_[page][enc];
    s.filter         = enc_values_[2][0];
    s.pan            = pan_;
    s.redux          = reduce_;
    s.loop           = loop_;
    s.sustain        = sustain_;
    s.randomness     = randomness_;
    s.feedback       = feedback_;
    s.out_gain       = enc_values_[0][5];
    s.comp           = final_comp_;
    s.in_gain        = enc_values_[1][5];
    s.sample_slot    = engines_[eng]->getVoiceSlot();
    s.monitor_mode   = fx_.getMonitorMode();
    s.pattern        = arp_.getPattern();
    s.rest_pattern   = arp_.getRestMode();
    s.clock_div      = clock_.getClockDivPos(eng);
    s.play           = arp_.getPlay(eng);
    s.latch_state    = arp_.getLatchType(eng);
    s.arp_randomness = arp_randomness_;
    state_.Save(eng * 2 + (size_t)st, &s, arp_.getSequence(eng));
}

void MunchiTempo::recallState(size_t eng, StateSaver::State st, bool apply_params)
{
    size_t      slot = eng * 2 + (size_t)st;
    SavedState *fg   = state_.Recall(slot);
    for(size_t page = 0; page < 2; ++page)
        for(size_t enc = 0; enc < 4; ++enc)
            enc_values_[page][enc] = fg->enc_values[page][enc];
    enc_values_[2][0] = fg->filter;
    reduce_           = fg->redux;
    pan_              = fg->pan;
    loop_             = fg->loop;
    sustain_          = fg->sustain;
    randomness_       = fg->randomness;
    feedback_         = fg->feedback;
    enc_values_[0][5] = fg->out_gain;
    final_comp_       = fg->comp;
    enc_values_[1][5] = fg->in_gain;
    arp_randomness_   = fg->arp_randomness;
    if(apply_params)
    {
        SavedState *bg = state_.Recall(slot > 1 ? slot - 2 : slot + 2);
        applyEngineParams(eng, fg);
        applyEngineParams(eng ^ 1, bg);
        fx_.setGranularMain(enc_values_[0][3]);
        fx_.setGranularMix(enc_values_[1][3], eng);
        fx_.setGranularAlt(randomness_);
        fx_.setGranularFeedback(feedback_);
        fx_.setGain(enc_values_[0][5]);
        fx_.setFinalComp(final_comp_);
        fx_.setInputGain(enc_values_[1][5]);
        fx_.setMonitorMode(fg->monitor_mode);
        clock_.changeTempo(0);
    }
}

void MunchiTempo::applyEngineParams(size_t eng, SavedState *s)
{
    BaseEngine *e = engines_[eng];
    e->setGlobalPitchFree(s->enc_values[0][0]);
    // Munchi: a slot erased since the state was taken falls back to the
    // buffer rather than pointing a player at a freed region
    size_t slot = s->sample_slot;
    if(slot < 1 || slot > 15 || !e->isValidSample(slot))
        slot = 15;
    e->setSample(slot - 1);
    e->setAttack(s->enc_values[1][1]);
    e->setRelease(s->enc_values[1][2]);
    e->setStartPoint(s->enc_values[0][1]);
    e->setEndPoint(s->enc_values[0][2]);
    e->setMasterCutoff(s->filter);
    e->setPan(s->pan);
    e->setSampleReducer(s->redux);
    e->setSampleVolume(s->enc_values[1][0]);
    e->setLoop(s->loop);
    e->setSustain(s->sustain);
    arp_.setSequence(eng, &s->sequence);
    arp_.setPattern(eng, s->pattern);
    arp_.setRestMode(eng, s->rest_pattern);
    arp_.setPlayDirect(eng, s->play);
    arp_.setLatchDirect(eng, s->latch_state);
    arp_.setRandomness(s->arp_randomness, eng);
    clock_.setDiv(eng, s->clock_div);
}

/* ---- MIDI in (MidiManager::ProcessMidiIn) --------------------------------- */

void MunchiTempo::Midi(const uint8_t *msg, int len)
{
    if(len < 1 || !ready_)
        return;
    const uint8_t st = msg[0];
    if(st >= 0xF8)
    {
        const bool transport_in = opts_.transport_type == 0 || opts_.transport_type == 2;
        if(st == 0xF8)
        {
            if(clock_.getClockMode() == SYNC)
                clock_.processMidiClock();
        }
        else if(st == 0xFA)
        {
            if(clock_.getClockMode() == SYNC && transport_in)
            {
                arp_.setPlayTransport(true);
                clock_.setNow(0);
                clock_.setNow(1);
                clock_.setNow(2);
            }
        }
        else if(st == 0xFC)
        {
            if(clock_.getClockMode() == SYNC && transport_in)
                arp_.setPlayTransport(false);
        }
        return;
    }
    if(len < 3 || (st & 0x0F) != opts_.midi_ch_in)
        return;
    const uint8_t type = st & 0xF0;
    const size_t  eng  = fx_.getEngine();
    if(type == 0x90 && msg[2] > 0)
        NoteRequest(msg[1] - 24, msg[2], true);
    else if(type == 0x80 || (type == 0x90 && msg[2] == 0))
        NoteRequest(msg[1] - 24, 0, false);
    else if(type == 0xB0)
    {
        if(menu_active_ || !opts_.midi_cc_in)
            return;
        uint8_t cc = msg[1], val = msg[2];
        if(cc >= 20 && cc < 26)
            NormalEncoder(cc - 20, val, 1);
        else if(cc == 14 || cc == 15)
        {
            // CC 14 is the CHOMPI key in record mode (the firmware dropped it
            // in play mode; on Move that role is always the Sample button's)
            const uint8_t idx  = cc - 14;
            const bool    last = key_cc_[idx];
            if(val > 84)
                key_cc_[idx] = true;
            else if(val < 42)
                key_cc_[idx] = false;
            if(last == key_cc_[idx])
                return;
            if(cc == 14)
                RecordKey(key_cc_[idx]);
            else
                Button(KEY_28, key_cc_[idx]);
        }
    }
}

void MunchiTempo::NoteIn(int note, bool on)
{
    if(ready_)
        NoteRequest(note - 24, 127, on);
}

/* MidiManager::ProcessMidiIn, NoteOn / NoteOff */
void MunchiTempo::NoteRequest(int key, uint8_t velocity, bool on)
{
    if(key > 48 || key < 0)
        return;
    const size_t eng = fx_.getEngine();
    float        tnn = eng == CHROMATIC ? (float)(key - 36) : 0.f;
    if(on)
    {
        float vel = velocity + 1;
        if(arp_.getPlay(eng))
            arp_.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, key - 36, midi2key[key], vel));
        else if(arp_.getLatch())
        {
            arp_.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, key - 36, midi2key[key], vel));
            engines_[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, tnn, midi2key[key], vel));
        }
        else
            engines_[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, tnn, midi2key[key], vel));
    }
    else
    {
        if(!arp_.getSustain() && !arp_.checkNotePlaying(tnn))
            engines_[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, tnn, midi2key[key], 127.f));
        if(arp_.getPlay(eng) || (arp_.getLatch() && !arp_.getSustain()))
            arp_.request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, tnn, midi2key[key], 127.f));
    }
}

/* ---- Munchi: preset keys and snapshots without the menu layer ------------- */

static int SlotKey(int slot)
{
    for(int id = 0; id < 40; id++)
        if(KeyIdToNote(id) >= 0 && KeyToSlot(id) == slot)
            return id;
    return -1;
}

bool MunchiTempo::DriveMenu(int fn_key, int slot_a, int slot_b)
{
    if(!ready_)
        return false;
    int ka = SlotKey(slot_a), kb = slot_b > 0 ? SlotKey(slot_b) : -1;
    if(ka < 0 || (slot_b > 0 && kb < 0))
        return false;
    menu_active_ = true;
    MenuFocusGained();
    MenuButton(fn_key, true);
    MenuButton(ka, true);
    MenuButton(ka, false);
    if(kb >= 0)
    {
        MenuButton(kb, true);
        MenuButton(kb, false);
    }
    const bool armed = selected_slot_ != kSlotNone;
    MenuButton(KEY_26, true); // confirm
    MenuButton(KEY_26, false);
    menu_active_   = false;
    preset_mode_   = PM_NONE;
    selected_slot_ = kSlotNone;
    return armed;
}

bool MunchiTempo::SaveSlot(int slot)
{
    if(slot < 1 || slot > 14 || !sm_.isValidSample(15, 0))
        return false;
    return DriveMenu(KEY_25, slot, -1);
}

bool MunchiTempo::CopySlot(int src, int dst, int src_engine)
{
    const int cur = (int)fx_.getEngine();
    if(src_engine < 0)
        src_engine = cur;
    if(src < 1 || src > 15 || dst < 1 || dst > 14 || !ready_
       || !engines_[src_engine]->isValidSample(src))
        return false;
    if(src_engine == cur)
        return DriveMenu(KEY_24, src, dst);
    // across engines: the firmware picked the source, then the engine key
    // switched banks under COPY_DEST; this is that confirm, directly
    DumpValuePresets((uint8_t)dst);
    presets_.Save();
    engines_[cur]->copySlot(src, dst, src_engine, cur);
    return true;
}

bool MunchiTempo::EraseSlot(int slot)
{
    if(slot < 1 || slot > 14 || !Eng().isValidSample(slot))
        return false;
    return DriveMenu(KEY_23, slot, -1);
}

void MunchiTempo::SelectEngine(int e)
{
    if(!ready_)
        return;
    MenuButton(e == 0 ? KEY_16 : KEY_17, true);
    MenuButton(e == 0 ? KEY_16 : KEY_17, false);
}

void MunchiTempo::SnapshotKey(bool down)
{
    if(!ready_)
        return;
    // the CURRENT state's key held 1 s copies it over (UiTick); a tap
    // presses the other state's key
    const int cur   = StateIsA() ? KEY_21 : KEY_22;
    const int other = StateIsA() ? KEY_22 : KEY_21;
    if(down)
    {
        snapshot_copied_ = false;
        MenuButton(cur, true);
        return;
    }
    MenuButton(cur, false);
    if(!snapshot_copied_)
    {
        MenuButton(other, true);
        MenuButton(other, false);
    }
}

/* ---- harness -------------------------------------------------------------- */

void MunchiTempo::ScriptCommand(const char *op, const char *a, float b)
{
    if(!strcmp(op, "on"))
    {
        init_ignore_ = false;
        Button(atoi(a), true);
    }
    else if(!strcmp(op, "off"))
        Button(atoi(a), false);
    else if(!strcmp(op, "slot"))
        SelectSlot(atoi(a));
    else if(!strcmp(op, "enc") || !strcmp(op, "menc"))
    {
        int knob = 0, page = 0;
        sscanf(a, "%d.%d", &knob, &page);
        init_ignore_ = false;
        Encoder(knob, page, (int)b, op[0] == 'm');
    }
    else if(!strcmp(op, "menukey"))
        MenuKey(atoi(a) != 0);
    else if(!strcmp(op, "rec"))
    {
        init_ignore_ = false;
        RecordKey(atoi(a) != 0);
    }
    else if(!strcmp(op, "press"))
        MenuPress(atoi(a));
    else if(!strcmp(op, "tap"))
        TapTempo();
    else if(!strcmp(op, "freeze"))
        Freeze();
    else if(!strcmp(op, "div"))
        Division(atoi(a));
    else if(!strcmp(op, "clockmode"))
        ToggleClockMode();
    else if(!strcmp(op, "switch"))
        SetSwitch(atoi(a) != 0);
    else if(!strcmp(op, "hp"))
        SetHeadphones(atoi(a) != 0);
    else if(!strcmp(op, "src"))
    {
        opts_.input_source = !strcmp(a, "line") ? 1 : !strcmp(a, "resample") ? 2 : 0;
        fx_.SetInputSource((InputSource)opts_.input_source);
    }
    else if(!strcmp(op, "save"))
        SaveSlot(atoi(a));
    else if(!strcmp(op, "copy"))
        CopySlot(atoi(a), (int)b);
    else if(!strcmp(op, "erase"))
        EraseSlot(atoi(a));
    else if(!strcmp(op, "engine"))
        SelectEngine(atoi(a));
    else if(!strcmp(op, "snap"))
        SnapshotKey(atoi(a) != 0);
    else if(!strcmp(op, "note"))
    {
        uint8_t m[3] = {uint8_t(0x90 | opts_.midi_ch_in), (uint8_t)atoi(a),
                        (uint8_t)(b < 0 ? 100 : b)};
        Midi(m, 3);
    }
    else if(!strcmp(op, "noteoff"))
    {
        uint8_t m[3] = {uint8_t(0x80 | opts_.midi_ch_in), (uint8_t)atoi(a), 0};
        Midi(m, 3);
    }
}

} // namespace munchi
