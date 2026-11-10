#pragma once
// game.h - what main.cpp asks of the game (game.cpp, ported from the browser's site/game.js), and what it offers back.
#include "canvas.h"
#include <string>

namespace Game {
const char* title();                              // the window's title
void init(float W, float H);                      // once, with the window's size
void resize(float W, float H);
void frame(Canvas& c, double nowMs);              // the browser's requestAnimationFrame loop: update and draw
// keys arrive named as the browser's e.key: "ArrowLeft", "Escape", "Enter", "Tab", " ", letters in lower case
void keyDown(const std::string& key, bool repeat);
void keyUp(const std::string& key);
void pointerDown(float x, float y);
void pointerMove(float x, float y);
void pointerUp(float x, float y);
void blur();                                      // the window lost focus
bool started();                                   // past the splash
}  // namespace Game

namespace Platform {
std::string load(const std::string& key, const std::string& def);   // localStorage, kept in the user's settings folder
void store(const std::string& key, const std::string& value);
Texture texture(const char* file);                // a PNG from assets/
void quit();
}  // namespace Platform
