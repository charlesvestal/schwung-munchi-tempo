/* OptionsManager.h -- TEMPO's options.json fields (OptionsManager.h in
 * upstream/). Reading and writing the file is munchi_tempo.cpp's job; the
 * arpeggiator only reads the channels and the transport behaviour. */
#pragma once
#include <stdint.h>
#include <stddef.h>

struct OptionsManager
{
    bool    record_latch             = false;
    uint8_t midi_ch_in               = 0;
    uint8_t midi_ch_out_chroma       = 0;
    uint8_t midi_ch_out_slice        = 1;
    bool    midi_clock_out           = true;
    size_t  monitor_position         = 0;
    bool    pitch_shift_quantization = true;
    bool    midi_cc_in               = true;
    bool    midi_cc_out              = true;
    size_t  transport_type           = 0;
    bool    delay_mute               = false;
    // Munchi
    bool    pad_velocity             = false;
    int     input_source             = 0; // InputSource
};
