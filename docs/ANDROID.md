# Android

The phone build is `pc/android/`. It is a Gradle project whose activity is SDL2's `SDLActivity` (zlib, SDL 2.30.10, vendored under `pc/android/app/src/main/java/org/libsdl/app/`). The native libraries are CMake and OpenGL ES 3, built for **arm64-v8a and armeabi-v7a**. minSdk is 24, targetSdk is 34, and the debug APK is signed with the standard Android debug key (`androiddebugkey` / `android`).

[zwaetschge/ACGC-Android-Port](https://github.com/zwaetschge/ACGC-Android-Port) was read for the shape of the problem: an SDL2 Android activity, a GLES 3 context instead of desktop GL, a Storage Access Framework picker so the disc is not in the APK, and NDK CMake. Its licence is the same MIT text as the PC port. None of its Java, Python, texture tools, or game patches were copied. The Java activity here is SDL's own, not that port's launcher.

No ROM and no game assets are in this tree or in the APK. The first screen asks for a USA GAFE01 image and copies it into the app's private files. Uninstalling the app deletes that copy.

## ABI

The dedicated server is a 32-bit x86 build with `-malign-double`. That flag makes `double` 8-byte aligned, which is what `Save_t` was checked against. On that ABI `long` and pointers are 4 bytes.

| ABI | Pointers and `long` | `double` alignment | Fits the server |
| --- | --- | --- | --- |
| Linux i386 + `-malign-double` (the server) | 4 | 8 | yes |
| `armeabi-v7a` | 4 | 8 (the ARM EABI already aligns `double` to 8) | yes, in memory |
| `arm64-v8a` | 8 | 8 | no |

`arm64-v8a` is the same class of problem as the unfinished 64-bit Linux build. The decomp still casts pointers to `u32`. `PC_ALLOW_64BIT` does not link. A 64-bit phone build would disagree with the server on every struct that contains a `long` or a pointer-sized field, even when the wire format uses explicit `uint32_t`.

`armeabi-v7a` matches the server in memory. `arm64-v8a` does not: pointers and `long` are 8 bytes, so a raw `Save_t` on the phone would not line up with the GCI the server writes. The network messages that already use `uint8_t` / `uint16_t` / `uint32_t` (`PCChatSendMsg` 68, `PCChatMsg` 80, `PCRoomMsg` 300) stay the same size on both ABIs. The town file must be copied field by field through those fixed-width types before an arm64 game build loads it. This APK does not load a town.

The APK still contains **arm64-v8a and armeabi-v7a**. A Galaxy Z Fold 6 is 64-bit only, so an armeabi-v7a-only package is rejected with "App not installed". The arm64 library is the shell that opens. It does not cast the disc's save onto `Save_t`.

Joining a town is the desktop Play Online screen, once the game itself is linked into this activity: `--connect` plus a town fetch, same messages. This APK does not add a packet. `touch_controls` defaults to `on` when the binary is built with `__ANDROID__`.

## Foldables

The Galaxy Z Fold 6 cover screen is a tall narrow strip. The inner screen is close to square, and the camera hole eats a corner. The manifest sets `resizeableActivity` and lists `screenSize`, `smallestScreenSize`, `screenLayout`, and `orientation` in `configChanges`, so a fold does not recreate the activity. The window is allowed to draw into the cutout (`shortEdges`). `SafeInsets` writes the system-bar and cutout insets to `safe_insets.txt`. The touch HUD adds those insets and shrinks the buttons on a very wide or very tall aspect.

## What the APK runs

`libmain.so` in this APK is the GLES 3 shell, built for both ABIs from `pc/android/app/src/main/cpp`. It opens, keeps the GL context across pause and resume, and does not load a town. The first screen is the disc picker. Start opens the SDL activity (green-tinted clear if a disc file was saved, dark red if not).

The desktop game sources do have a `PC_GLES` path (`#version 300 es`, highp precision, `glClearDepthf` / `glDepthRangef`, no `GL_MULTISAMPLE` enable). That path is not what this APK links. `pc/CMakeLists.txt` still refuses a 64-bit configure unless `PC_ALLOW_64BIT` is on, and that configure does not link. Putting it in the APK would drop arm64 again or ship a binary that reads `Save_t` with the wrong offsets.

Pause and resume in the shell use `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` and rebind the context on `SDL_APP_DIDENTERFOREGROUND`. On a build that links the game, `SDLK_AC_BACK` opens the same menu as Esc. The shell logs the back key and does not quit.

`--connect` with a guest profile that already has a name leaves the desktop title on its own. A profile that does not exist yet stays on the title so the player can type a name.

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

Linking the decomp into `libmain.so` on arm64, with the GCI loaded through the fixed-width fields instead of a cast to `Save_t`. Until that exists, Play Online on the phone cannot join the server. The GLES game path has not been watched on a device.
