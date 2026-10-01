/* hardware.h -- the slice of TEMPO's Hardware class its engine code uses.
 *
 * The arpeggiator and pages queue MIDI through hw_->queueMidiNote(),
 * queueMidiCC(), queueMidiTransport() and queueMidiClock(); on CHOMPI
 * MidiManager::ProcessMidiOut sent them to the UART and USB ports. Here they
 * land in a ring the standalone loop drains to Move's USB-A port.
 * Single-threaded: the SPI loop writes and reads it.
 */
#pragma once
#include <stdint.h>
#include "daisy.h"

namespace daisy
{
enum MidiMessageType
{
    NoteOff,
    NoteOn,
    ControlChange,
    SystemRealTime,
};
}
using daisy::ControlChange;
using daisy::MidiMessageType;
using daisy::NoteOff;
using daisy::NoteOn;

class Hardware
{
  public:
    struct Msg
    {
        uint8_t bytes[3];
        uint8_t len;
    };

    void setMidiCCOut(bool enable) { midi_cc_out = enable; }

    // MidiManager::ProcessMidiOut sent every note-on at 127, every note-off at 0
    void queueMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, MidiMessageType type)
    {
        (void)velocity;
        if(type == NoteOn)
            Push(0x90 | (channel & 15), note & 127, 127, 3);
        else
            Push(0x80 | (channel & 15), note & 127, 0, 3);
    }

    void queueMidiCC(uint8_t channel, uint8_t control_number, uint8_t value)
    {
        if(!midi_cc_out)
            return;
        Push(0xB0 | (channel & 15), control_number & 127, value > 127 ? 127 : value, 3);
    }

    void queueMidiTransport(uint8_t channel, bool start)
    {
        (void)channel;
        Push(start ? 0xFA : 0xFC, 0, 0, 1);
    }

    void queueMidiClock(uint8_t channel)
    {
        (void)channel;
        Push(0xF8, 0, 0, 1);
    }

    bool Pop(Msg *m)
    {
        if(r_ == w_)
            return false;
        *m = ring_[r_];
        r_ = (r_ + 1) & 255;
        return true;
    }

  private:
    void Push(uint8_t a, uint8_t b, uint8_t c, uint8_t len)
    {
        int w = (w_ + 1) & 255;
        if(w == r_)
            return;
        ring_[w_] = {{a, b, c}, len};
        w_        = w;
    }
    bool midi_cc_out = true;
    Msg  ring_[256];
    int  r_ = 0, w_ = 0;
};
