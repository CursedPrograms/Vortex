// game.cpp - VORTEX, an Arcade Phantasm game by Cursed Entertainment, ported from the browser's site/game.js and
// site/sfx.js. You're falling through a collapsing dimension. Run along the inside of the tunnel: steering runs you
// round the wall, so "down" keeps changing, and the tunnel turns by itself too. Dodge the blocks, jump the gaps, time
// the spinning bars and grab the energy fragments. The glitchy, flickering blocks turn your left and right around for
// a few seconds. Faster every level; corkscrews from level 3, the tunnel breaks apart from level 4, and from level 5
// you jump between floating pieces of it.
#include "game.h"
#include "splash.h"
#include "synth.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr float PI = 3.14159265358979f;
float frand() { return Synth::rand01(); }            // Math.random()

// ---- the sound (site/sfx.js) ------------------------------------------------------------------------------------
namespace Sfx {
using namespace Synth;
bool awake = false, muted = false;
int musicBus = 0;

void wake() {                                       // the browser starts its audio on the first key or tap
    if (awake || !Synth::ready()) return;
    awake = true;
    busGain(0, muted ? 0.f : 0.35f);
    musicBus = addBus(0.55f);
}

const int BASS[16] = {0, 0, 12, 0, 3, 0, 15, 3, 0, 0, 12, 0, 7, 5, 3, -2};
bool playing = false;
double nextT = 0;
int step = 0;
float bpm = 120;

void schedule() {                                   // the music: a sixteen-step bass line in A minor with a ticking hat
    if (!awake || !playing) return;
    while (nextT < now() + 0.15) {
        const float f = 55.f * std::pow(2.f, BASS[step % 16] / 12.f);
        Voice v;
        v.wave = Sawtooth;
        v.freq = Param(f);
        v.filter = Lowpass;
        v.cutoff.set(900, nextT).expTo(160, nextT + 0.16);
        v.gain.set(0.16f, nextT).expTo(0.001f, nextT + 0.2);
        v.start = nextT; v.stop = nextT + 0.22; v.bus = musicBus;
        play(v);
        if (step % 2 == 1) noise(nextT, 0.03f, 0.05f, 7000, Highpass, musicBus);
        if (step % 8 == 4) noise(nextT, 0.12f, 0.12f, 1800, Bandpass, musicBus);
        if (step % 32 == 0) tone(Triangle, 440, 440, nextT, 0.6f, 0.04f, musicBus);
        nextT += 60.0 / bpm / 4;
        step++;
    }
}

bool toggleMute() {
    muted = !muted;
    Platform::store("vortexMute", muted ? "1" : "0");
    if (awake) busGain(0, muted ? 0.f : 0.35f, 0.02f);
    return muted;
}
bool isMuted() { return muted; }
void music(bool on, int level = 1) {
    bpm = std::min(180.f, 116.f + level * 6.f);
    if (on && !playing) {
        if (!awake) return;
        playing = true; nextT = now() + 0.05; step = 0;
    } else if (!on && playing) playing = false;
}
void jump() {
    if (!awake) return;
    const double t = now();
    tone(Square, 220, 660, t, 0.14f, 0.12f);
    tone(Sine, 330, 990, t, 0.16f, 0.25f);
}
void frag(int combo) {                              // a chime, climbing with each fragment in a row
    if (!awake) return;
    const double t = now();
    const float up = std::pow(2.f, std::min(combo, 12) / 12.f);
    tone(Triangle, 880 * up, 880 * up, t, 0.08f, 0.4f);
    tone(Sine, 1320 * up, 1760 * up, t + 0.05, 0.16f, 0.3f);
}
void hit() {                                        // a crash: a boom and a falling zap
    if (!awake) return;
    const double t = now();
    noise(t, 0.6f, 0.7f, 900);
    tone(Sawtooth, 300, 40, t, 0.5f, 0.3f);
    tone(Square, 1200, 90, t, 0.3f, 0.12f);
}
void fall() {                                       // a long whistle down into the void
    if (!awake) return;
    const double t = now();
    tone(Sine, 1400, 60, t, 1.1f, 0.3f);
    tone(Triangle, 700, 30, t + 0.1, 1.0f, 0.2f);
}
void glitch() {                                     // the glitch: broken bleeps jumping about
    if (!awake) return;
    const double t = now();
    for (int i = 0; i < 9; i++) {
        const float f = 200 + frand() * 2400;
        tone(i % 2 ? Square : Sawtooth, f, f * (0.5f + frand()), t + i * 0.035, 0.05f, 0.12f);
    }
    noise(t, 0.3f, 0.25f, 4000, Bandpass);
}
void unglitch() {
    if (!awake) return;
    tone(Triangle, 660, 990, now(), 0.12f, 0.2f);
}
void level() {                                      // a rising arpeggio through the gate
    if (!awake) return;
    const double t = now();
    const int notes[] = {0, 3, 7, 12, 15, 19, 24};
    for (int i = 0; i < 7; i++) {
        const float f = 330 * std::pow(2.f, notes[i] / 12.f);
        tone(Square, f, f, t + i * 0.06, 0.18f, 0.1f);
    }
    noise(t, 0.8f, 0.15f, 6000, Highpass);
}
void start() {
    if (!awake) return;
    const double t = now();
    tone(Sawtooth, 80, 880, t, 0.5f, 0.12f);
    tone(Sine, 160, 1760, t, 0.5f, 0.15f);
}
void over() {
    if (!awake) return;
    const double t = now();
    tone(Square, 440, 110, t, 1.0f, 0.2f);
    tone(Triangle, 330, 55, t + 0.15, 1.2f, 0.35f);
    noise(t, 1.2f, 0.2f, 500);
}
}  // namespace Sfx

// ---- the game (site/game.js) ------------------------------------------------------------------------------------
// the Cursed colours
namespace C {
const Color navy("#001725"), deep("#005184"), blue("#007ab7"), sky("#34a9e3"), pink("#f088e9"), green("#39ff6a"),
    black("#020611"), white("#ffffff");
}

// the tunnel: N panels round, rows ROW long, VIS rows ahead in view
constexpr int N = 14, VIS = 60;
constexpr float TAU = PI * 2, LA = TAU / N, R = 1, ROW = 0.6f, CAM_BACK = 1.15f, CAM_Y = 0.4f, NEAR = 0.14f;
constexpr float JUMP_V = 3.6f, GRAV = 11.5f, STEER = 2.8f, BLOCK_H = 0.6f, FRAG_H = 0.3f, RUN_HW = 0.07f;
constexpr int LEVEL_ROWS = 260;
constexpr float REVERSE_TIME = 4.5f, CRUMBLE_D = 11, INVULN = 2.2f;
enum { EMPTY = 0, HOLE = 1, BLOCK = 2, GLITCH = 3, CRUMBLE = 4 };
const char* LEVEL_NOTES[] = {"", "Run, jump, collect", "Glitches and spinning bars", "Corkscrews", "The tunnel is breaking up",
                             "Jump between the pieces", "Crossbars", "Faster", "Faster still"};
constexpr int NOTES = 9;

// a seeded random for the track
uint32_t seed = 1;
float rnd() {
    seed += 0x6D2B79F5u;
    uint32_t t = (seed ^ (seed >> 15)) * (1u | seed);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return (float)((t ^ (t >> 14)) / 4294967296.0);
}
int ri(int n) { return (int)std::floor(rnd() * n); }
int wrap(int i) { return ((i % N) + N) % N; }
float mod(float a, float m) { return std::fmod(std::fmod(a, m) + m, m); }
int ld(int a, int b) { const int d = std::abs(wrap(a) - wrap(b)); return std::min(d, N - d); }
float clamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

struct Bar { float ph, sp; bool cross; };
struct Row {
    int c[N] = {}, f[N] = {};
    float twist = 0, cx = 0, cy = 0;
    bool hasBar = false; Bar bar{};
    int gate = 0;
    bool island = false;
    float wob = 0;
};
struct Gen {
    int r = 0, safe = 0, oldSafe = -1, moveAt = -99, lvl = 1, inLvl = 0;
    float twist = 0, twistRate = 0, twistTarget = 0, cx = 0, cy = 0, tcx = 0, tcy = 0;
};
struct Particle { float x, y, vx, vy, life, max; Color c; float s; };
struct Star { float a, r, sp; };
struct Shard { float a, r, rot, vr, s; Color c; };

float W = 0, H = 0, F = 0, CX = 0, CY = 0;
std::unordered_map<int, Row> rows;
Gen gen;
float pz, phi, camRoll, h, vh, spin;
bool grounded;
int level, lives, frags, combo;
float score, comboT, reverseT, invulnT, fallT, time_, bannerT, flashT, shakeT, overT, jumpBuf;
std::string bannerText, bannerSub, state = "attract";
Color flashCol;
struct { bool left = false, right = false; } keys;
std::vector<Particle> particles;
std::vector<Star> stars;
std::vector<Shard> shards;
int best = 0;
bool started_ = false, paused = false, autopilot = false;
bool wasReversed = false;
double last = 0;
float splashFade = 1;
Texture logo;

// ---- the track ------------------------------------------------------------------------------------------------
Row newRow() {
    gen.twistRate += (gen.twistTarget - gen.twistRate) * 0.05f;
    gen.cx += (gen.tcx - gen.cx) * 0.03f;
    gen.cy += (gen.tcy - gen.cy) * 0.03f;
    Row row;
    row.twist = gen.twist; row.cx = gen.cx; row.cy = gen.cy;
    gen.twist += gen.twistRate;
    return row;
}
using RowFn = std::function<void(Row&)>;
void pushRow(const RowFn& fn) {
    Row row = newRow();
    if (fn) fn(row);
    rows[gen.r++] = row;
    gen.inLvl++;
}
void fragAtSafe(Row& r) { r.f[gen.safe] = 1; }
void moveSafe(int by = 0) {
    gen.oldSafe = gen.safe;
    gen.safe = wrap(gen.safe + (by ? by : (rnd() < 0.5f ? -1 : 1)));
    gen.moveAt = gen.r;
}
bool isSafe(int l) { return l == gen.safe || (l == gen.oldSafe && gen.r - gen.moveAt < 5); }

void setTargets() {
    const int L = gen.lvl;
    const bool cork = L >= 3 && rnd() < 0.45f;
    gen.twistTarget = cork ? (rnd() < 0.5f ? -1 : 1) * std::min(0.1f, 0.045f + 0.012f * (L - 3)) : (rnd() - 0.5f) * 0.02f;
    const float bend = std::min(1.f, 0.4f + L * 0.15f) * 0.007f;
    const float a = rnd() * TAU, m = rnd() < 0.2f ? 0 : bend * (0.4f + 0.6f * rnd());
    gen.tcx = std::sin(a) * m;
    gen.tcy = std::cos(a) * m;
}

void patRun() {
    const int k = 12 + ri(6);
    for (int i = 0; i < k; i++) { if (i % 5 == 4) moveSafe(); pushRow(i % 2 == 0 ? RowFn(fragAtSafe) : RowFn()); }
}
void patBlocks() {
    const float p = std::min(0.34f, 0.12f + 0.03f * gen.lvl);
    const int k = 14 + ri(8);
    for (int i = 0; i < k; i++) {
        if (i % 4 == 3 && rnd() < 0.7f) moveSafe();
        pushRow([&](Row& r) {
            if (i % 2) return;
            for (int l = 0; l < N; l++) if (!isSafe(l) && rnd() < p) r.c[l] = BLOCK;
            if (rnd() < 0.35f) r.f[gen.safe] = 1;
        });
    }
}
void patWall() {
    const int n = gen.lvl >= 3 ? 2 : 1;
    for (int w = 0; w < n; w++) {
        for (int i = 0; i < 6; i++) { if (i == 1 || (i == 3 && gen.lvl >= 2)) moveSafe(); pushRow(i == 4 ? RowFn(fragAtSafe) : RowFn()); }
        pushRow([](Row& r) { for (int l = 0; l < N; l++) if (ld(l, gen.safe) > 1) r.c[l] = BLOCK; });
    }
    for (int j = 0; j < 4; j++) pushRow(nullptr);
}
void patGap() {
    for (int i = 0; i < 5; i++) pushRow(nullptr);
    const int g = 1 + ri(std::min(3, gen.lvl));
    for (int j = 0; j < g; j++) pushRow([&](Row& r) { for (int l = 0; l < N; l++) r.c[l] = HOLE; if (j == (g >> 1)) r.f[gen.safe] = 1; });
    for (int i = 0; i < 6; i++) pushRow(nullptr);
}
void patGlitch() {
    const int k = 12 + ri(6), hitRow = 4 + 2 * ri(3);
    for (int i = 0; i < k; i++) {
        if (i % 5 == 4) moveSafe();
        pushRow([&](Row& r) {
            if (i % 2) return;
            for (int l = 0; l < N; l++) {
                if (isSafe(l)) continue;
                const float q = rnd();
                if (q < 0.17f) r.c[l] = GLITCH; else if (q < 0.23f) r.c[l] = BLOCK;
            }
            if (i == hitRow) r.c[gen.safe] = GLITCH;
        });
    }
}
void patBars() {
    const int n = gen.lvl >= 4 ? 2 : 1;
    for (int b = 0; b < n; b++) {
        for (int i = 0; i < 6; i++) pushRow(i == 2 ? RowFn(fragAtSafe) : RowFn());
        pushRow([](Row& r) {
            r.hasBar = true;
            r.bar.ph = rnd() * PI;
            r.bar.sp = (rnd() < 0.5f ? -1 : 1) * std::min(3.2f, 1.3f + 0.25f * gen.lvl);
            r.bar.cross = gen.lvl >= 6 && rnd() < 0.6f;
        });
    }
    for (int i = 0; i < 6; i++) pushRow(nullptr);
}
void patBroken() {
    const float p = std::min(0.5f, 0.24f + 0.04f * (gen.lvl - 4));
    const int k = 16 + ri(8);
    for (int i = 0; i < k; i++) {
        if (i % 4 == 3) moveSafe();
        pushRow([&](Row& r) {
            for (int l = 0; l < N; l++) {
                if (isSafe(l)) continue;
                if (rnd() < p) r.c[l] = rnd() < 0.5f ? CRUMBLE : HOLE;
                else if (i % 3 == 0 && rnd() < 0.08f) r.c[l] = BLOCK;
            }
            if (i % 3 == 0) r.f[gen.safe] = 1;
        });
    }
}
void patIslands() {
    const int segs = 3 + ri(3), w = gen.lvl >= 7 ? 1 : 2;
    for (int s = 0; s < segs; s++) {
        const int len = 5 + ri(4);
        const float wob = rnd() * TAU;
        for (int i = 0; i < len; i++) pushRow([&](Row& r) {
            r.island = true; r.wob = wob;
            for (int l = 0; l < N; l++) if (ld(l, gen.safe) > w) r.c[l] = HOLE;
            if (i == 2) r.f[gen.safe] = 1;
        });
        const int g = 2 + ri(std::min(4, gen.lvl - 3));
        for (int j = 0; j < g; j++) pushRow([&](Row& r) {
            for (int l = 0; l < N; l++) r.c[l] = HOLE;
            if (j == (g >> 1)) r.f[gen.safe] = 1;
        });
        gen.safe = wrap(gen.safe + ri(3) - 1);      // the next piece has floated a little way round
    }
    for (int i = 0; i < 4; i++) pushRow(nullptr);
}
void patSlalom() {
    const int k = 3 + ri(3);
    int dirn = rnd() < 0.5f ? -1 : 1;
    for (int s = 0; s < k; s++) {
        for (int i = 0; i < 7; i++) { if (i == 1 || i == 3) moveSafe(dirn); pushRow(i == 5 ? RowFn(fragAtSafe) : RowFn()); }
        pushRow([](Row& r) { for (int l = 0; l < N; l++) if (ld(l, gen.safe) > 1) r.c[l] = BLOCK; });
        if (rnd() < 0.4f) dirn = -dirn;
    }
    for (int j = 0; j < 4; j++) pushRow(nullptr);
}

void genChunk() {
    if (gen.inLvl >= LEVEL_ROWS) {
        gen.lvl++;
        gen.inLvl = 0;
        pushRow([](Row& r) { r.gate = gen.lvl; });
        setTargets();
        for (int i = 0; i < 14; i++) pushRow(i % 2 ? RowFn() : RowFn(fragAtSafe));
        return;
    }
    if (rnd() < 0.35f) setTargets();
    const int L = gen.lvl;
    struct P { void (*fn)(); float w; };
    const P list[] = {{patRun, 2}, {patBlocks, 3}, {patWall, L >= 2 ? 2.f : 1.f}, {patGap, 2}, {patGlitch, L >= 2 ? 2.5f : 1.2f},
                      {patBars, L >= 2 ? 2.f : 0.f}, {patBroken, L >= 4 ? 3.f : 0.f}, {patIslands, L >= 5 ? 3.5f : 0.f},
                      {patSlalom, L >= 3 ? 2.f : 0.f}};
    const int n = 9;
    float total = 0;
    for (const P& p : list) total += p.w;
    float pick = rnd() * total;
    int k;
    for (k = 0; k < n; k++) { pick -= list[k].w; if (pick <= 0) break; }
    list[std::min(k, n - 1)].fn();
}

void ensureRows(int upTo) { while (gen.r <= upTo + 2) genChunk(); }

void resetTrack(int startLevel) {
    rows.clear();
    gen = Gen();
    gen.safe = ri(N);
    gen.lvl = startLevel;
    if (startLevel > 1) setTargets();
    for (int i = 0; i < 30; i++) pushRow(i > 12 && i % 2 == 0 ? RowFn(fragAtSafe) : RowFn());
}

Row* rowAt(int i) { auto it = rows.find(i); return it == rows.end() ? nullptr : &it->second; }
float twistAt(float z) {
    const float e = z / ROW;
    const int i = (int)std::floor(e);
    Row* a = rowAt(i);
    Row* b = rowAt(i + 1);
    if (!a) return 0;
    if (!b) return a->twist;
    return a->twist + (b->twist - a->twist) * (e - i);
}
// which panel the runner is over, at depth z, at angle offset off
int laneAt(float z, float off = 0) { return (int)std::floor(mod(phi + off - twistAt(z) - spin, TAU) / LA) % N; }

// ---- the game -------------------------------------------------------------------------------------------------
float levelSpeed(int L) { return std::min(22.f, 9 + 1.4f * (L - 1)); }
void banner(const std::string& t, const std::string& sub = "") { bannerText = t; bannerSub = sub; bannerT = 2.6f; }
const char* note(int L) { return LEVEL_NOTES[std::min(L, NOTES - 1)]; }

void newGame(int startLevel) {
    resetTrack(startLevel);
    pz = 2 * ROW; spin = 0; h = 0; vh = 0; grounded = true;
    phi = (gen.safe + 0.5f) * LA;
    // start on the lane the first fragments sit on
    for (int i = 0; i < 30; i++) {
        Row& r = rows[i];
        int at = -1;
        for (int l = 0; l < N; l++) if (r.f[l] == 1) { at = l; break; }
        if (at >= 0) { phi = (at + 0.5f) * LA + r.twist; break; }
    }
    camRoll = phi;
    level = startLevel; lives = 3; score = 0; frags = 0; combo = 0; comboT = 0;
    reverseT = 0; invulnT = 0; fallT = 0; flashT = 0; shakeT = 0; jumpBuf = 0;
    particles.clear();
    state = "play";
    banner("LEVEL " + std::to_string(level), note(level));
    Sfx::start(); Sfx::music(true, level);
}

// ---- projection -----------------------------------------------------------------------------------------------
// edge rings: the screen positions of the panel corners at every row edge in view, worked out once a frame
constexpr int MAXE = VIS + 12;
float EX[MAXE * N], EY[MAXE * N], EOX[MAXE], EOY[MAXE], ES[MAXE];
int firstE = 0, lastE = 0;
float PX = 0, PY = 0, PS = 0;

void computeRings() {
    const int p0 = (int)std::floor(pz / ROW);
    firstE = (int)std::floor((pz - CAM_BACK) / ROW);
    lastE = std::min(p0 + VIS, firstE + MAXE - 1);
    const float cr = std::cos(camRoll), sr = std::sin(camRoll);
    float ox = 0, oy = 0, vx = 0, vy = 0, prevZ = pz;
    for (int e = firstE; e <= lastE; e++) {
        const int k = e - firstE;
        const float z = e * ROW;
        if (e > p0) {                                     // bends: the curve of the tunnel, built up from the runner forwards
            Row* row = rowAt(e - 1);
            const float len = z - prevZ;
            if (row) { vx += row->cx * len; vy += row->cy * len; }
            ox += vx * len; oy += vy * len; prevZ = z;
        }
        const float cx = ox * cr - oy * sr, cy = oy * cr + ox * sr;
        EOX[k] = cx; EOY[k] = cy;
        const float dz = std::max(NEAR, z - pz + CAM_BACK), s = F / dz;
        ES[k] = s;
        Row* rw = rowAt(e);
        const float tw = (rw ? rw->twist : 0) + spin - camRoll;
        for (int l = 0; l < N; l++) {
            const float a = l * LA + tw;
            EX[k * N + l] = CX + (cx + R * std::sin(a)) * s;
            EY[k * N + l] = CY + (cy + R * std::cos(a) - CAM_Y) * s;
        }
    }
}

// any point: world angle th, radius r (1 = the wall), depth z; sets PX, PY and PS (the scale there)
bool P(float th, float r, float z) {
    const float dz = z - pz + CAM_BACK;
    if (dz < NEAR) return false;
    const float f = z / ROW - firstE;
    int k = (int)std::floor(f);
    float t = f - k;
    if (k < 0) { k = 0; t = 0; }
    if (k >= lastE - firstE) { k = lastE - firstE - 1; t = 1; }
    const float ox = EOX[k] + (EOX[k + 1] - EOX[k]) * t, oy = EOY[k] + (EOY[k + 1] - EOY[k]) * t;
    const float a = th - camRoll, s = F / dz;
    PX = CX + (ox + r * std::sin(a)) * s;
    PY = CY + (oy + r * std::cos(a) - CAM_Y) * s;
    PS = s;
    return true;
}

void runnerScreen(float radiusOff, float& x, float& y) {
    if (!P(phi, R - h + radiusOff, pz)) { x = W / 2; y = H * 0.8f; return; }
    x = PX; y = PY;
}

void burst(float x, float y, int n, float speed, const std::vector<Color>& cols, float size) {
    for (int i = 0; i < n; i++) {
        const float a = frand() * TAU, v = speed * (0.2f + frand() * 0.8f), life = 0.4f + frand() * 0.6f;
        particles.push_back({x, y, std::cos(a) * v, std::sin(a) * v, life, life, cols[(size_t)(frand() * cols.size()) % cols.size()],
                             size * (0.5f + frand())});
    }
}
void flash(const Color& col, float t) { flashCol = col; flashT = t; }

void gameOver() {
    state = "over"; overT = time_;
    Sfx::music(false); Sfx::over();
    if (score > best) { best = (int)std::floor(score); Platform::store("vortexBest", std::to_string(best)); }
}

void loseLife() {
    lives--;
    combo = 0;
    if (lives <= 0) { gameOver(); return; }
    state = "play";
    invulnT = INVULN; h = 0; vh = 0; grounded = true; reverseT = 0;
}

void crash() {
    float x, y;
    runnerScreen(-0.15f, x, y);
    burst(x, y, 70, 520, {C::pink, C::white, C::sky, C::green}, 4);
    burst(x, y, 30, 220, {C::white}, 6);
    Sfx::hit();
    shakeT = 0.5f; flash(C::white, 0.35f);
    loseLife();
}

void startFall() {
    state = "fall"; fallT = 0;
    Sfx::fall();
}

void glitchHit(int lane, int r) {
    rows[r].c[lane] = EMPTY;
    reverseT = REVERSE_TIME;
    float x, y;
    runnerScreen(-0.2f, x, y);
    burst(x, y, 40, 380, {C::green, C::pink, C::white}, 3);
    Sfx::glitch(); flash(C::green, 0.25f); shakeT = 0.25f;
}

void collect(int lane, int r) {
    rows[r].f[lane] = 0;
    frags++;
    combo = comboT > 0 ? combo + 1 : 0; comboT = 1.2f;
    score += 50.f * level * (1 + std::min(combo, 10) * 0.1f);
    float x, y;
    runnerScreen(-FRAG_H, x, y);
    burst(x, y, 22, 300, {C::green, C::white}, 2.5f);
    Sfx::frag(combo);
}

// the autopilot drives the tunnel behind the splash: head for the clearest lane ahead
int autoSteer() {
    const int r0 = (int)std::floor(pz / ROW), cur = laneAt(pz);
    int bestK = 0;
    float bestCost = 1e9f;
    for (int k = -3; k <= 3; k++) {
        float cost = std::abs(k) * 0.6f;
        for (int j = 0; j < 9; j++) {
            Row* row = rowAt(r0 + j);
            if (!row) continue;
            const int c = row->c[wrap(cur + k)];
            if (c == BLOCK) cost += 12 - j; else if (c == GLITCH) cost += 4; else if ((c == HOLE || c == CRUMBLE) && j < 2) cost += 1;
            if (row->f[wrap(cur + k)]) cost -= 1;
        }
        if (cost < bestCost) { bestCost = cost; bestK = k; }
    }
    const float target = (wrap(cur + bestK) + 0.5f) * LA + twistAt(pz) + spin;
    const float d = mod(target - phi + PI, TAU) - PI;
    Row* ahead = rowAt(r0 + 1);
    if (ahead && grounded && (ahead->c[cur] == HOLE || ahead->c[cur] == CRUMBLE)) jumpBuf = 0.1f;
    return std::fabs(d) < 0.06f ? 0 : (d > 0 ? 1 : -1);
}

float barAngle(const Row& row, int r) { return row.bar.ph + row.bar.sp * time_ + twistAt((r + 0.5f) * ROW) + spin; }

void update(float dt) {
    time_ += dt;
    bannerT = std::max(0.f, bannerT - dt);
    flashT = std::max(0.f, flashT - dt);
    shakeT = std::max(0.f, shakeT - dt);
    if (state == "over") { pz += 3 * dt; return; }
    const bool attract = state == "attract";
    float speed = attract ? 9 : levelSpeed(level);
    if (state == "fall") speed *= 0.5f;

    // the tunnel turns by itself, back and forth, harder every level
    spin += std::min(0.75f, 0.14f + 0.07f * level) * std::sin(time_ * 0.17f + 1.3f) * dt;

    int dir;
    if (attract || autopilot) dir = autoSteer();
    else {
        dir = (keys.right ? 1 : 0) - (keys.left ? 1 : 0);
        if (reverseT > 0) dir = -dir;
    }
    if (state == "play" || attract) phi += dir * STEER * dt;

    jumpBuf = std::max(0.f, jumpBuf - dt);
    if (jumpBuf > 0 && grounded && (state == "play" || attract)) {
        vh = JUMP_V; grounded = false; jumpBuf = 0;
        if (!attract) Sfx::jump();
    }
    if (!grounded && state != "fall") {
        h += vh * dt; vh -= GRAV * dt;
        if (h <= 0) { h = 0; vh = 0; grounded = true; }
    }

    const float oz = pz, tw0 = twistAt(pz);
    pz += speed * dt;
    ensureRows((int)std::floor(pz / ROW) + VIS + 4);
    phi += twistAt(pz) - tw0;                         // the corkscrew carries you round with it
    const int rb = (int)std::floor(pz / ROW) - 6;     // forget the rows far behind
    for (int del = rb - 10; del < rb; del++) rows.erase(del);

    if (state == "fall") {
        fallT += dt;
        h -= (1.5f + fallT * 6) * dt;
        if (fallT > 0.95f) {
            float x, y;
            runnerScreen(0, x, y);
            burst(x, y, 30, 200, {C::pink, C::sky}, 3);
            loseLife();
        }
        return;
    }
    if (attract) return;

    score += speed * dt * 2 * (1 + 0.25f * (level - 1));
    reverseT = std::max(0.f, reverseT - dt);
    if (reverseT == 0 && wasReversed) Sfx::unglitch();
    wasReversed = reverseT > 0;
    invulnT = std::max(0.f, invulnT - dt);
    comboT = std::max(0.f, comboT - dt);

    // everything between the last frame and this one
    for (int r = (int)std::floor(oz / ROW); r <= (int)std::floor(pz / ROW) && state == "play"; r++) {
        Row* row = rowAt(r);
        if (!row) continue;
        const float zc = std::min(std::max(pz, r * ROW + 0.01f), (r + 1) * ROW - 0.01f);
        if (row->gate > level) {
            level = row->gate;
            score += 500.f * level;
            if (level % 3 == 0 && lives < 5) lives++;
            banner("LEVEL " + std::to_string(level), note(level));
            Sfx::level(); Sfx::music(false); Sfx::music(true, level);
            flash(C::white, 0.5f);
        }
        const int lc = laneAt(zc), ll = laneAt(zc, -RUN_HW), lr = laneAt(zc, RUN_HW);
        std::vector<int> lanes = ll == lr ? std::vector<int>{lc} : std::vector<int>{ll, lr};
        for (int L : lanes) {
            const int c = row->c[L];
            if (c == GLITCH && h < BLOCK_H) glitchHit(L, r);
            else if (c == BLOCK && h < BLOCK_H && invulnT <= 0) { crash(); break; }
            if (row->f[L] && std::fabs(h - FRAG_H) < 0.45f) collect(L, r);
        }
        if (state != "play") break;
        if (grounded && invulnT <= 0 && (row->c[lc] == HOLE || row->c[lc] == CRUMBLE)) { startFall(); break; }
    }
    // the spinning bars
    for (int r = (int)std::floor((pz - 0.3f) / ROW); r <= (int)std::floor((pz + 0.3f) / ROW) && state == "play"; r++) {
        Row* br = rowAt(r);
        if (!br || !br->hasBar || invulnT > 0) continue;
        const float bzc = (r + 0.5f) * ROW;
        if (std::fabs(pz - bzc) > 0.22f) continue;
        const float b = barAngle(*br, r), per = br->bar.cross ? PI / 2 : PI;
        const float m = mod(phi - b, per);
        if (std::min(m, per - m) < 0.16f + RUN_HW) crash();
    }
}

void makeStars() {
    stars.clear();
    for (int i = 0; i < 220; i++) stars.push_back({frand() * TAU, frand(), 0.3f + frand() * 0.9f});
    shards.clear();
    for (int i = 0; i < 26; i++)
        shards.push_back({frand() * TAU, 0.15f + frand() * 0.9f, frand() * TAU, (frand() - 0.5f) * 2, 0.4f + frand(), i % 2 ? C::pink : C::sky});
}

// ---- drawing --------------------------------------------------------------------------------------------------
Color PAL_FILL[33], PAL_LINE[33];
void makePalette() {
    const float PINK[3] = {240, 136, 233}, SKY[3] = {52, 169, 227};
    for (int i = 0; i <= 32; i++) {
        const float t = i / 32.f;
        const float r = SKY[0] + (PINK[0] - SKY[0]) * t, g = SKY[1] + (PINK[1] - SKY[1]) * t, b = SKY[2] + (PINK[2] - SKY[2]) * t;
        PAL_LINE[i] = rgb((int)r, (int)g, (int)b);
        PAL_FILL[i] = rgb((int)(r * 0.2f), (int)(g * 0.18f + 10), (int)(b * 0.22f + 22));
    }
}

Canvas* ctx = nullptr;
float tmp[16];

void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float inset) {
    const float mx = (x0 + x1 + x2 + x3) / 4, my = (y0 + y1 + y2 + y3) / 4, k = 1 - inset;
    ctx->beginPath();
    ctx->moveTo(mx + (x0 - mx) * k, my + (y0 - my) * k);
    ctx->lineTo(mx + (x1 - mx) * k, my + (y1 - my) * k);
    ctx->lineTo(mx + (x2 - mx) * k, my + (y2 - my) * k);
    ctx->lineTo(mx + (x3 - mx) * k, my + (y3 - my) * k);
    ctx->closePath();
}

bool pquad(float th0, float th1, float r0, float r1, float z0, float z1, bool alongZ) {   // false if behind the camera
    const float pts[4][3] = {{th0, r0, z0}, {th1, alongZ ? r0 : r1, z0}, {th1, r1, z1}, {th0, alongZ ? r1 : r0, z1}};
    for (int i = 0; i < 4; i++) {
        if (!P(pts[i][0], pts[i][1], pts[i][2])) return false;
        tmp[i * 2] = PX; tmp[i * 2 + 1] = PY;
    }
    quad(tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5], tmp[6], tmp[7], 0);
    return true;
}

void drawBackground(float dt, float speed) {
    Canvas& c = *ctx;
    c.fillStyle = C::black;
    c.fillRect(0, 0, W, H);
    const int k = lastE - firstE;
    const float vx = CX + EOX[k] * ES[k], vy = CY + (EOY[k] - CAM_Y) * ES[k], big = std::max(W, H) * 0.9f;
    // stars streaming out of the vortex
    c.strokeStyle = C::white;
    for (Star& s : stars) {
        s.r += s.r * dt * s.sp * speed * 0.12f + dt * 0.01f;
        if (s.r > 1.3f) { s.r = 0.02f + frand() * 0.05f; s.a = frand() * TAU; }
        const float a = s.a - camRoll * 0.6f, r1 = s.r * big, r0 = r1 * (1 - 0.06f * speed / 9);
        c.globalAlpha = std::min(1.f, s.r * 1.5f);
        c.lineWidth = 1 + s.r * 1.5f;
        c.beginPath();
        c.moveTo(vx + std::cos(a) * r0, vy + std::sin(a) * r0);
        c.lineTo(vx + std::cos(a) * r1, vy + std::sin(a) * r1);
        c.stroke();
    }
    // from level 4 on, broken bits of the tunnel tumble in the void
    if (level >= 4) {
        for (Shard& sh : shards) {
            sh.rot += sh.vr * dt; sh.a += dt * 0.05f * sh.vr;
            const float sa = sh.a - camRoll * 0.3f, sr = sh.r * big * 0.8f, x = vx + std::cos(sa) * sr, y = vy + std::sin(sa) * sr, z = 14 * sh.s;
            c.save(); c.translate(x, y); c.rotate(sh.rot);
            c.globalAlpha = 0.4f;
            c.fillStyle = C::navy; c.strokeStyle = sh.c; c.lineWidth = 1.5f;
            c.fillRect(-z, -z * 0.6f, z * 2, z * 1.2f); c.strokeRect(-z, -z * 0.6f, z * 2, z * 1.2f);
            c.restore();
        }
    }
    // the eye of the vortex
    c.globalAlpha = 1;
    for (int i = 6; i >= 1; i--) {
        const float rr = (i * 9 + std::fmod(time_ * 40, 9.f)) * (H / 720);
        c.strokeStyle = i % 2 ? C::pink : C::sky;
        c.globalAlpha = 0.5f * (1 - i / 7.f);
        c.lineWidth = 2;
        c.beginPath(); c.arc(vx, vy, rr, 0, TAU); c.stroke();
    }
    c.globalAlpha = 1;
    c.shadowColor = C::pink; c.shadowBlur = 30; c.fillStyle = C::white;
    c.beginPath(); c.arc(vx, vy, 4 + 2 * std::sin(time_ * 6), 0, TAU); c.fill();
    c.shadowBlur = 0;
}

void drawBox(int e, int l, const Row& row, float fog, bool glitch) {
    Canvas& c = *ctx;
    const float tw = row.twist + spin;
    float a0 = (l + 0.12f) * LA + tw, a1 = (l + 0.88f) * LA + tw;
    const float z0 = e * ROW + 0.08f, z1 = (e + 1) * ROW - 0.08f, r0 = R, r1 = R - BLOCK_H;
    Color fill = C::navy, line = C::white, topLine = C::sky;
    if (glitch) {
        if (frand() < 0.18f) return;                      // it flickers out of being
        const float jit = 0.05f;
        const Color cs[3] = {C::green, C::pink, C::white};
        line = cs[(int)(frand() * 3) % 3]; topLine = cs[(int)(frand() * 3) % 3];
        fill = frand() < 0.5f ? Color("#06331a") : Color("#2a0a2a");
        a0 += (frand() - 0.5f) * jit; a1 += (frand() - 0.5f) * jit;
    }
    c.globalAlpha = fog;
    c.lineWidth = clamp(PS * 0.008f, 0.8f, 3);
    // the sides are walls going in from the panel: (angle, radius) along the edge
    for (int i = 0; i < 4; i++) {
        if (i < 2) {
            const float a = i == 0 ? a0 : a1;
            if (!P(a, r0, z0)) return;
            tmp[0] = PX; tmp[1] = PY;
            P(a, r1, z0); tmp[2] = PX; tmp[3] = PY;
            P(a, r1, z1); tmp[4] = PX; tmp[5] = PY;
            P(a, r0, z1); tmp[6] = PX; tmp[7] = PY;
            quad(tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5], tmp[6], tmp[7], 0);
        } else if (i == 2) {
            if (!pquad(a0, a1, r1, r1, z0, z1, false)) return;
        } else {
            if (!pquad(a0, a1, r0, r1, z0, z0, true)) return;
        }
        c.fillStyle = i == 3 ? fill : (glitch ? fill : Color("#000c16"));
        c.fill();
        c.strokeStyle = i == 2 ? topLine : line;
        c.stroke();
    }
    if (glitch && frand() < 0.5f) {                       // a ghost copy, torn off sideways
        c.globalAlpha = fog * 0.5f;
        if (pquad(a0 + 0.08f, a1 + 0.08f, r0, r1, z0, z0, true)) { c.strokeStyle = C::green; c.stroke(); }
    }
}

void drawRowThings(const Row& row, int e, float fog) {
    Canvas& c = *ctx;
    for (int l = 0; l < N; l++) {
        if (row.c[l] == BLOCK) drawBox(e, l, row, fog, false);
        else if (row.c[l] == GLITCH) drawBox(e, l, row, fog, true);
    }
    const float zc = (e + 0.5f) * ROW;
    for (int l = 0; l < N; l++) {
        if (!row.f[l]) continue;
        if (!P((l + 0.5f) * LA + row.twist + spin, R - FRAG_H + 0.04f * std::sin(time_ * 4 + l), zc)) continue;
        const float s = PS * 0.08f, rot = time_ * 3 + l;
        c.globalAlpha = fog;
        c.save(); c.translate(PX, PY); c.rotate(rot);
        c.shadowColor = C::green; c.shadowBlur = 14; c.fillStyle = C::green;
        c.beginPath(); c.moveTo(0, -s); c.lineTo(s * 0.6f, 0); c.lineTo(0, s); c.lineTo(-s * 0.6f, 0); c.closePath(); c.fill();
        c.shadowBlur = 0; c.fillStyle = C::white;
        c.beginPath(); c.moveTo(0, -s * 0.4f); c.lineTo(s * 0.25f, 0); c.lineTo(0, s * 0.4f); c.lineTo(-s * 0.25f, 0); c.closePath(); c.fill();
        c.restore();
    }
    if (row.hasBar) {
        const float b = barAngle(row, e);
        const int arms = row.bar.cross ? 2 : 1;
        c.globalAlpha = fog;
        c.shadowColor = C::pink; c.shadowBlur = 18; c.lineCap = Cap::Round;
        for (int a = 0; a < arms; a++) {
            const float ang = b + a * PI / 2;
            if (!P(ang, R, zc)) break;
            const float x0 = PX, y0 = PY, s0 = PS;
            P(ang + PI, R, zc);
            c.strokeStyle = C::pink; c.lineWidth = std::max(2.f, s0 * 0.09f);
            c.beginPath(); c.moveTo(x0, y0); c.lineTo(PX, PY); c.stroke();
            c.strokeStyle = C::white; c.lineWidth = std::max(1.f, s0 * 0.03f);
            c.beginPath(); c.moveTo(x0, y0); c.lineTo(PX, PY); c.stroke();
        }
        if (P(0, 0, zc)) { c.fillStyle = C::white; c.beginPath(); c.arc(PX, PY, std::max(2.f, PS * 0.07f), 0, TAU); c.fill(); }
        c.shadowBlur = 0; c.lineCap = Cap::Butt;
    }
    if (row.gate) {                                       // the level gate: a bright ring
        c.globalAlpha = fog;
        c.shadowColor = C::green; c.shadowBlur = 24; c.strokeStyle = C::green; c.lineWidth = clamp(ES[e - firstE] * 0.03f, 1, 12);
        c.beginPath();
        for (int l = 0; l <= N * 3; l++) { P(l * LA / 3 + row.twist + spin, R * 0.96f, e * ROW); if (l) c.lineTo(PX, PY); else c.moveTo(PX, PY); }
        c.stroke(); c.shadowBlur = 0;
    }
}

void drawTunnel() {
    Canvas& c = *ctx;
    const float far = (lastE - firstE) * ROW;
    for (int e = lastE - 1; e >= firstE; e--) {
        Row* rowp = rowAt(e);
        if (!rowp) continue;
        const Row& row = *rowp;
        const int k = e - firstE;
        const float d = e * ROW - pz;
        float fog = clamp(1 - (d - 2) / (far - 2), 0, 1);
        fog = std::pow(fog, 0.8f);
        if (fog <= 0.01f) continue;
        const float lw = clamp(ES[k] * 0.006f, 0.6f, 4);
        const float wave = 0.5f + 0.5f * std::sin(time_ * 5 - e * 0.45f);
        Row* nearer = rowAt(e - 1);
        const float wob = row.island ? 0.03f * std::sin(time_ * 1.7f + row.wob) : 0;
        for (int l = 0; l < N; l++) {
            const int cc = row.c[l];
            const int i0 = k * N + l, i1 = k * N + (l + 1) % N, j0 = (k + 1) * N + l, j1 = (k + 1) * N + (l + 1) % N;
            const int pal = (int)std::round((l % 2 ? 1 - wave : wave) * 32);
            if (cc == HOLE) continue;
            if (cc == CRUMBLE && d < CRUMBLE_D) {           // the panel breaks off and falls away into the void
                const float q = 1 - std::max(0.f, d) / CRUMBLE_D, tw = row.twist + spin + q * 0.5f * (l % 2 ? 1 : -1);
                c.globalAlpha = fog * (1 - q);
                if (pquad(l * LA + tw, (l + 1) * LA + tw, R + q * q * 3, R + q * q * 3, e * ROW, (e + 1) * ROW, false)) {
                    c.fillStyle = PAL_FILL[pal]; c.fill();
                    c.strokeStyle = PAL_LINE[pal]; c.lineWidth = lw; c.stroke();
                }
                continue;
            }
            // the cliff edge, where the panel nearer to you is missing
            if (nearer && (nearer->c[l] == HOLE || nearer->c[l] == CRUMBLE) && d > -0.5f) {
                const float tw2 = row.twist + spin;
                c.globalAlpha = fog;
                if (pquad(l * LA + tw2, (l + 1) * LA + tw2, R, R + 0.25f, e * ROW, e * ROW, true)) {
                    c.fillStyle = C::navy; c.fill();
                    c.strokeStyle = C::deep; c.lineWidth = lw; c.stroke();
                }
            }
            c.globalAlpha = fog;
            if (wob != 0) {
                const float tw3 = row.twist + spin;
                if (!pquad(l * LA + tw3, (l + 1) * LA + tw3, R + wob, R + wob, e * ROW, (e + 1) * ROW, false)) continue;
            } else {
                quad(EX[i0], EY[i0], EX[i1], EY[i1], EX[j1], EY[j1], EX[j0], EY[j0], 0.1f);
            }
            c.fillStyle = cc == CRUMBLE ? C::navy : PAL_FILL[pal];
            c.fill();
            c.strokeStyle = cc == CRUMBLE ? (frand() < 0.5f ? C::white : PAL_LINE[pal]) : PAL_LINE[pal];
            c.lineWidth = lw;
            c.stroke();
        }
        if (d > -0.3f) drawRowThings(row, e, fog);
    }
    c.globalAlpha = 1;
}

void drawRunner() {
    Canvas& c = *ctx;
    if (state == "attract" || state == "over") return;
    if (invulnT > 0 && (int)std::floor(time_ * 14) % 2) return;
    const float a = phi - camRoll;
    const Color glowC = reverseT > 0 ? C::green : C::pink;
    if (h > 0.02f && P(phi, R, pz)) {                     // the shadow on the wall
        c.fillStyle = Color("rgba(0,0,0,0.45)");
        c.beginPath(); c.ellipse(PX, PY, PS * 0.09f, PS * 0.03f, -a, 0, TAU); c.fill();
    }
    if (!P(phi, R - h, pz)) return;
    const float u = PS * 0.075f, run = time_ * 16;
    const bool air = !grounded || state == "fall";
    c.save();
    c.translate(PX, PY); c.rotate(-a);
    if (state == "fall") { const float k = std::max(0.1f, 1 - fallT); c.scale(k, k); c.rotate(fallT * 6); }
    c.strokeStyle = C::white; c.lineCap = Cap::Round; c.roundJoin = true;
    c.shadowColor = glowC; c.shadowBlur = 20;
    c.lineWidth = u * 0.45f;
    const float s1 = std::sin(run), s2 = std::sin(run + PI);
    c.beginPath();                                        // legs
    if (air) { c.moveTo(-0.3f * u, -0.5f * u); c.lineTo(-0.35f * u, -1.2f * u); c.moveTo(0.3f * u, -0.6f * u); c.lineTo(0.35f * u, -1.2f * u); }
    else {
        c.moveTo(-0.35f * u, -std::max(0.f, s1) * 0.7f * u); c.lineTo(-0.3f * u, -1.2f * u);
        c.moveTo(0.35f * u, -std::max(0.f, s2) * 0.7f * u); c.lineTo(0.3f * u, -1.2f * u);
    }
    c.stroke();
    c.lineWidth = u * 0.7f;                               // body
    c.beginPath(); c.moveTo(0, -1.25f * u); c.lineTo(0, -2.4f * u); c.stroke();
    c.lineWidth = u * 0.32f;                              // arms
    c.beginPath();
    const float sw = air ? -0.8f : 0.35f;
    c.moveTo(-0.3f * u, -2.3f * u); c.lineTo(-0.75f * u, (-1.7f + (air ? -1 : s2 * sw)) * u);
    c.moveTo(0.3f * u, -2.3f * u); c.lineTo(0.75f * u, (-1.7f + (air ? -1 : s1 * sw)) * u);
    c.stroke();
    c.fillStyle = C::white;                               // head
    c.beginPath(); c.arc(0, -2.95f * u, 0.42f * u, 0, TAU); c.fill();
    c.shadowBlur = 0;
    c.fillStyle = glowC;                                  // a little glowing core
    c.beginPath(); c.arc(0, -1.9f * u, 0.15f * u, 0, TAU); c.fill();
    c.restore();
}

void drawParticles(float dt) {
    Canvas& c = *ctx;
    const float drag = std::exp(-2.5f * dt);
    for (int i = (int)particles.size() - 1; i >= 0; i--) {
        Particle& p = particles[i];
        p.life -= dt;
        if (p.life <= 0) { particles.erase(particles.begin() + i); continue; }
        p.x += p.vx * dt; p.y += p.vy * dt; p.vx *= drag; p.vy *= drag;
        const float k = p.life / p.max;
        c.globalAlpha = k;
        c.fillStyle = p.c; c.shadowColor = p.c; c.shadowBlur = 10;
        c.beginPath(); c.arc(p.x, p.y, 0.5f + p.s * k, 0, TAU); c.fill();
    }
    c.globalAlpha = 1; c.shadowBlur = 0;
}

void centreText(const std::string& t, float y, float size, const Color& col, const Color* glow = nullptr) {
    Canvas& c = *ctx;
    c.textAlign = Align::Center; c.textBaseline = Baseline::Middle;
    c.font(Font::Black, size);
    c.shadowColor = glow ? *glow : col; c.shadowBlur = glow ? 22.f : 0.f;
    c.fillStyle = col; c.fillText(t, W / 2, y);
    c.shadowBlur = 0;
}

void drawOverlays() {
    Canvas& c = *ctx;
    if (reverseT > 0) {
        // the screen goes green and tears while your controls are the wrong way round
        c.fillStyle = Color("rgba(57,255,106,0.13)");
        c.fillRect(0, 0, W, H);
        c.grabScreen();
        for (int i = 0; i < 5; i++) {
            if (frand() < 0.55f) continue;
            const float y = frand() * H, hh = 4 + frand() * 40, dx = (frand() - 0.5f) * 50;
            c.drawGrab(0, y, W, hh, dx, y, W, hh);
        }
        c.strokeStyle = C::green; c.lineWidth = 6;
        c.strokeRect(3, 3, W - 6, H - 6);
        const float sz = std::min(44.f, W / 14);
        centreText((int)std::floor(time_ * 10) % 7 == 0 ? "C0NTR0LS R3V3RS3D" : "CONTROLS REVERSED", H * 0.14f, sz, C::green, &C::green);
        const float bw = std::min(360.f, W * 0.6f);
        c.fillStyle = C::navy; c.fillRect(W / 2 - bw / 2, H * 0.14f + sz * 0.7f, bw, 8);
        c.fillStyle = C::green; c.fillRect(W / 2 - bw / 2, H * 0.14f + sz * 0.7f, bw * reverseT / REVERSE_TIME, 8);
    }
    if (flashT > 0) {
        c.globalAlpha = std::min(0.6f, flashT * 1.6f);
        c.fillStyle = flashCol; c.fillRect(0, 0, W, H);
        c.globalAlpha = 1;
    }
    if (bannerT > 0 && state != "attract") {
        c.globalAlpha = std::min(1.f, bannerT * 1.5f);
        centreText(bannerText, H * 0.32f, std::min(84.f, W / 8), C::pink, &C::pink);
        centreText(bannerSub, H * 0.32f + std::min(60.f, W / 12), std::min(26.f, W / 22), C::sky);
        c.globalAlpha = 1;
    }
    if (paused || state == "over") {
        c.fillStyle = Color("rgba(2, 6, 17, 0.65)");
        c.fillRect(0, 0, W, H);
        centreText(paused ? "PAUSED" : "GAME OVER", H / 2 - 30, std::min(84.f, W / 8), C::pink, &C::pink);
        c.font(Font::Regular, 20); c.fillStyle = C::white;
        c.fillText(paused ? "P to carry on"
                          : "Score " + std::to_string((int)std::floor(score)) + "  \xC2\xB7  Level " + std::to_string(level) +
                                "  \xC2\xB7  Fragments " + std::to_string(frags),
                   W / 2, H / 2 + 28);
        if (!paused) {
            const bool nb = (int)std::floor(score) >= best && score > 0;
            c.fillStyle = nb ? C::green : C::sky;
            c.fillText(nb ? "A new best!" : "Best " + std::to_string(best), W / 2, H / 2 + 60);
            c.fillStyle = Color("#c2c2c2");
            c.fillText("Space, Enter or click to run again", W / 2, H / 2 + 96);
        }
    }
}

// the HTML bits over the canvas: the HUD (top left) and the help line (bottom right)
void drawHud() {
    Canvas& c = *ctx;
    if (!started_) return;
    c.save();
    c.globalAlpha = 1 - splashFade;
    c.font(Font::Bold, 20); c.letterSpacing = 1; c.textAlign = Align::Left; c.textBaseline = Baseline::Top;
    float x = 14;
    const std::string parts[4] = {"Score " + std::to_string((int)std::floor(score)), "Level " + std::to_string(level),
                                  "Lives " + std::to_string(std::max(0, lives)),
                                  "Best " + std::to_string(best) + (Sfx::isMuted() ? "  (muted)" : "")};
    for (int i = 0; i < 4; i++) {
        c.fillStyle = i == 3 ? C::sky : C::pink;
        c.fillText(parts[i], x, 12);
        x += c.measureText(parts[i]) + 22;
    }
    c.letterSpacing = 0;
    c.font(Font::Regular, 13); c.fillStyle = Color("#c2c2c2"); c.textAlign = Align::Right; c.textBaseline = Baseline::Bottom;
    c.fillText("\xE2\x86\x90 \xE2\x86\x92 / A D run round \xC2\xB7 Space / \xE2\x86\x91 jump \xC2\xB7 P pause \xC2\xB7 M mute \xC2\xB7 F11 fullscreen", W - 14, H - 10);
    c.restore();
}

void drawTheSplash(double nowMs) {
    if (splashFade <= 0) return;
    const float vw = W / 100;
    std::vector<SplashItem> items;
    SplashItem title; title.text = "VORTEX"; title.font = Font::Black; title.px = std::min(150.f, 22 * vw);
    title.spacing = title.px * 0.12f; title.lineH = 1; title.color = C::pink;
    SplashItem tag; tag.text = "AN ARCADE PHANTASM GAME"; tag.font = Font::Bold; tag.px = std::min(24.f, 5 * vw);
    tag.spacing = tag.px * 0.2f; tag.color = C::sky; tag.marginBottom = 10;
    SplashItem img; img.image = &logo; img.imageW = std::min(260.f, 50 * vw);
    SplashItem copy; copy.text = "\xC2\xA9 Cursed Entertainment 2026"; copy.px = 15; copy.color = Color("#c2c2c2");
    SplashItem how; how.text = "Run round the tunnel, jump the gaps, grab the fragments.\nGlitches flip your left and right.";
    how.px = 15; how.lineH = 1.5f; how.color = Color("#c2c2c2"); how.marginTop = 10;
    SplashItem press; press.text = "Press any key or click to start"; press.font = Font::Bold; press.px = 20;
    press.spacing = 1.6f; press.color = C::green; press.marginTop = 14;
    press.alpha = std::fmod(nowMs / 1000.0, 1.2) < 0.6 ? 1.f : 0.f;   // steps(2, start): on, then off
    items = {title, tag, img, copy, how, press};
    drawSplash(*ctx, items, 14, Color("rgba(2, 6, 17, 0.62)"), splashFade);
}

void draw(float dt) {
    const float shake = shakeT > 0 ? shakeT * 18 : 0;
    camRoll += (phi - camRoll) * std::min(1.f, dt * 7);
    CX = W / 2 + (frand() - 0.5f) * shake;
    CY = H * 0.47f + (frand() - 0.5f) * shake;
    computeRings();
    drawBackground(dt, state == "attract" ? 9 : levelSpeed(level));
    drawTunnel();
    drawRunner();
    drawParticles(dt);
    drawOverlays();
}

void start() {
    if (started_) return;
    started_ = true;
    newGame(1);
}

void togglePause() {
    if (state == "over" || !started_) return;
    paused = !paused;
    Sfx::music(!paused, level);
}

void restart() { if (state == "over" && time_ - overT > 0.8f) newGame(1); }
}  // namespace

// ---- what main.cpp calls ----------------------------------------------------------------------------------------
const char* Game::title() { return "VORTEX"; }
bool Game::started() { return started_; }

void Game::init(float w, float h) {
    best = std::atoi(Platform::load("vortexBest", "0").c_str());
    Sfx::muted = Platform::load("vortexMute", "0") == "1";
    logo = Platform::texture("cursed-logo.png");
    makePalette();
    resize(w, h);
    makeStars();
    time_ = 0;
    seed = 20261109;
    newGame(1);
    state = "attract";
    bannerT = 0;
}

void Game::resize(float w, float h) {
    W = w; H = h;
    F = std::min(W * 0.75f, H) * 0.62f;
}

void Game::frame(Canvas& c, double now) {
    ctx = &c;
    const float dt = last ? std::min(0.05f, (float)((now - last) / 1000)) : 1.f / 60;
    last = now;
    Sfx::schedule();
    if (!paused) update(dt);
    draw(paused ? 0 : dt);
    if (started_) splashFade = std::max(0.f, splashFade - dt / 0.5f);
    drawHud();
    drawTheSplash(now);
}

void Game::keyDown(const std::string& k, bool repeat) {
    Sfx::wake();
    if (!started_) { start(); return; }
    if (k == "m") Sfx::toggleMute();
    else if (k == "p" || k == "Escape") togglePause();
    else if (state == "over") { if (k == " " || k == "Enter" || k == "r") restart(); }
    else if (k == "ArrowLeft" || k == "a") keys.left = true;
    else if (k == "ArrowRight" || k == "d") keys.right = true;
    else if ((k == " " || k == "ArrowUp" || k == "w") && !repeat) jumpBuf = 0.15f;
}

void Game::keyUp(const std::string& k) {
    if (k == "ArrowLeft" || k == "a") keys.left = false;
    if (k == "ArrowRight" || k == "d") keys.right = false;
}

void Game::pointerDown(float, float) {
    Sfx::wake();
    if (!started_) { start(); return; }
    if (state == "over") restart();
}
void Game::pointerMove(float, float) {}
void Game::pointerUp(float, float) {}

void Game::blur() {
    keys.left = keys.right = false;
    if (state == "play" && !paused) togglePause();
}
