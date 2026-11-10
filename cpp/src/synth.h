#pragma once
// synth.h - a small Web Audio stand-in, so the browser game's sound code ports as it is: oscillators (sine, square,
// sawtooth, triangle) and noise, through a biquad filter and a gain, into buses that mix into the output. Every
// frequency, cut-off and gain is a Param with Web Audio's automation: setValueAtTime, exponential and linear ramps,
// setTargetAtTime. Times are in seconds on the audio clock, now().
#include <vector>

namespace Synth {

enum Wave { Sine, Square, Sawtooth, Triangle, Noise, NoiseLoop };   // Noise: a one-shot burst that fades out
enum Filter { NoFilter, Lowpass, Highpass, Bandpass };

struct Param {
    struct Ev { int type; double t; float v; float tc; };
    float init = 0.f;
    std::vector<Ev> ev;
    Param(float v = 0.f) : init(v) {}
    Param& set(float v, double t) { ev.push_back({0, t, v, 0}); return *this; }
    Param& expTo(float v, double t) { ev.push_back({1, t, v, 0}); return *this; }
    Param& linTo(float v, double t) { ev.push_back({2, t, v, 0}); return *this; }
    Param& target(float v, double t, float tc) { ev.push_back({3, t, v, tc}); return *this; }
    float at(double t) const;
    void retarget(float v, double t, float tc);   // collapse to "from where it is now, head for v"
};

struct Voice {
    Wave wave = Sine;
    bool dual = false;                            // a second oscillator on freq2 (two detuned saws, say)
    Param freq{440.f}, freq2{440.f}, gain{1.f}, cutoff{350.f};
    Filter filter = NoFilter;
    float q = 1.f;                                // Web Audio's Q: in dB for low/high-pass, plain for band-pass
    double start = 0, stop = 1e30;
    int bus = 0;                                  // 0 = straight to the output
    float noiseLen = 0.f, noiseCurve = 2.f;       // Noise: length in seconds, and the fade's power
    // playing state
    int id = 0;
    double ph = 0, ph2 = 0;
    float z1 = 0, z2 = 0, b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, lastFc = -1;
    long long n = 0;
};

bool open();                                      // false: no sound device (the game still runs)
void close();
bool ready();
double now();

int addBus(float gain);                           // a bus that mixes into the output
void busGain(int bus, float v, float tc = 0.f);   // bus 0 is the output itself; tc > 0 glides (setTargetAtTime)
int play(const Voice& v);                         // an id, for voices that keep playing (drones)
enum Which { Gain, Freq, Freq2, Cutoff };
void target(int id, Which w, float v, float tc);  // setTargetAtTime(v, now, tc)
void set(int id, Which w, float v);               // param.value = v

// the games' own two helpers
void tone(Wave type, float f0, float f1, double at, float len, float vol, int bus = 0);
void noise(double at, float len, float vol, float freq, Filter type = Lowpass, int bus = 0, float curve = 2.f);

float rand01();                                   // Math.random(), for the game code too
}  // namespace Synth
