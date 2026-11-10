// synth.cpp - the voices are mixed in an SDL audio callback, a sample at a time; the game adds voices and moves
// their params from the main thread under the audio device's lock.
#include "synth.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>

namespace Synth {
namespace {
constexpr double PI = 3.14159265358979;
SDL_AudioDeviceID dev = 0;
int rate = 44100;
std::atomic<long long> rendered{0};
std::vector<Voice> voices;
std::vector<Param> buses;                         // buses[0]: the output's own gain
int nextId = 1;
unsigned noiseState = 22222;

float white() { noiseState = noiseState * 1664525u + 1013904223u; return (noiseState >> 8) / 8388608.f - 1.f; }

float polyblep(double t, double dt) {             // softens the jumps in square and saw (less aliasing)
    if (t < dt) { t /= dt; return (float)(t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float)(t * t + t + t + 1.0); }
    return 0.f;
}

float osc(Wave w, double& ph, float f) {
    const double dt = f / rate;
    float s;
    switch (w) {
    case Sine: s = (float)std::sin(ph * 2 * PI); break;
    case Square: s = (ph < 0.5 ? 1.f : -1.f) + polyblep(ph, dt) - polyblep(std::fmod(ph + 0.5, 1.0), dt); break;
    case Sawtooth: s = (float)(2.0 * ph - 1.0) - polyblep(ph, dt); break;
    default: s = (float)(ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph); break;
    }
    ph += dt;
    ph -= std::floor(ph);
    return s;
}

void coeffs(Voice& v, float fc) {
    fc = std::clamp(fc, 10.f, rate * 0.45f);
    const double w0 = 2 * PI * fc / rate, cw = std::cos(w0), sw = std::sin(w0);
    const double Q = v.filter == Bandpass ? std::max(0.05f, v.q) : std::pow(10.0, v.q / 20.0);
    const double alpha = sw / (2 * Q);
    double b0, b1, b2, a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha;
    if (v.filter == Lowpass) { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; }
    else if (v.filter == Highpass) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; }
    else { b0 = alpha; b1 = 0; b2 = -alpha; }
    v.b0 = (float)(b0 / a0); v.b1 = (float)(b1 / a0); v.b2 = (float)(b2 / a0); v.a1 = (float)(a1 / a0); v.a2 = (float)(a2 / a0);
    v.lastFc = fc;
}

void callback(void*, Uint8* stream, int len) {
    float* out = (float*)stream;
    const int frames = len / (int)(sizeof(float) * 2);
    const long long base = rendered.load();
    std::vector<float> mix(buses.size());
    for (int i = 0; i < frames; ++i) {
        const double t = (double)(base + i) / rate;
        std::fill(mix.begin(), mix.end(), 0.f);
        for (Voice& v : voices) {
            if (t < v.start || t >= v.stop) continue;
            float s;
            if (v.wave == Noise) {
                const float n = v.noiseLen * rate;
                if (v.n >= n) { v.stop = 0; continue; }
                s = white() * std::pow(1.f - v.n / n, v.noiseCurve);
            } else if (v.wave == NoiseLoop) s = white();
            else {
                s = osc(v.wave, v.ph, v.freq.at(t));
                if (v.dual) s += osc(v.wave, v.ph2, v.freq2.at(t));
            }
            ++v.n;
            if (v.filter != NoFilter) {
                if ((v.n & 15) == 1 || v.lastFc < 0) {
                    const float fc = v.cutoff.at(t);
                    if (std::fabs(fc - v.lastFc) > 0.01f) coeffs(v, fc);
                }
                const float y = v.b0 * s + v.z1;
                v.z1 = v.b1 * s - v.a1 * y + v.z2;
                v.z2 = v.b2 * s - v.a2 * y;
                s = y;
            }
            const int b = v.bus >= 0 && v.bus < (int)mix.size() ? v.bus : 0;
            mix[b] += s * v.gain.at(t);
        }
        float o = mix[0];
        for (size_t b = 1; b < mix.size(); ++b) o += mix[b] * buses[b].at(t);
        o *= buses[0].at(t);
        o = std::clamp(o, -1.f, 1.f);
        out[2 * i] = out[2 * i + 1] = o;
    }
    rendered += frames;
    const double t = (double)rendered.load() / rate;
    voices.erase(std::remove_if(voices.begin(), voices.end(), [t](const Voice& v) { return v.stop <= t; }), voices.end());
}

Voice* find(int id) {
    for (Voice& v : voices) if (v.id == id) return &v;
    return nullptr;
}
Param* which(Voice& v, Which w) { return w == Gain ? &v.gain : w == Freq ? &v.freq : w == Freq2 ? &v.freq2 : &v.cutoff; }
}  // namespace

float Param::at(double t) const {
    float v = init;
    double tv = 0;
    bool tgt = false;
    double t0 = 0; float v0 = 0, goal = 0, tc = 1;
    auto tval = [&](double x) { return goal + (v0 - goal) * (float)std::exp(-(x - t0) / std::max(1e-4f, tc)); };
    for (const Ev& e : ev) {
        if (e.type == 1 || e.type == 2) {
            if (tgt) { v = tval(tv); tgt = false; }
            if (t < e.t) {
                const double span = e.t - tv;
                const float u = span > 0 ? (float)std::clamp((t - tv) / span, 0.0, 1.0) : 1.f;
                if (e.type == 2) return v + (e.v - v) * u;
                if (v <= 0 || e.v <= 0) return v;
                return v * std::pow(e.v / v, u);
            }
            v = e.v; tv = e.t;
        } else if (e.type == 0) {
            if (t < e.t) return tgt ? tval(t) : v;
            v = e.v; tv = e.t; tgt = false;
        } else {
            if (t < e.t) return tgt ? tval(t) : v;
            v0 = tgt ? tval(e.t) : v; t0 = e.t; goal = e.v; tc = e.tc; tgt = true; tv = e.t;
        }
    }
    return tgt ? tval(t) : v;
}

void Param::retarget(float v, double t, float tc) {
    const float cur = at(t);
    ev.clear();
    init = cur;
    if (tc <= 0) { init = v; return; }
    ev.push_back({3, t, v, tc});
}

bool open() {
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = callback;
    dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev) return false;
    rate = have.freq;
    buses.assign(1, Param(1.f));
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

void close() {
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
}

bool ready() { return dev != 0; }
double now() { return (double)rendered.load() / rate; }

int addBus(float gain) {
    if (!dev) return 0;
    SDL_LockAudioDevice(dev);
    buses.push_back(Param(gain));
    const int b = (int)buses.size() - 1;
    SDL_UnlockAudioDevice(dev);
    return b;
}

void busGain(int bus, float v, float tc) {
    if (!dev || bus < 0 || bus >= (int)buses.size()) return;
    SDL_LockAudioDevice(dev);
    buses[bus].retarget(v, now(), tc);
    SDL_UnlockAudioDevice(dev);
}

int play(const Voice& v) {
    if (!dev) return 0;
    SDL_LockAudioDevice(dev);
    voices.push_back(v);
    voices.back().id = nextId++;
    const int id = voices.back().id;
    if (voices.size() > 400) voices.erase(voices.begin());          // never let it run away
    SDL_UnlockAudioDevice(dev);
    return id;
}

void target(int id, Which w, float v, float tc) {
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    if (Voice* p = find(id)) which(*p, w)->retarget(v, now(), tc);
    SDL_UnlockAudioDevice(dev);
}

void set(int id, Which w, float v) { target(id, w, v, 0.f); }

void tone(Wave type, float f0, float f1, double at, float len, float vol, int bus) {
    Voice v;
    v.wave = type;
    v.freq = Param(f0); v.freq.set(f0, at).expTo(std::max(1.f, f1), at + len);
    v.gain = Param(vol); v.gain.set(vol, at).expTo(0.001f, at + len);
    v.start = at; v.stop = at + len + 0.02;
    v.bus = bus;
    play(v);
}

void noise(double at, float len, float vol, float freq, Filter type, int bus, float curve) {
    Voice v;
    v.wave = Noise;
    v.noiseLen = len; v.noiseCurve = curve;
    v.filter = type; v.cutoff = Param(freq);
    v.gain = Param(vol);
    v.start = at; v.stop = at + len + 0.01;
    v.bus = bus;
    play(v);
}

float rand01() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    return std::uniform_real_distribution<float>(0.f, 1.f)(rng);
}
}  // namespace Synth
