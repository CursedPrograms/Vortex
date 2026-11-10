#pragma once
// splash.h - the browser's splash is an HTML column (title, tagline, Cursed logo, copyright, how to play, "press any
// key"), centred over the attract mode. This lays such a column out on the canvas the same way.
#include "canvas.h"
#include <algorithm>
#include <string>
#include <vector>

struct SplashItem {
    std::string text;                 // empty with an image: the image
    const Texture* image = nullptr;
    float imageW = 0;
    Font font = Font::Regular;
    float px = 15, spacing = 0, lineH = 1.2f, marginTop = 0, marginBottom = 0;
    Color color{1, 1, 1};
    Color stroke{0, 0, 0, 0};         // -webkit-text-stroke
    float strokeW = 0;
    float alpha = 1;                  // for blinking lines
    std::vector<SplashItem> inline_;  // a title made of parts in different sizes (ROCKS <small>OF</small> RUIN)
};

inline float splashItemH(const SplashItem& it) {
    if (it.image) return it.image->w ? it.imageW * it.image->h / it.image->w : 0;
    int lines = 1 + (int)std::count(it.text.begin(), it.text.end(), '\n');
    return it.px * it.lineH * lines;
}

// draws the column centred in W x H with gap between items; backdrop: the overlay colour
inline void drawSplash(Canvas& c, const std::vector<SplashItem>& items, float gap, Color backdrop, float fade) {
    if (fade <= 0) return;
    const float W = c.width(), H = c.height();
    c.save();
    c.globalAlpha = fade;
    c.fillStyle = backdrop;
    c.fillRect(0, 0, W, H);
    float total = 0;
    for (size_t i = 0; i < items.size(); ++i) total += splashItemH(items[i]) + items[i].marginTop + items[i].marginBottom + (i ? gap : 0);
    float y = (H - total) / 2;
    c.textAlign = Align::Center;
    c.textBaseline = Baseline::Middle;
    for (size_t i = 0; i < items.size(); ++i) {
        const SplashItem& it = items[i];
        if (i) y += gap;
        y += it.marginTop;
        const float h = splashItemH(it);
        c.globalAlpha = fade * it.alpha;
        if (it.image) {
            c.drawImage(*it.image, W / 2 - it.imageW / 2, y, it.imageW, h);
        } else if (!it.inline_.empty()) {
            // parts side by side, each centred on the line's middle
            float tw = 0;
            std::vector<float> ws;
            for (const SplashItem& p : it.inline_) {
                c.font(p.font, p.px); c.letterSpacing = p.spacing;
                ws.push_back(c.measureText(p.text));
                tw += ws.back();
            }
            float x = W / 2 - tw / 2;
            c.textAlign = Align::Left;
            for (size_t k = 0; k < it.inline_.size(); ++k) {
                const SplashItem& p = it.inline_[k];
                c.font(p.font, p.px); c.letterSpacing = p.spacing;
                if (p.strokeW > 0) { c.strokeStyle = p.stroke; c.strokeText(p.text, x, y + h / 2, p.strokeW); }
                c.fillStyle = p.color;
                c.fillText(p.text, x, y + h / 2);
                x += ws[k];
            }
            c.textAlign = Align::Center;
        } else {
            c.font(it.font, it.px);
            c.letterSpacing = it.spacing;
            size_t start = 0;
            float ly = y + it.px * it.lineH / 2;
            while (true) {
                const size_t nl = it.text.find('\n', start);
                const std::string line = it.text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                // letter-spacing adds after the last letter too, as in CSS: nudge back by half of it
                const float nudge = it.spacing / 2;
                if (it.strokeW > 0) { c.strokeStyle = it.stroke; c.strokeText(line, W / 2 + nudge, ly, it.strokeW); }
                c.fillStyle = it.color;
                c.fillText(line, W / 2 + nudge, ly);
                if (nl == std::string::npos) break;
                start = nl + 1;
                ly += it.px * it.lineH;
            }
            c.letterSpacing = 0;
        }
        y += h + it.marginBottom;
    }
    c.restore();
}
