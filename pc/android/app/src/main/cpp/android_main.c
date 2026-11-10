/* UNUSED shell. Gradle does not compile this file. The APK links the decomp
 * through pc/CMakeLists.txt. Kept so the old standalone CMake still configures. */
#include <SDL.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <unistd.h>

static void read_insets(int* l, int* t, int* r, int* b) {
    FILE* f = fopen("safe_insets.txt", "r");
    *l = *t = *r = *b = 0;
    if (f == NULL) return;
    if (fscanf(f, "%d %d %d %d", l, t, r, b) != 4) {
        *l = *t = *r = *b = 0;
    }
    fclose(f);
}

int main(int argc, char* argv[]) {
    SDL_Window* window;
    SDL_GLContext gl;
    int have_rom = 0;
    int i;
    for (i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--rom") == 0 && i + 1 < argc) have_rom = 1;
    }
    SDL_Log("ACGC Town %s shell. pointer=%u. ROM %s. Town load waits on an arm64 Save_t that matches the 32-bit server.",
            sizeof(void*) == 8 ? "arm64" : "arm32", (unsigned)sizeof(void*), have_rom ? "selected" : "missing");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    {
        const char* storage = SDL_AndroidGetInternalStoragePath();
        if (storage != NULL && storage[0] != '\0') chdir(storage);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    window = SDL_CreateWindow("ACGC Town", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        SDL_Log("window failed: %s", SDL_GetError());
        return 1;
    }
    gl = SDL_GL_CreateContext(window);
    if (gl == NULL) {
        SDL_Log("GLES context failed: %s", SDL_GetError());
        return 1;
    }
    for (;;) {
        SDL_Event e;
        int insets[4];
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return 0;
            if (e.type == SDL_APP_DIDENTERFOREGROUND) SDL_GL_MakeCurrent(window, gl);
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_AC_BACK) {
                SDL_Log("back pressed (shell has no pause menu yet)");
            }
        }
        read_insets(&insets[0], &insets[1], &insets[2], &insets[3]);
        (void)insets;
        if (have_rom) glClearColor(0.12f, 0.32f, 0.22f, 1.0f);
        else glClearColor(0.28f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }
}
