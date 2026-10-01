/* munchi_tempo.h -- CHOMPI TEMPO 1.0, minus the hardware.
 *
 *   audio      TEMPO's two sample engines (chromatic and slice), the granular
 *              delay, reverb and output stage, at their native 48 kHz,
 *              resampled to and from Move's 44.1 kHz.
 *   controls   NormalPage + MenuPage + ui.h + MidiManager's input side, with
 *              their switch statements kept, driven by CHOMPI key ids.
 *   card       chromatic/, slice/ and buffer/ samples plus presets.json and
 *              options.json in a folder, as on the CHOMPI's SD card. A worker
 *              thread loads it and does every write.
 *
 * One thread (the SPI loop) runs audio and controls, as the firmware did.
 */
#pragma once
#include "daisy_compat.h"
#include "tempo/shim/hardware.h"
#include "tempo/shim/OptionsManager.h"
#include "tempo/clockManager.h"
#include "tempo/SampleEngine.h"
#include "tempo/SliceEngine.h"
#include "tempo/ArpeggiatorSequencer.h"
#include "tempo/FxEngine.h"
#include "tempo/granularDelay.h"
#include "tempo/StateSaver.h"
#include "tempo/reverb.h"
#include "resampler.h"
#include <atomic>
#include <string>
#include <thread>

namespace munchi
{

/* CHOMPI Hardware::SwId */
enum SwId
{
    ENC_1_SW = 0, ENC_2_SW, ENC_3_SW, ENC_4_SW, NC_6,
    KEY_26, // the CHOMPI key
    SW_TOG,
    KEY_16, KEY_2, KEY_3, KEY_4, KEY_5, KEY_17, KEY_18, KEY_19, KEY_1,
    KEY_6, KEY_7, KEY_8, KEY_9, KEY_10, KEY_20, KEY_21, KEY_22,
    KEY_11, KEY_12, KEY_13, KEY_14, KEY_15, KEY_23, KEY_24, KEY_25,
    ENC_6_SW,
    KEY_27, // play
    KEY_28, // loop
    NC_1, NC_2, NC_3, NC_4, NC_5,
    SR_LAST,
};
static constexpr int ENC_5_SW  = 4;
static constexpr int kSlotNone = 100;

int KeyIdToNote(int key_id); // 48..72, or -1
int NoteToKeyId(int note);   // MIDI 24..72 -> key id (off-keyboard ids too)
int KeyToSlot(int key_id);   // white key -> slot 1..15, else kSlotNone

/** PresetManager (TEMPO): 2 engines x 14 slots x 11 controls. */
struct TempoPresets
{
    static constexpr int kModes    = 2;
    static constexpr int kSlots    = 14;
    static constexpr int kControls = 11;
    float presetValues[kModes][kSlots][kControls];
    float preset_defaults[kControls];
    bool  updated = false;

    void  Init(const float *defaults);
    float GetValue(size_t mode, size_t slot, size_t control);           // slot 0-based
    void  SetValue(float value, size_t mode, size_t slot, size_t ctrl); // slot 1-based
    float getDefault(size_t control) { return preset_defaults[control]; }
    void  Save() { updated = true; }
    bool  Parse(const char *json);
    std::string Serialize() const;
};

class MunchiTempo
{
  public:
    MunchiTempo();
    ~MunchiTempo();

    int  Init();
    /** Seeds the card from `factory_dir` on first run, loads it, then keeps
     *  the worker for writes. Returns at once; Ready() says when it is in. */
    void StartCard(const std::string &card_dir, const std::string &factory_dir);
    void StopCard();
    bool Ready() const { return ready_; }
    bool Loading() const { return !ready_; }

    void ProcessHostBlock(const int16_t *in, int16_t *out, int frames);

    /* controls -- CHOMPI key ids and encoder indices, as in the firmware */
    void Button(int sw_id, bool rising);
    void Encoder(int knob, int page, int turns, bool menu);
    void SetKnobPage(int knob, int page) { knob_page_[knob] = (uint8_t)page; }
    void MenuKey(bool rising);   // Shift: the CHOMPI key in play mode
    void RecordKey(bool rising); // Sample: the CHOMPI key in record mode
    void MenuPress(int sw);      // one press + release on the menu page
    void Click(int sw);          // an encoder click with the menu's meaning
    void TapTempo();             // ENC_5_SW on the play page
    void Freeze();               // ENC_3_SW (short) on the play page
    void Division(int turns);    // ENC_5_SW held + turn
    void ToggleClockMode();      // ENC_5_SW held 1 s on the menu page
    void ToggleLoop();           // the menu page's loop knob, as a switch
    void ToggleSustain();        // the menu page's sustain knob, as a switch
    void SelectSlot(int slot);   // a sample slot (1-14) or the buffer (15)
    void SetSwitch(bool play);   // SW_TOG: true = play mode (no monitoring)
    bool GetSwitch() const { return switch_state_; }
    void SetHeadphones(bool hp) { fx_.SetHeadphones(hp); }
    void Midi(const uint8_t *msg, int len);
    /** A note from the keyboard's octave shift: MidiManager's note path,
     *  without the channel filter. `note` 24..72. */
    void NoteIn(int note, bool on);

    /* Munchi: the preset keys without the menu layer. Each drives the
     * MenuPage state machine through its keys, so the result is the
     * firmware's; the menu never stays open over the pads. */
    bool SaveSlot(int slot);          // the buffer into slot 1-14
    bool CopySlot(int src, int dst, int src_engine = -1); // dst == src: commit
    bool EraseSlot(int slot);
    void SnapshotKey(bool down);      // tap: A <-> B; hold 1 s: copy over
    bool SnapshotCopied() const { return snapshot_copied_; }
    bool StateIsA() { return state_.getState() == StateSaver::State::A; }
    void SelectEngine(int e);         // 0 chroma, 1 slice
    bool PopMidiOut(Hardware::Msg *m) { return hw_.Pop(m); }

    /* state for the lights and the screen */
    BaseEngine          &Eng() { return *engines_[fx_.getEngine()]; }
    BaseEngine          &EngineAt(int e) { return *engines_[e]; }
    int                  CurEngine() { return (int)fx_.getEngine(); }
    ArpeggiatorSequencer &Arp() { return arp_; }
    clockManager        &Clock() { return clock_; }
    fxEngine            &Fx() { return fx_; }
    sampleManager       &Samples() { return sm_; }
    StateSaver          &States() { return state_; }
    bool                 MenuActive() const { return menu_active_; }
    int                  PresetMode() const { return preset_mode_; }
    int                  SelectedSlot() const { return selected_slot_; }
    int                  CopySrc() const { return copy_src_; }
    bool                 SlotValid(int s) { return Eng().isValidSample(s); }
    float                EncValue(int page, int knob) const { return enc_values_[page][knob]; }
    float                Pan() const { return pan_; }
    float                Reduce() const { return reduce_; }
    float                LoopVal() const { return loop_; }
    float                SustainVal() const { return sustain_; }
    float                Randomness() const { return randomness_; }
    float                Feedback() const { return feedback_; }
    float                FinalComp() const { return final_comp_; }
    float                ArpRandomness() const { return arp_randomness_; }
    bool                 Frozen() const { return frozen_; }
    bool                 Recording() { return fx_.getRecording(); }
    float                RecordFill() const { return sm_.recordFill(); }
    const OptionsManager &Opts() const { return opts_; }
    OptionsManager      &MutableOpts() { return opts_; }
    void                 OptionsChanged();
    void                 LoadOptions(const std::string &path);
    void                 SaveOptions();
    uint32_t             NowMs() const { return daisy::System::now_ms; }

    void ScriptCommand(const char *op, const char *a, float b);

    enum
    {
        PM_NONE = 0,
        PM_ERASE_SEL,
        PM_ERASING,
        PM_COPY_SRC,
        PM_COPY_DEST,
        PM_COPYING,
        PM_SAVE_SEL,
        PM_SAVING,
    };

  private:
    void RunEngineBlock();
    void UiTick();
    void OnReady();
    void NormalButton(int id, bool rising);
    void NormalEncoder(int enc, int turns, int steps_per_rev);
    bool MenuButton(int id, bool rising);
    void MenuEncoder(int enc, int turns);
    void MenuFocusGained();
    bool MenuIsClosable();
    void StopRecordingAndLoad();
    void SetVoiceSlot(size_t slot);
    void DumpValuePresets(uint8_t slot);
    void InitStatesFromDefault();
    void saveState(size_t eng, StateSaver::State st);
    void recallState(size_t eng, StateSaver::State st, bool apply_params);
    void applyEngineParams(size_t eng, SavedState *state);
    void SwitchEngine(engineSelection e);
    void LoaderMain(std::string factory_dir);
    void NoteRequest(int key, uint8_t vel, bool on);
    bool DriveMenu(int fn_key, int slot_a, int slot_b);
    void WriterTick();

    /* the firmware's objects */
    sampleManager        sm_;
    sampleEngine         chroma_;
    sliceEngine          slice_;
    BaseEngine          *engines_[2] = {&chroma_, &slice_};
    fxEngine             fx_;
    granularDelay        delay_;
    daisysp::Reverb     *reverb_ = nullptr;
    float               *granular_buf_ = nullptr, *frozen_buf_ = nullptr;
    ArpeggiatorSequencer arp_;
    clockManager         clock_;
    daisy::TimerHandle   timer_;
    StateSaver           state_;
    Hardware             hw_;
    OptionsManager       opts_;
    TempoPresets         presets_;

    /* audio plumbing */
    StreamResampler up_, down_;
    static constexpr int kEngineBlock = 48;
    float in48_[4][kEngineBlock];
    float out48_[4][kEngineBlock];
    float chroma48_[2][kEngineBlock], slice48_[2][kEngineBlock];
    int   in48_count_ = 0;
    float out44_l_[1024], out44_r_[1024];
    int   out44_count_ = 0;
    double timer_phase_ = 0.0;
    std::atomic<bool> loaded_{false};
    bool  ready_ = false;
    bool  was_recording_ = false;

    /* ui.h / NormalPage state */
    float   enc_values_[3][6];
    uint8_t knob_page_[6]      = {0, 0, 0, 0, 0, 0};
    bool    switch_state_      = true; // play mode: the CHOMPI key is the menu
    bool    init_ignore_       = true;
    uint32_t init_time_        = 0;
    bool    chompi_key_pressed_ = false;
    bool    transport_held_    = false;
    bool    record_reset_      = false;
    bool    key_cc_[2]         = {false, false};
    bool    frozen_            = false;

    /* MenuPage state */
    bool     menu_active_          = false;
    bool     menu_chompi_pressed_  = false;
    int      preset_mode_          = PM_NONE;
    uint8_t  selected_slot_        = kSlotNone;
    uint8_t  copy_src_             = kSlotNone;
    uint8_t  cs_engine_            = kSlotNone;
    uint32_t blink_startt_         = 0;
    bool     copy_pressed_         = false;
    bool     snapshot_copied_      = false;
    uint32_t copy_time_            = 0;
    int32_t  window_encoder_counter_ = 0;
    float    feedback_ = .3f, randomness_ = 0.f, pan_ = .5f;
    float    loop_ = 1.f, sustain_ = 1.f, reduce_ = 0.f;
    float    final_comp_ = 0.f, arp_randomness_ = 0.f;
    float    pre_quantized_amount_ = .5f;

    /* card */
    std::string       card_dir_;
    std::thread       loader_;
    std::thread       writer_;
    std::atomic<bool> writer_run_{false};
    std::atomic<bool> presets_ready_{false}, options_ready_{false};
    std::string       presets_snapshot_, options_snapshot_;
    uint32_t          presets_check_t_ = 0;
};

} // namespace munchi
