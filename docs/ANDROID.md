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

## What the APK runs today

`libmain.so` is an SDL program that opens a GLES 3 context and clears the screen. It is the renderer path the full port will use (SDL's GL attributes request ES 3.0). It does not link `emu64` or the decomp. Desktop GL calls in `pc/src/pc_gx.c` are not GLES calls, and the game's CMake target is still the desktop one.

`LauncherActivity` copies the chosen disc to `files/gafe01.iso` and will not put that file in the APK. `TownActivity` then starts SDL. If the file is missing, the clear stays dark red and logcat says so.

## What is left before a phone can play

1. Compile `pc/CMakeLists.txt` with the NDK as `armeabi-v7a`, `TARGET_PC`, and `PC_GLES` (not added yet) so `pc_gx.c` includes `<GLES3/gl3.h>` instead of desktop GL.
2. Replace the GL 3.3 loader (`pc/lib/glad`) on that target. GLES 3 has the entry points in the system library.
3. Point `SDL_main` at the game's `main` and pass the private-files path as the ROM directory. Play Online is already in that `main`.
4. Confirm `sizeof` of `Save_t` and one network message against the Linux server binary (the asserts in `abi.c` cover the ABI, not every field).
5. Ship `arm64-v8a` only after the 64-bit port links, and only with an explicit pack or a serializer if the layouts still differ. Do not turn it on to satisfy a 64-bit-only device until that exists.
