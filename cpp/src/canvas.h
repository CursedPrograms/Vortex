#pragma once
// canvas.h - a small HTML5 canvas 2D context on top of Dear ImGui's draw lists, so the browser game's drawing code
// ports line for line: paths (lines, arcs, ellipses, curves), fill and stroke, the transform stack, globalAlpha,
// dashes, a round clip, glows (shadowBlur), "lighter" blending, text in Arial / Arial Black, and images.
#include <imgui.h>
#include <string>
#include <vector>

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    Color() = default;
    Color(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
    Color(const char* css);                       // "#f088e9", "#fff", "rgb(..)", "rgba(..)", "hsl(..)"
    Color(const std::string& css) : Color(css.c_str()) {}
    Color alpha(float k) const { return Color(r, g, b, a * k); }
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
};
Color rgb(int r, int g, int b, float a = 1.f);

struct Texture { unsigned id = 0; int w = 0, h = 0; };

enum class Font { Regular, Bold, Black };
enum class Align { Left, Center, Right };
enum class Baseline { Alphabetic, Middle, Top, Bottom };
enum class Cap { Butt, Round, Square };

class Canvas {
public:
    // the frame: everything goes into dl, W x H in logical pixels
    void begin(ImDrawList* dl, float W, float H);
    static void loadFonts();                      // once, before the ImGui font atlas is built

    // state (saved and restored by save() / restore())
    Color fillStyle{1, 1, 1, 1}, strokeStyle{0, 0, 0, 1}, shadowColor{0, 0, 0, 0};
    float lineWidth = 1.f, globalAlpha = 1.f, shadowBlur = 0.f, lineDashOffset = 0.f, letterSpacing = 0.f;
    Cap lineCap = Cap::Butt;
    bool roundJoin = false;
    Align textAlign = Align::Left;
    Baseline textBaseline = Baseline::Alphabetic;
    void font(Font f, float px) { font_ = f; fontPx_ = px; }
    void setLineDash(std::vector<float> d) { dash_ = std::move(d); }
    void lighter(bool on);                        // globalCompositeOperation "lighter" / "source-over"

    void save();
    void restore();
    void translate(float x, float y);
    void rotate(float a);
    void scale(float x, float y);
    void setTransform(float a, float b, float c, float d, float e, float f);
    void resetTransform() { setTransform(1, 0, 0, 1, 0, 0); }

    // paths
    void beginPath();
    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void closePath();
    void arc(float x, float y, float r, float a0, float a1, bool ccw = false);
    void ellipse(float x, float y, float rx, float ry, float rot, float a0, float a1, bool ccw = false);
    void quadraticCurveTo(float cx, float cy, float x, float y);
    void rect(float x, float y, float w, float h);
    void fill(bool evenOdd = false);
    void stroke();
    void clip();                                  // the current path's first shape (convex) becomes the clip

    void fillRect(float x, float y, float w, float h);
    void strokeRect(float x, float y, float w, float h);
    void fillText(const std::string& s, float x, float y);
    void strokeText(const std::string& s, float x, float y, float width);   // an outline round the letters
    float measureText(const std::string& s) const;

    // a radial glow cloud (createRadialGradient with two stops, drawn as a disc)
    void radialDisc(float x, float y, float r, Color inner, Color outer);
    void drawImage(const Texture& t, float x, float y, float w, float h);

    // the screen as drawn so far, for effects like the tearing in VORTEX (copied at this point in the frame)
    void grabScreen();
    void drawGrab(float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh);

    float width() const { return W_; }
    float height() const { return H_; }

private:
    struct Sub { std::vector<ImVec2> p; bool closed = false; };
    struct State {
        Color fill, stroke, shadow; float lw, ga, blur, dashOff, spacing; Cap cap; bool rj; Align al; Baseline bl;
        Font font; float px; std::vector<float> dash; float m[6]; std::vector<ImVec2> clip;
    };
    ImDrawList* dl_ = nullptr;
    float W_ = 0, H_ = 0;
    float m_[6] = {1, 0, 0, 1, 0, 0};
    std::vector<Sub> path_;
    std::vector<ImVec2> clip_;                    // screen-space convex polygon, empty = none
    std::vector<State> stack_;
    std::vector<float> dash_;
    Font font_ = Font::Regular;
    float fontPx_ = 10.f;
    bool lighter_ = false;

    ImVec2 tf(float x, float y) const { return {m_[0] * x + m_[2] * y + m_[4], m_[1] * x + m_[3] * y + m_[5]}; }
    float scaleOf() const;
    ImU32 col(const Color& c, float k = 1.f) const;
    Sub& cur();
    void fillPoly(const std::vector<ImVec2>& pts, ImU32 c);
    void fillEvenOdd(const std::vector<std::vector<ImVec2>>& polys, ImU32 c);
    void strokePoly(const std::vector<ImVec2>& pts, bool closed, ImU32 c, float w, bool caps);
    void glowPoly(const std::vector<ImVec2>& pts, bool closed, float baseW);
    void textAt(const std::string& s, float x, float y, ImU32 c, float dx = 0, float dy = 0);
    ImFont* imfont() const;
    float emOf() const;
};
