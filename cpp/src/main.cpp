// main.cpp - the window, the keyboard and mouse, the settings file, and the frame loop. The game itself, splash
// included, is game.cpp, drawn through the canvas (canvas.cpp) and heard through the synth (synth.cpp).
//
//   F11 or Alt+Enter   fullscreen          Esc on the splash   quit
//   everything else goes to the game, as the browser version has it
#include "game.h"
#include "synth.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <fstream>
#include <map>
#include <string>

namespace {
std::string base;                                 // the folder the exe is in
std::map<std::string, std::string> settings;
std::string settingsFile;
bool quitting = false;

void readSettings() {
    char* p = SDL_GetPrefPath("Cursed Entertainment", Game::title());
    settingsFile = p ? std::string(p) + "settings.txt" : "settings.txt";
    SDL_free(p);
    std::ifstream f(settingsFile);
    std::string line;
    while (std::getline(f, line)) {
        const size_t eq = line.find('=');
        if (eq != std::string::npos) settings[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

std::string keyName(SDL_Keycode k) {
    switch (k) {
    case SDLK_LEFT: return "ArrowLeft";
    case SDLK_RIGHT: return "ArrowRight";
    case SDLK_UP: return "ArrowUp";
    case SDLK_DOWN: return "ArrowDown";
    case SDLK_ESCAPE: return "Escape";
    case SDLK_RETURN: case SDLK_KP_ENTER: return "Enter";
    case SDLK_TAB: return "Tab";
    case SDLK_SPACE: return " ";
    case SDLK_LSHIFT: case SDLK_RSHIFT: return "Shift";
    case SDLK_LCTRL: case SDLK_RCTRL: return "Control";
    case SDLK_LALT: case SDLK_RALT: return "Alt";
    default: break;
    }
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return std::string(1, (char)('1' + (k - SDLK_KP_1)));
    if (k == SDLK_KP_0) return "0";
    if (k > 32 && k < 127) return std::string(1, (char)k);
    return "";
}
}  // namespace

std::string Platform::load(const std::string& key, const std::string& def) {
    auto it = settings.find(key);
    return it == settings.end() ? def : it->second;
}

void Platform::store(const std::string& key, const std::string& value) {
    settings[key] = value;
    std::ofstream f(settingsFile);
    for (const auto& kv : settings) f << kv.first << "=" << kv.second << "\n";
}

Texture Platform::texture(const char* file) {
    Texture t;
    int n = 0;
    unsigned char* px = stbi_load((base + "assets/" + file).c_str(), &t.w, &t.h, &n, 4);
    if (!px) return t;
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    t.id = id;
    return t;
}

void Platform::quit() { quitting = true; }

int main(int, char**) {
    char* bp = SDL_GetBasePath();
    base = bp ? bp : "";
    SDL_free(bp);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) return 1;
    readSettings();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* win = SDL_CreateWindow(Game::title(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) return 1;
    SDL_SetWindowMinimumSize(win, 640, 400);
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    Canvas::loadFonts();
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    if (SDL_Surface* icon = SDL_LoadBMP((base + "assets/icon.bmp").c_str())) { SDL_SetWindowIcon(win, icon); SDL_FreeSurface(icon); }
    Synth::open();

    int w, h;
    SDL_GetWindowSize(win, &w, &h);
    Game::init((float)w, (float)h);
    float lastW = (float)w, lastH = (float)h;
    bool fullscreen = false;
    Canvas canvas;

    while (!quitting) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            switch (e.type) {
            case SDL_QUIT: quitting = true; break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) Game::blur();
                break;
            case SDL_KEYDOWN: {
                const SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    if (!e.key.repeat) {
                        fullscreen = !fullscreen;
                        SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    }
                    break;
                }
                if (k == SDLK_ESCAPE && !Game::started()) { quitting = true; break; }
                const std::string n = keyName(k);
                if (!n.empty()) Game::keyDown(n, e.key.repeat != 0);
                break;
            }
            case SDL_KEYUP: {
                const std::string n = keyName(e.key.keysym.sym);
                if (!n.empty()) Game::keyUp(n);
                break;
            }
            case SDL_MOUSEBUTTONDOWN: Game::pointerDown((float)e.button.x, (float)e.button.y); break;
            case SDL_MOUSEMOTION: Game::pointerMove((float)e.motion.x, (float)e.motion.y); break;
            case SDL_MOUSEBUTTONUP: Game::pointerUp((float)e.button.x, (float)e.button.y); break;
            default: break;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const float W = io.DisplaySize.x, H = io.DisplaySize.y;
        if (W != lastW || H != lastH) { lastW = W; lastH = H; Game::resize(W, H); }
        canvas.begin(ImGui::GetBackgroundDrawList(), W, H);
        Game::frame(canvas, (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency());
        canvas.lighter(false);

        ImGui::Render();
        int dw, dh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.008f, 0.024f, 0.067f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }

    Synth::close();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
