# Android

The phone build is `pc/android/`. It is a Gradle project whose activity is SDL2's `SDLActivity` (zlib, SDL 2.30.10, vendored under `pc/android/app/src/main/java/org/libsdl/app/`). The native library is CMake, OpenGL ES 3, and **armeabi-v7a only**.

[zwaetschge/ACGC-Android-Port](https://github.com/zwaetschge/ACGC-Android-Port) was read for the shape of the problem: an SDL2 Android activity, a GLES 3 context instead of desktop GL, a Storage Access Framework picker so the disc is not in the APK, and NDK CMake. Its licence is the same MIT text as the PC port. None of its Java, Python, texture tools, or game patches were copied. The Java activity here is SDL's own, not that port's launcher.

No ROM and no game assets are in this tree or in the APK. The first screen asks for a USA GAFE01 image and copies it into the app's private files. Uninstalling the app deletes that copy.

## ABI: stay on 32-bit ARM

The dedicated server is a 32-bit x86 build with `-malign-double`. That flag makes `double` 8-byte aligned, which is what `Save_t` was checked against. On that ABI `long` and pointers are 4 bytes.

| ABI | Pointers and `long` | `double` alignment | Fits the server |
| --- | --- | --- | --- |
| Linux i386 + `-malign-double` (the server) | 4 | 8 | yes |
| `armeabi-v7a` | 4 | 8 (the ARM EABI already aligns `double` to 8) | yes, in memory |
| `arm64-v8a` | 8 | 8 | no |

`arm64-v8a` is the same class of problem as the unfinished 64-bit Linux build. The decomp still casts pointers to `u32`. `PC_ALLOW_64BIT` does not link. A 64-bit phone build would disagree with the server on every struct that contains a `long` or a pointer-sized field, even when the wire format uses explicit `uint32_t`.

So the APK builds **armeabi-v7a only**. There is no second copy of `Save_t` and no new serializer. The network structs already use fixed-width integers and `_Static_assert` on their offsets (`pc_net_game.c`, `pc_chat.h`, `pc_roommates.h`). Those offsets do not change between i386 and armeabi-v7a. The in-memory save matches because both sides are ILP32 with 8-byte `double`. `pc/android/app/src/main/cpp/abi.c` refuses to compile if `sizeof(void*)`, `sizeof(long)`, or `_Alignof(double)` is wrong.

Joining a town is the desktop Play Online screen, once the game itself is linked into this activity: `--connect` plus a town fetch, same messages. This APK does not add a packet. `touch_controls` defaults to `on` when the binary is built with `__ANDROID__`.

## What the APK runs

`libmain.so` is the game. `pc/CMakeLists.txt` builds it for `armeabi-v7a` with `PC_GLES`. `SDL_MAIN_HANDLED` is off on Android, so the game's `main` is `SDL_main` and SDL's activity calls it. The process `chdir`s to internal storage, where the chosen disc image sits (the loader looks in `.` and `rom/`) and where `TownActivity` has copied `shaders/default.vert` and `default.frag`.

`PC_GLES` includes `<GLES3/gl3.h>` instead of glad. The context is OpenGL ES 3.0. Shader sources that start with `#version 330 core` are rewritten to `#version 300 es` plus `precision highp float` and `precision highp int`. There is no fixed-function path in `pc_gx.c` (draws are `glDrawArrays` of triangles, textures use `GL_CLAMP_TO_EDGE`). The uniform calls go to the GLES entry points.

Pause and resume: `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` keeps the loop from spinning while the activity is stopped, and `SDL_APP_DIDENTERFOREGROUND` rebinds the existing GL context instead of creating a new one. The Android back key is `SDLK_AC_BACK` and opens the same menu as Esc.

Play Online is that same `main`. `--connect` with a guest profile that already has a name leaves the title on its own. A profile that does not exist yet stays on the title so the player can type a name.

## Layout check

`pc/tools/check_abi_layout.sh` compiles `pc/tools/abi_layout.c` twice and compares the `.abi_nums` section. Gradle runs it before the APK is built. A mismatch prints both rows and exits 1.

The numbers that matched on this machine (linux i386 `-malign-double` and armeabi-v7a):

| Field | Bytes |
| --- | --- |
| `sizeof(void*)`, `sizeof(long)` | 4, 4 |
| `_Alignof(double)` | 8 |
| `sizeof(Save_t)` | 148128 |
| `offsetof(Save_t, homes)` | 40168 |
| `sizeof(Private_c)`, loan offset | 9280, 144 |
| `sizeof(mHm_hs_c)` | 9904 |
| `sizeof(PCChatSendMsg)`, `PCChatMsg`, `PCRoomMsg` | 68, 80, 300 |

## What is still open

`arm64-v8a` stays out until the 64-bit port links. A device that refuses 32-bit apps cannot install this APK. The GLES path is the one the game already uses (triangles, shaders, VAOs). It has not been watched on a phone, so a missing GLES entry point would show up as a black screen or a shader log, not as a layout mismatch.
