/* SDL entry for the Android activity. GLES 3 context only: the decomp's desktop GL renderer is not linked. */
#include <SDL.h>
#include <GLES3/gl3.h>

int main(int argc, char* argv[]) {
    SDL_Window* window;
    SDL_GLContext gl;
    int have_rom = 0;
    int i;
    for (i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--rom") == 0 && i + 1 < argc) have_rom = 1;
    }
    SDL_Log("ACGC Town Android: GLES3 shell. ROM %s. Play Online is the desktop UI once the game library is linked. No new network packets.",
            have_rom ? "selected" : "missing");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    window = SDL_CreateWindow("ACGC Town", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (window == NULL) {
        SDL_Log("window failed: %s", SDL_GetError());
        return 1;
    }
    gl = SDL_GL_CreateContext(window);
    if (gl == NULL) {
        SDL_Log("GLES context failed: %s", SDL_GetError());
        return 1;
    }
    if (have_rom) glClearColor(0.15f, 0.35f, 0.22f, 1.0f);
    else glClearColor(0.35f, 0.08f, 0.08f, 1.0f);
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return 0;
        }
        glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }
}
