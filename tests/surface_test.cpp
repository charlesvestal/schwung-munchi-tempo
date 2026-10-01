/* surface_test.cpp -- Move controls in, TEMPO state out, no device.
 * Turns every knob on both pages (and the Shift gestures) through Surface as
 * the SPI loop delivers them, checks the labelled parameter moved, drives the
 * step-button preset gestures, and prints the screen as ASCII.
 *
 *   surface_test <factory card> <scratch card>
 */
#include "../src/standalone/surface.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

using namespace munchi;

static MunchiTempo eng;
static int16_t     in[256], out[256];

static void run_ms(int ms)
{
    for(int i = 0; i < ms * 441 / 1280 + 1; i++)
        eng.ProcessHostBlock(in, out, 128);
}

static void dump(Surface &s)
{
    const uint8_t *px = s.TestScreen();
    for(int y = 0; y < 64; y += 2)
    {
        for(int x = 0; x < 128; x++)
        {
            int a = px[y * 128 + x], b = px[(y + 1) * 128 + x];
            putchar(a && b ? '#' : a ? '"' : b ? '.' : ' ');
        }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    if(argc < 3)
    {
        fprintf(stderr, "usage: surface_test <factory card> <scratch card>\n");
        return 2;
    }
    eng.Init();
    eng.StartCard(argv[2], argv[1]);
    Surface s(eng);
    for(int i = 0; i < 400 && !eng.Ready(); i++)
    {
        run_ms(10);
        usleep(5000);
    }
    run_ms(1600);
    int  fails = 0;
    auto cc    = [&](int c, int v) {
        s.HandleInternal(0xB0, c, v);
        run_ms(10);
    };
    auto turn = [&](int knob, int d) {
        s.HandleInternal(0xB0, knob == 8 ? 79 : 71 + knob, d > 0 ? d : 128 + d);
        run_ms(20);
    };
    auto step = [&](int st) {
        s.HandleInternal(0x90, 16 + st, 127);
        run_ms(10);
        s.HandleInternal(0x80, 16 + st, 0);
        run_ms(10);
    };
    auto expect = [&](const char *what, float a, float b) {
        bool ok = a != b;
        printf("%-26s %.3f -> %.3f %s\n", what, a, b, ok ? "ok" : "DID NOT MOVE");
        fails += !ok;
    };
    auto check = [&](const char *what, bool ok) {
        printf("%-26s %s\n", what, ok ? "ok" : "FAILED");
        fails += !ok;
    };
    float v;
    // page 1: the sample
    v = eng.EncValue(0, 0); turn(0, 20); expect("p1 k1 speed", v, eng.EncValue(0, 0));
    v = eng.EncValue(0, 1); turn(1, 5); expect("p1 k2 start", v, eng.EncValue(0, 1));
    v = eng.EncValue(0, 2); turn(2, -5); expect("p1 k3 end", v, eng.EncValue(0, 2));
    v = eng.EncValue(1, 1); turn(3, 5); expect("p1 k4 attack", v, eng.EncValue(1, 1));
    v = eng.EncValue(1, 2); turn(4, 5); expect("p1 k5 release", v, eng.EncValue(1, 2));
    v = eng.EncValue(1, 0); turn(5, -5); expect("p1 k6 level", v, eng.EncValue(1, 0));
    v = eng.EncValue(2, 0); turn(6, 10); expect("p1 k7 filter", v, eng.EncValue(2, 0));
    v = eng.Reduce(); turn(7, 10); expect("p1 k8 lofi", v, eng.Reduce());
    v = eng.EncValue(0, 5); turn(8, -5); expect("volume knob", v, eng.EncValue(0, 5));
    dump(s);
    // page 2: effects and pattern
    cc(63, 127);
    v = eng.EncValue(0, 3); turn(0, -20); expect("p2 k1 delay", v, eng.EncValue(0, 3));
    v = eng.EncValue(1, 3); turn(1, -5); expect("p2 k2 fx mix", v, eng.EncValue(1, 3));
    v = eng.Randomness(); turn(2, 5); expect("p2 k3 fx random", v, eng.Randomness());
    v = eng.Feedback(); turn(3, 5); expect("p2 k4 feedback", v, eng.Feedback());
    v = eng.Pan(); turn(4, 5); expect("p2 k5 pan", v, eng.Pan());
    v = eng.Clock().getTempo(); turn(5, 5); expect("p2 k6 tempo", v, eng.Clock().getTempo());
    v = eng.ArpRandomness(); turn(6, 5); expect("p2 k7 arp random", v, eng.ArpRandomness());
    v = eng.FinalComp(); turn(7, 5); expect("p2 k8 comp", v, eng.FinalComp());
    dump(s);
    // shift turns
    cc(49, 127);
    v = eng.EncValue(1, 5); turn(8, -5); expect("shift volume: input gain", v, eng.EncValue(1, 5));
    v = eng.Clock().getClockDivPos(0); turn(5, 12); run_ms(600);
    expect("shift k6: division", v, eng.Clock().getClockDivPos(0));
    cc(62, 127);
    v = eng.EncValue(0, 0); turn(0, -8); expect("shift k1: speed step", v, eng.EncValue(0, 0));
    v = eng.EncValue(0, 1); turn(1, 3); expect("shift k2: move window", v, eng.EncValue(0, 1));
    v = eng.EncValue(0, 2); turn(2, -3); expect("shift k3: window size", v, eng.EncValue(0, 2));
    // Shift+Play / Shift+Loop
    v = eng.Arp().getPattern(); cc(85, 127); cc(85, 0); expect("shift play: arp style", v, eng.Arp().getPattern());
    v = eng.Arp().getRestMode(); cc(58, 127); cc(58, 0); expect("shift loop: rests", v, eng.Arp().getRestMode());
    // Shift+Down / Shift+Up: octave (chroma)
    cc(54, 127); cc(54, 0);
    s.HandleInternal(0x90, 68, 100);
    run_ms(30);
    check("octave -1 pad plays via MIDI path", eng.Eng().isKeyPlaying(NoteToKeyId(36)));
    s.HandleInternal(0x80, 68, 0);
    cc(55, 127); cc(55, 0);
    cc(49, 0);
    // engines
    cc(55, 127); cc(55, 0); check("up: slice engine", eng.CurEngine() == 1);
    cc(54, 127); cc(54, 0); check("down: chroma engine", eng.CurEngine() == 0);
    // tracks
    v = eng.LoopVal(); cc(42, 127); cc(42, 0); expect("track 2: loop", v, eng.LoopVal());
    v = eng.SustainVal(); cc(41, 127); cc(41, 0); expect("track 3: sustain", v, eng.SustainVal());
    v = eng.GetSwitch(); cc(40, 127); cc(40, 0); expect("track 4: monitor", v, eng.GetSwitch());
    cc(40, 127); cc(40, 0);
    // steps: slot 3, then copy 3 -> 9 and erase 9
    step(2); check("step 3 selects slot 3", eng.Eng().getVoiceSlot() == 3);
    cc(60, 127); step(2); step(8); cc(60, 0);
    check("copy + 3, 9 copies", eng.SlotValid(9));
    // across engines: source in chroma, Up to slice, destination 12
    cc(60, 127); step(2); cc(55, 127); cc(55, 0); step(11); cc(60, 0);
    check("copy across engines", eng.CurEngine() == 1 && eng.SlotValid(12));
    cc(54, 127); cc(54, 0);
    cc(119, 127); step(8); cc(119, 0);
    check("delete + 9 once keeps it", eng.SlotValid(9));
    cc(119, 127); step(8); step(8); cc(119, 0);
    check("delete + 9, 9 erases", !eng.SlotValid(9));
    // snapshot: tap switches to B
    step(15); check("step 16 tap: snapshot B", !eng.StateIsA());
    step(15); check("step 16 tap: snapshot A", eng.StateIsA());
    // pads play
    s.HandleInternal(0x90, 68, 100);
    run_ms(30);
    check("pad plays a voice", eng.Eng().isKeyPlaying(KEY_1));
    s.HandleInternal(0x80, 68, 0);
    run_ms(300);
    dump(s);
    eng.StopCard();
    printf("%s\n", fails ? "FAILED" : "all controls move what they say");
    return fails;
}
