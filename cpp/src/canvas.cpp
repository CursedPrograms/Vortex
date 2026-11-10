// canvas.cpp - the canvas 2D context on ImGui draw lists. Shapes are flattened to screen-space polygons as they are
// added (the transform applies there, as in a browser); fills are convex (anti-aliased by ImGui), ear-clipped, or
// scan-converted for even-odd paths; strokes are ImGui polylines with dashes and round caps added here.
#include "canvas.h"
#include <imgui_internal.h>                       // the white pixel's UV and the UTF-8 decoder

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {
constexpr float TAU = 6.2831853f;

struct Baked { ImFont* f; float px, em; };   // em: ImGui's size for 1 CSS px (ImGui sizes by ascent+descent, CSS by em)

// a TrueType font's (ascent - descent) / units per em, from its hhea and head tables
float emRatio(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t o) { return o + 1 < d.size() ? (unsigned)(d[o] << 8 | d[o + 1]) : 0u; };
    auto u32 = [&](size_t o) { return o + 3 < d.size() ? (unsigned)(d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]) : 0u; };
    if (d.size() < 12) return 1.f;
    const unsigned n = u16(4);
    float upem = 0, asc = 0, desc = 0;
    for (unsigned i = 0; i < n; ++i) {
        const size_t rec = 12 + 16 * i;
        const unsigned off = u32(rec + 8);
        if (!std::memcmp(&d[rec], "head", 4)) upem = (float)u16(off + 18);
        if (!std::memcmp(&d[rec], "hhea", 4)) { asc = (float)(short)u16(off + 4); desc = (float)(short)u16(off + 6); }
    }
    return upem > 0 && asc - desc > 0 ? (asc - desc) / upem : 1.f;
}
std::vector<Baked> baked[3];                    // per Font kind, smallest size first

float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

float hue2rgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.f / 3) return p + (q - p) * (2.f / 3 - t) * 6;
    return p;
}

float area(const std::vector<ImVec2>& p) {
    float a = 0;
    for (size_t i = 0, n = p.size(); i < n; ++i) {
        const ImVec2& u = p[i]; const ImVec2& v = p[(i + 1) % n];
        a += u.x * v.y - v.x * u.y;
    }
    return a * 0.5f;
}

bool convex(const std::vector<ImVec2>& p) {
    const size_t n = p.size();
    int sign = 0;
    for (size_t i = 0; i < n; ++i) {
        const ImVec2 &a = p[i], &b = p[(i + 1) % n], &c = p[(i + 2) % n];
        const float cr = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (std::fabs(cr) < 1e-4f) continue;
        const int s = cr > 0 ? 1 : -1;
        if (sign && s != sign) return false;
        sign = s;
    }
    return true;
}

// the inside of a positive-area convex polygon: cross >= 0 on every edge
float side(const ImVec2& a, const ImVec2& b, const ImVec2& p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); }

std::vector<ImVec2> clipPoly(const std::vector<ImVec2>& subject, const std::vector<ImVec2>& clip) {
    std::vector<ImVec2> out = subject, in;
    for (size_t i = 0, n = clip.size(); i < n && !out.empty(); ++i) {
        const ImVec2 a = clip[i], b = clip[(i + 1) % n];
        in.swap(out); out.clear();
        for (size_t j = 0, m = in.size(); j < m; ++j) {
            const ImVec2 p = in[j], q = in[(j + 1) % m];
            const float sp = side(a, b, p), sq = side(a, b, q);
            if (sp >= 0) out.push_back(p);
            if ((sp >= 0) != (sq >= 0)) {
                const float t = sp / (sp - sq);
                out.push_back({p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t});
            }
        }
    }
    return out;
}

// ear clipping (after John W. Ratcliff's classic): triangles as indices into p; false if it gives up
bool snip(const std::vector<ImVec2>& p, int u, int v, int w, int n, const std::vector<int>& V) {
    const ImVec2 A = p[V[u]], B = p[V[v]], Cc = p[V[w]];
    if ((B.x - A.x) * (Cc.y - A.y) - (B.y - A.y) * (Cc.x - A.x) < 1e-7f) return false;
    for (int k = 0; k < n; ++k) {
        if (k == u || k == v || k == w) continue;
        const ImVec2 P = p[V[k]];
        if (side(A, B, P) >= 0 && side(B, Cc, P) >= 0 && side(Cc, A, P) >= 0) return false;
    }
    return true;
}
bool triangulate(const std::vector<ImVec2>& p, std::vector<int>& tris) {
    const int n = (int)p.size();
    if (n < 3) return false;
    std::vector<int> V(n);
    if (area(p) > 0) for (int i = 0; i < n; ++i) V[i] = i;
    else for (int i = 0; i < n; ++i) V[i] = n - 1 - i;
    int nv = n, count = 2 * nv;
    for (int v = nv - 1; nv > 2;) {
        if (count-- <= 0) return false;
        int u = v; if (nv <= u) u = 0;
        v = u + 1; if (nv <= v) v = 0;
        int w = v + 1; if (nv <= w) w = 0;
        if (snip(p, u, v, w, nv, V)) {
            tris.push_back(V[u]); tris.push_back(V[v]); tris.push_back(V[w]);
            for (int s = v, t = v + 1; t < nv; ++s, ++t) V[s] = V[t];
            --nv;
            count = 2 * nv;
        }
    }
    return true;
}

void dedupe(std::vector<ImVec2>& p) {
    std::vector<ImVec2> o;
    o.reserve(p.size());
    for (const ImVec2& q : p)
        if (o.empty() || std::fabs(o.back().x - q.x) > 0.01f || std::fabs(o.back().y - q.y) > 0.01f) o.push_back(q);
    while (o.size() > 2 && std::fabs(o.front().x - o.back().x) < 0.01f && std::fabs(o.front().y - o.back().y) < 0.01f) o.pop_back();
    p.swap(o);
}

// what one baked font draws at a requested size
const Baked* pick(Font f, float px) {
    const auto& v = baked[(int)f];
    if (v.empty()) return nullptr;
    for (const Baked& b : v) if (b.px >= px * 0.95f) return &b;
    return &v.back();
}

GLuint grabTex = 0;
void grabCallback(const ImDrawList*, const ImDrawCmd*) {
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    glBindTexture(GL_TEXTURE_2D, grabTex);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, vp[0], vp[1], vp[2], vp[3], 0);
}
void blendLighter(const ImDrawList*, const ImDrawCmd*) { glBlendFunc(GL_SRC_ALPHA, GL_ONE); }
void blendNormal(const ImDrawList*, const ImDrawCmd*) { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
}  // namespace

// ---- colours ----------------------------------------------------------------------------------------------------
Color rgb(int r, int g, int b, float a) { return Color(r / 255.f, g / 255.f, b / 255.f, a); }

Color::Color(const char* s) {
    while (*s == ' ') ++s;
    if (*s == '#') {
        const size_t n = std::strlen(s + 1);
        unsigned v = (unsigned)std::strtoul(s + 1, nullptr, 16);
        if (n == 3) { r = ((v >> 8) & 15) / 15.f; g = ((v >> 4) & 15) / 15.f; b = (v & 15) / 15.f; }
        else { r = ((v >> 16) & 255) / 255.f; g = ((v >> 8) & 255) / 255.f; b = (v & 255) / 255.f; }
        a = 1;
        return;
    }
    float v[4] = {0, 0, 0, 1};
    if (!std::strncmp(s, "rgb", 3) || !std::strncmp(s, "hsl", 3)) {
        const bool hsl = s[0] == 'h';
        const char* p = std::strchr(s, '(');
        int k = 0;
        while (p && k < 4) {
            ++p;
            v[k++] = (float)std::strtod(p, nullptr);
            p = std::strpbrk(p, ",)");
            if (p && *p == ')') break;
        }
        if (hsl) {
            const float h = std::fmod(v[0] / 360.f + 1.f, 1.f), sat = v[1] / 100.f, l = v[2] / 100.f;
            if (sat <= 0) { r = g = b = l; }
            else {
                const float q = l < 0.5f ? l * (1 + sat) : l + sat - l * sat, pp = 2 * l - q;
                r = hue2rgb(pp, q, h + 1.f / 3); g = hue2rgb(pp, q, h); b = hue2rgb(pp, q, h - 1.f / 3);
            }
        } else { r = v[0] / 255.f; g = v[1] / 255.f; b = v[2] / 255.f; }
        a = k >= 4 ? v[3] : 1.f;
        return;
    }
    if (!std::strcmp(s, "white")) { r = g = b = 1; a = 1; return; }
    if (!std::strcmp(s, "transparent")) { r = g = b = 0; a = 0; return; }
    r = g = b = 0; a = 1;                          // black and anything unknown
}

// ---- fonts ------------------------------------------------------------------------------------------------------
void Canvas::loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar text[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x2195, 0};
    static const ImWchar title[] = {0x0020, 0x007F, 0x00A9, 0x00A9, 0x00B7, 0x00B7, 0};
    static const ImWchar symbols[] = {0x25A0, 0x25FF, 0x2600, 0x2606, 0};
    const char* dir = "C:/Windows/Fonts/";
    struct Want { Font f; const char* file; std::vector<float> sizes; const ImWchar* ranges; };
    const Want wants[] = {
        {Font::Regular, "arial.ttf", {12, 16, 22, 34}, text},
        {Font::Bold, "arialbd.ttf", {12, 16, 22, 34, 64}, text},
        {Font::Black, "ariblk.ttf", {22, 40, 72, 150}, title},
    };
    const std::string sym = std::string(dir) + "seguisym.ttf";
    const bool haveSym = std::ifstream(sym).good();
    for (const Want& w : wants) {
        std::string path = std::string(dir) + w.file;
        if (!std::ifstream(path).good()) {
            // Linux and friends: DejaVu stands in
            for (const char* alt : {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"})
                if (std::ifstream(alt).good()) { path = alt; break; }
        }
        if (!std::ifstream(path).good()) continue;
        const float em = emRatio(path);
        for (float px : w.sizes) {
            ImFontConfig cfg;
            cfg.OversampleH = px > 40 ? 1 : 2;
            cfg.OversampleV = 1;
            ImFont* f = io.Fonts->AddFontFromFileTTF(path.c_str(), px * em, &cfg, w.ranges);
            if (!f) continue;
            if (haveSym && w.f != Font::Black) {
                ImFontConfig m;
                m.MergeMode = true;
                m.OversampleH = 1;
                io.Fonts->AddFontFromFileTTF(sym.c_str(), px * em, &m, symbols);
            }
            baked[(int)w.f].push_back({f, px, em});
        }
    }
    if (baked[0].empty() && baked[1].empty()) {
        ImFont* f = io.Fonts->AddFontDefault();
        for (auto& v : baked) v.push_back({f, 13.f, 1.f});
    }
    for (auto& v : baked) if (v.empty()) v = baked[1].empty() ? baked[0] : baked[1];
}

ImFont* Canvas::imfont() const { const Baked* b = pick(font_, fontPx_ * scaleOf()); return b ? b->f : ImGui::GetFont(); }
float Canvas::emOf() const { const Baked* b = pick(font_, fontPx_ * scaleOf()); return b ? b->em : 1.f; }

// ---- state ------------------------------------------------------------------------------------------------------
void Canvas::begin(ImDrawList* dl, float W, float H) {
    dl_ = dl; W_ = W; H_ = H;
    stack_.clear(); path_.clear(); clip_.clear(); dash_.clear();
    resetTransform();
    fillStyle = Color(1, 1, 1); strokeStyle = Color(0, 0, 0); shadowColor = Color(0, 0, 0, 0);
    lineWidth = 1; globalAlpha = 1; shadowBlur = 0; lineDashOffset = 0; letterSpacing = 0;
    lineCap = Cap::Butt; roundJoin = false; textAlign = Align::Left; textBaseline = Baseline::Alphabetic;
    font_ = Font::Regular; fontPx_ = 10; lighter_ = false;
}

void Canvas::save() {
    State s{fillStyle, strokeStyle, shadowColor, lineWidth, globalAlpha, shadowBlur, lineDashOffset, letterSpacing,
            lineCap, roundJoin, textAlign, textBaseline, font_, fontPx_, dash_, {}, clip_};
    std::memcpy(s.m, m_, sizeof m_);
    stack_.push_back(std::move(s));
}

void Canvas::restore() {
    if (stack_.empty()) return;
    State& s = stack_.back();
    fillStyle = s.fill; strokeStyle = s.stroke; shadowColor = s.shadow; lineWidth = s.lw; globalAlpha = s.ga;
    shadowBlur = s.blur; lineDashOffset = s.dashOff; letterSpacing = s.spacing; lineCap = s.cap; roundJoin = s.rj;
    textAlign = s.al; textBaseline = s.bl; font_ = s.font; fontPx_ = s.px; dash_ = s.dash; clip_ = s.clip;
    std::memcpy(m_, s.m, sizeof m_);
    stack_.pop_back();
}

void Canvas::translate(float x, float y) { m_[4] += m_[0] * x + m_[2] * y; m_[5] += m_[1] * x + m_[3] * y; }
void Canvas::rotate(float a) {
    const float c = std::cos(a), s = std::sin(a);
    const float a0 = m_[0], b0 = m_[1], c0 = m_[2], d0 = m_[3];
    m_[0] = a0 * c + c0 * s; m_[1] = b0 * c + d0 * s;
    m_[2] = -a0 * s + c0 * c; m_[3] = -b0 * s + d0 * c;
}
void Canvas::scale(float x, float y) { m_[0] *= x; m_[1] *= x; m_[2] *= y; m_[3] *= y; }
void Canvas::setTransform(float a, float b, float c, float d, float e, float f) {
    m_[0] = a; m_[1] = b; m_[2] = c; m_[3] = d; m_[4] = e; m_[5] = f;
}
float Canvas::scaleOf() const { return std::sqrt(std::fabs(m_[0] * m_[3] - m_[1] * m_[2])); }

ImU32 Canvas::col(const Color& c, float k) const {
    const float a = clampf(c.a * globalAlpha * k, 0, 1);
    return IM_COL32((int)(clampf(c.r, 0, 1) * 255 + 0.5f), (int)(clampf(c.g, 0, 1) * 255 + 0.5f),
                    (int)(clampf(c.b, 0, 1) * 255 + 0.5f), (int)(a * 255 + 0.5f));
}

void Canvas::lighter(bool on) {
    if (on == lighter_ || !dl_) return;
    lighter_ = on;
    dl_->AddCallback(on ? blendLighter : blendNormal, nullptr);
}

// ---- paths ------------------------------------------------------------------------------------------------------
Canvas::Sub& Canvas::cur() { if (path_.empty()) path_.emplace_back(); return path_.back(); }
void Canvas::beginPath() { path_.clear(); }
void Canvas::moveTo(float x, float y) { path_.emplace_back(); path_.back().p.push_back(tf(x, y)); }
void Canvas::lineTo(float x, float y) {
    if (path_.empty()) { moveTo(x, y); return; }
    cur().p.push_back(tf(x, y));
}
void Canvas::closePath() {
    if (path_.empty() || path_.back().p.empty()) return;
    path_.back().closed = true;
    const ImVec2 first = path_.back().p.front();
    path_.emplace_back();
    path_.back().p.push_back(first);
}

void Canvas::ellipse(float x, float y, float rx, float ry, float rot, float a0, float a1, bool ccw) {
    // the end angle as Chrome settles it (arc(x, y, r, 0, 2 pi, true) is a whole circle there)
    float end = a1;
    if (!ccw && a1 - a0 >= TAU) end = a0 + TAU;
    else if (ccw && a0 - a1 >= TAU) end = a0 - TAU;
    else if (!ccw && a0 > a1) end = a0 + (TAU - std::fmod(a0 - a1, TAU));
    else if (ccw && a0 < a1) end = a0 - (TAU - std::fmod(a1 - a0, TAU));
    const float sweep = end - a0;
    const float rs = std::max(std::fabs(rx), std::fabs(ry)) * scaleOf();
    float step = rs > 0.3f ? 2.f * std::acos(std::max(-1.f, 1.f - 0.25f / rs)) : 0.8f;
    step = clampf(step, 0.012f, 0.8f);
    const int n = std::max(1, std::min(720, (int)std::ceil(std::fabs(sweep) / step)));
    const float cr = std::cos(rot), sr = std::sin(rot);
    if (std::fabs(sweep) >= TAU - 1e-4f && !path_.empty() && !path_.back().p.empty()) {
        // a whole circle added to a shape: the browser joins it with a line, which only shows when stroked; filled,
        // it's a shape of its own (two eyes in one path, a ring with a hole)
        const float lx = std::cos(a0) * rx, ly = std::sin(a0) * ry;
        const ImVec2 s = tf(x + lx * cr - ly * sr, y + lx * sr + ly * cr), last = path_.back().p.back();
        if (std::fabs(s.x - last.x) > 0.5f || std::fabs(s.y - last.y) > 0.5f) {
            path_.emplace_back();
            path_.back().p = {last, s};
        }
        path_.emplace_back();
    }
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + sweep * i / n;
        const float lx = std::cos(a) * rx, ly = std::sin(a) * ry;
        const float px = x + lx * cr - ly * sr, py = y + lx * sr + ly * cr;
        if (i == 0 && (path_.empty() || path_.back().p.empty())) moveTo(px, py);
        else lineTo(px, py);
    }
}
void Canvas::arc(float x, float y, float r, float a0, float a1, bool ccw) { ellipse(x, y, r, r, 0, a0, a1, ccw); }

void Canvas::quadraticCurveTo(float cx, float cy, float x, float y) {
    if (path_.empty() || path_.back().p.empty()) moveTo(cx, cy);
    const ImVec2 p0 = path_.back().p.back(), p1 = tf(cx, cy), p2 = tf(x, y);
    const float len = std::hypot(p1.x - p0.x, p1.y - p0.y) + std::hypot(p2.x - p1.x, p2.y - p1.y);
    const int n = std::max(2, std::min(40, (int)(len / 6)));
    for (int i = 1; i <= n; ++i) {
        const float t = (float)i / n, u = 1 - t;
        path_.back().p.push_back({u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x, u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y});
    }
}

void Canvas::rect(float x, float y, float w, float h) {
    moveTo(x, y); lineTo(x + w, y); lineTo(x + w, y + h); lineTo(x, y + h); closePath();
}

// ---- filling ----------------------------------------------------------------------------------------------------
void Canvas::fillPoly(const std::vector<ImVec2>& in, ImU32 c) {
    std::vector<ImVec2> p = in;
    dedupe(p);
    if (p.size() < 3) return;
    if (!clip_.empty()) {
        if (!convex(p)) {                         // clip a concave shape as its triangles
            std::vector<int> tris;
            if (!triangulate(p, tris)) return;
            for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                std::vector<ImVec2> t = clipPoly({p[tris[i]], p[tris[i + 1]], p[tris[i + 2]]}, clip_);
                if (t.size() >= 3) { if (area(t) < 0) std::reverse(t.begin(), t.end()); dl_->AddConvexPolyFilled(t.data(), (int)t.size(), c); }
            }
            return;
        }
        p = clipPoly(p, clip_);
        if (p.size() < 3) return;
    }
    if (convex(p)) {
        if (area(p) < 0) std::reverse(p.begin(), p.end());
        dl_->AddConvexPolyFilled(p.data(), (int)p.size(), c);
        return;
    }
    std::vector<int> tris;
    if (!triangulate(p, tris) || tris.empty()) {
        fillEvenOdd({p}, c);
        return;
    }
    const ImVec2 uv = dl_->_Data->TexUvWhitePixel;
    dl_->PrimReserve((int)tris.size(), (int)p.size());
    const unsigned base = dl_->_VtxCurrentIdx;
    for (const ImVec2& q : p) dl_->PrimWriteVtx(q, uv, c);
    for (int t : tris) dl_->PrimWriteIdx((ImDrawIdx)(base + t));
}

void Canvas::fillEvenOdd(const std::vector<std::vector<ImVec2>>& polys, ImU32 c) {
    float y0 = 1e9f, y1 = -1e9f;
    for (const auto& p : polys) for (const ImVec2& q : p) { y0 = std::min(y0, q.y); y1 = std::max(y1, q.y); }
    y0 = std::max(y0, -1.f); y1 = std::min(y1, H_ + 1.f);
    std::vector<float> xs;
    for (float y = std::floor(y0); y < y1; y += 1.f) {
        const float sy = y + 0.5f;
        xs.clear();
        for (const auto& p : polys)
            for (size_t i = 0, n = p.size(); i < n; ++i) {
                const ImVec2 a = p[i], b = p[(i + 1) % n];
                if ((a.y <= sy) != (b.y <= sy)) xs.push_back(a.x + (sy - a.y) / (b.y - a.y) * (b.x - a.x));
            }
        std::sort(xs.begin(), xs.end());
        float cl = -1e9f, cr = 1e9f;
        if (!clip_.empty()) {                     // the convex clip's span on this row
            std::vector<float> cx;
            for (size_t i = 0, n = clip_.size(); i < n; ++i) {
                const ImVec2 a = clip_[i], b = clip_[(i + 1) % n];
                if ((a.y <= sy) != (b.y <= sy)) cx.push_back(a.x + (sy - a.y) / (b.y - a.y) * (b.x - a.x));
            }
            if (cx.size() < 2) continue;
            cl = *std::min_element(cx.begin(), cx.end()); cr = *std::max_element(cx.begin(), cx.end());
        }
        for (size_t i = 0; i + 1 < xs.size(); i += 2) {
            const float a = std::max(xs[i], cl), b = std::min(xs[i + 1], cr);
            if (b > a) dl_->AddRectFilled({a, y}, {b, y + 1.f}, c);
        }
    }
}

void Canvas::fill(bool evenOdd) {
    std::vector<std::vector<ImVec2>> polys;
    for (const Sub& s : path_) if (s.p.size() >= 3) polys.push_back(s.p);
    if (polys.empty()) return;
    for (const auto& p : polys) glowPoly(p, true, 0);
    const ImU32 c = col(fillStyle);
    if (evenOdd && polys.size() > 1) fillEvenOdd(polys, c);
    else for (const auto& p : polys) fillPoly(p, c);
}

void Canvas::fillRect(float x, float y, float w, float h) {
    std::vector<ImVec2> p = {tf(x, y), tf(x + w, y), tf(x + w, y + h), tf(x, y + h)};
    glowPoly(p, true, 0);
    const ImU32 c = col(fillStyle);
    if (clip_.empty() && m_[1] == 0 && m_[2] == 0) {
        dl_->AddRectFilled({std::min(p[0].x, p[2].x), std::min(p[0].y, p[2].y)}, {std::max(p[0].x, p[2].x), std::max(p[0].y, p[2].y)}, c);
        return;
    }
    fillPoly(p, c);
}

void Canvas::radialDisc(float x, float y, float r, Color inner, Color outer) {
    const ImVec2 c = tf(x, y);
    const float R = r * scaleOf();
    const int n = std::max(12, std::min(96, (int)(R / 4)));
    const ImU32 ci = col(inner), co = col(outer);
    const ImVec2 uv = dl_->_Data->TexUvWhitePixel;
    dl_->PrimReserve(n * 3, n + 1);
    const unsigned base = dl_->_VtxCurrentIdx;
    dl_->PrimWriteVtx(c, uv, ci);
    for (int i = 0; i < n; ++i) {
        const float a = TAU * i / n;
        dl_->PrimWriteVtx({c.x + std::cos(a) * R, c.y + std::sin(a) * R}, uv, co);
    }
    for (int i = 0; i < n; ++i) {
        dl_->PrimWriteIdx((ImDrawIdx)base);
        dl_->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
        dl_->PrimWriteIdx((ImDrawIdx)(base + 1 + (i + 1) % n));
    }
}

// ---- stroking ---------------------------------------------------------------------------------------------------
void Canvas::strokePoly(const std::vector<ImVec2>& in, bool closed, ImU32 c, float w, bool caps) {
    // no zero-length pieces (ImGui's joins spike on them); a shape that ends where it starts is closed
    std::vector<ImVec2> pts;
    pts.reserve(in.size());
    for (const ImVec2& q : in)
        if (pts.empty() || std::fabs(pts.back().x - q.x) > 0.01f || std::fabs(pts.back().y - q.y) > 0.01f) pts.push_back(q);
    if (pts.size() > 2 && std::fabs(pts.front().x - pts.back().x) < 0.01f && std::fabs(pts.front().y - pts.back().y) < 0.01f) {
        pts.pop_back();
        closed = true;
    }
    if (pts.size() < 2) return;
    std::vector<std::vector<ImVec2>> runs;
    std::vector<ImVec2> src = pts;
    if (closed && !src.empty()) src.push_back(src.front());
    // dashes
    if (!dash_.empty()) {
        std::vector<float> d = dash_;
        if (d.size() % 2) d.insert(d.end(), dash_.begin(), dash_.end());
        float total = 0;
        for (float v : d) total += v;
        const float sc = scaleOf();
        if (total * sc > 0.5f) {
            for (float& v : d) v *= sc;
            total *= sc;
            float off = std::fmod(lineDashOffset * sc, total);
            if (off < 0) off += total;
            size_t di = 0;
            while (off >= d[di]) { off -= d[di]; di = (di + 1) % d.size(); }
            float left = d[di] - off;
            bool on = di % 2 == 0;
            std::vector<ImVec2> run;
            if (on && !src.empty()) run.push_back(src[0]);
            for (size_t i = 0; i + 1 < src.size(); ++i) {
                ImVec2 a = src[i];
                const ImVec2 b = src[i + 1];
                float seg = std::hypot(b.x - a.x, b.y - a.y);
                while (seg > 0) {
                    if (left >= seg) {
                        left -= seg; seg = 0;
                        if (on) run.push_back(b);
                    } else {
                        const float t = left / seg;
                        const ImVec2 m = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
                        if (on) { run.push_back(m); if (run.size() >= 2) runs.push_back(run); run.clear(); }
                        else run.assign(1, m);
                        seg -= left; a = m;
                        di = (di + 1) % d.size(); left = d[di]; on = !on;
                    }
                }
            }
            if (on && run.size() >= 2) runs.push_back(run);
            closed = false;
        } else runs.push_back(src);
    } else runs.push_back(closed ? pts : src);
    // the clip
    if (!clip_.empty()) {
        std::vector<std::vector<ImVec2>> clipped;
        for (auto& r : runs) {
            std::vector<ImVec2> rr = r;
            if (closed) rr.push_back(rr.front());
            std::vector<ImVec2> cur;
            for (size_t i = 0; i + 1 < rr.size(); ++i) {
                const ImVec2 p = rr[i], q = rr[i + 1];
                float t0 = 0, t1 = 1;
                bool out = false;
                for (size_t k = 0, n = clip_.size(); k < n && !out; ++k) {
                    const float f0 = side(clip_[k], clip_[(k + 1) % n], p), f1 = side(clip_[k], clip_[(k + 1) % n], q);
                    if (f0 < 0 && f1 < 0) out = true;
                    else if (f0 < 0) t0 = std::max(t0, f0 / (f0 - f1));
                    else if (f1 < 0) t1 = std::min(t1, f0 / (f0 - f1));
                }
                if (out || t0 >= t1) { if (cur.size() >= 2) clipped.push_back(cur); cur.clear(); continue; }
                const ImVec2 a = {p.x + (q.x - p.x) * t0, p.y + (q.y - p.y) * t0}, b = {p.x + (q.x - p.x) * t1, p.y + (q.y - p.y) * t1};
                if (cur.empty() || t0 > 0) { if (cur.size() >= 2) clipped.push_back(cur); cur.assign(1, a); }
                cur.push_back(b);
                if (t1 < 1) { clipped.push_back(cur); cur.clear(); }
            }
            if (cur.size() >= 2) clipped.push_back(cur);
        }
        runs.swap(clipped);
        closed = false;
    }
    for (auto& r : runs) {
        if (r.size() < 2) continue;
        dl_->AddPolyline(r.data(), (int)r.size(), c, closed ? ImDrawFlags_Closed : ImDrawFlags_None, w);
        if (caps && lineCap == Cap::Round && w > 2.f && !closed) {
            dl_->AddCircleFilled(r.front(), w * 0.5f, c, 12);
            dl_->AddCircleFilled(r.back(), w * 0.5f, c, 12);
        }
        if (caps && roundJoin && w > 3.f)
            for (size_t i = 1; i + 1 < r.size(); ++i) dl_->AddCircleFilled(r[i], w * 0.5f, c, 12);
    }
}

void Canvas::glowPoly(const std::vector<ImVec2>& pts, bool closed, float baseW) {
    if (shadowBlur <= 0.5f || shadowColor.a <= 0) return;
    const float b = shadowBlur;
    std::vector<float> keep = dash_;
    const float k[3][2] = {{1.0f, 0.07f}, {0.6f, 0.10f}, {0.3f, 0.14f}};
    for (const auto& kk : k) {
        const float w = baseW + b * kk[0];
        if (w < 1.5f) continue;
        strokePoly(pts, closed, col(shadowColor, kk[1]), w, false);
    }
    dash_ = keep;
}

void Canvas::stroke() {
    const float w = std::max(0.5f, lineWidth * scaleOf());
    const ImU32 c = col(strokeStyle);
    for (const Sub& s : path_) {
        if (s.p.size() < 2) continue;
        glowPoly(s.p, s.closed, w);
        strokePoly(s.p, s.closed, c, w, true);
    }
}

void Canvas::strokeRect(float x, float y, float w, float h) {
    std::vector<ImVec2> p = {tf(x, y), tf(x + w, y), tf(x + w, y + h), tf(x, y + h)};
    const float lw = std::max(0.5f, lineWidth * scaleOf());
    glowPoly(p, true, lw);
    strokePoly(p, true, col(strokeStyle), lw, true);
}

void Canvas::clip() {
    for (const Sub& s : path_) {
        if (s.p.size() < 3) continue;
        std::vector<ImVec2> p = s.p;
        dedupe(p);
        if (p.size() < 3) continue;
        if (area(p) < 0) std::reverse(p.begin(), p.end());
        clip_ = clip_.empty() ? p : clipPoly(p, clip_);
        if (clip_.size() < 3) clip_ = {ImVec2(-10, -10), ImVec2(-9, -10), ImVec2(-9, -9)};   // nothing shows
        return;
    }
}

// ---- text -------------------------------------------------------------------------------------------------------
float Canvas::measureText(const std::string& s) const {
    const Baked* b = pick(font_, fontPx_);
    ImFont* f = b ? b->f : ImGui::GetFont();
    float w = f->CalcTextSizeA(fontPx_ * (b ? b->em : 1.f), FLT_MAX, 0.f, s.c_str()).x;
    if (letterSpacing != 0) {
        int n = 0;
        for (const char* p = s.c_str(); *p;) { unsigned c; p += ImTextCharFromUtf8(&c, p, nullptr); ++n; }
        w += letterSpacing * n;
    }
    return w;
}

void Canvas::textAt(const std::string& s, float x, float y, ImU32 c, float dx, float dy) {
    const float sc = scaleOf(), size = fontPx_ * sc, draw = size * emOf();
    ImFont* f = imfont();
    const ImVec2 at = tf(x, y);
    const float w = measureText(s) * sc;
    float left = at.x;
    if (textAlign == Align::Center) left -= w * 0.5f;
    else if (textAlign == Align::Right) left -= w;
    const float k = draw / f->FontSize;
    float top;
    switch (textBaseline) {
    case Baseline::Middle: top = at.y + 0.3465f * size - f->Ascent * k; break;
    case Baseline::Top: top = at.y + 0.9f * size - f->Ascent * k; break;
    case Baseline::Bottom: top = at.y - 0.212f * size - f->Ascent * k; break;
    default: top = at.y - f->Ascent * k; break;
    }
    left += dx; top += dy;
    if (letterSpacing == 0) { dl_->AddText(f, draw, {left, top}, c, s.c_str()); return; }
    char buf[8];
    for (const char* p = s.c_str(); *p;) {
        unsigned ch;
        const int n = ImTextCharFromUtf8(&ch, p, nullptr);
        std::memcpy(buf, p, n); buf[n] = 0;
        dl_->AddText(f, draw, {left, top}, c, buf);
        left += f->CalcTextSizeA(draw, FLT_MAX, 0.f, buf).x + letterSpacing * sc;
        p += n;
    }
}

void Canvas::fillText(const std::string& s, float x, float y) {
    if (s.empty()) return;
    if (shadowBlur > 0.5f && shadowColor.a > 0) {          // a soft halo round the letters
        const float r = std::min(4.f, shadowBlur * 0.16f);
        const ImU32 g = col(shadowColor, 0.22f);
        for (int i = 0; i < 8; ++i) {
            const float a = TAU * i / 8;
            textAt(s, x, y, g, std::cos(a) * r, std::sin(a) * r);
        }
    }
    textAt(s, x, y, col(fillStyle));
}

void Canvas::strokeText(const std::string& s, float x, float y, float width) {
    const float r = width * 0.5f * scaleOf();
    const ImU32 c = col(strokeStyle);
    const int n = r > 1.5f ? 16 : 8;
    for (int i = 0; i < n; ++i) {
        const float a = TAU * i / n;
        textAt(s, x, y, c, std::cos(a) * r, std::sin(a) * r);
    }
}

// ---- images -----------------------------------------------------------------------------------------------------
void Canvas::drawImage(const Texture& t, float x, float y, float w, float h) {
    if (!t.id) return;
    dl_->AddImageQuad((ImTextureID)(intptr_t)t.id, tf(x, y), tf(x + w, y), tf(x + w, y + h), tf(x, y + h),
                      {0, 0}, {1, 0}, {1, 1}, {0, 1}, col(Color(1, 1, 1)));
}

void Canvas::grabScreen() {
    if (!grabTex) {
        glGenTextures(1, &grabTex);
        glBindTexture(GL_TEXTURE_2D, grabTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);   // GL_CLAMP_TO_EDGE
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    }
    dl_->AddCallback(grabCallback, nullptr);
}

void Canvas::drawGrab(float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh) {
    if (!grabTex) return;
    const ImVec2 uv0 = {sx / W_, 1.f - sy / H_}, uv1 = {(sx + sw) / W_, 1.f - (sy + sh) / H_};
    dl_->AddImage((ImTextureID)(intptr_t)grabTex, tf(dx, dy), tf(dx + dw, dy + dh), uv0, uv1, col(Color(1, 1, 1)));
}
