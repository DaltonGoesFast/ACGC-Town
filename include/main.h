#ifndef MAIN_H
#define MAIN_H

#include "types.h"
#include "dolphin/os/OSThread.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define SCREEN_WIDTH_F ((f32)SCREEN_WIDTH)
#define SCREEN_HEIGHT_F ((f32)SCREEN_HEIGHT)

extern int ScreenWidth;
extern int ScreenHeight;

extern OSThread graphThread;
extern u8 SegmentBaseAddress[0x40];

/* SDL renames main to SDL_main. Keep this declaration as the GameCube entry
 * (compiled as ac_entry) so it does not collide with int SDL_main(int, char**). */
#pragma push_macro("main")
#undef main
void main(void);
#pragma pop_macro("main")
extern void mainproc(void* val);

#ifdef __cplusplus
}
#endif

#endif
