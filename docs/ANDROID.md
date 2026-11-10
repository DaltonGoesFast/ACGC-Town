# Android

The phone build is `pc/android/`. It is a Gradle project whose activity is SDL2's `SDLActivity` (zlib, SDL 2.30.10, vendored under `pc/android/app/src/main/java/org/libsdl/app/`). The native libraries are CMake and OpenGL ES 3. This APK is **arm64-v8a** (versionCode 8), which is the only ABI a Galaxy Z Fold 6 can load. minSdk is 24, targetSdk is 34, and the debug APK is signed with the standard Android debug key (`androiddebugkey` / `android`).

[zwaetschge/ACGC-Android-Port](https://github.com/zwaetschge/ACGC-Android-Port) was read for the shape of the problem: an SDL2 Android activity, a GLES 3 context instead of desktop GL, a Storage Access Framework picker so the disc is not in the APK, and NDK CMake. Its licence is the same MIT text as the PC port. None of its Java, Python, texture tools, or game patches were copied. The Java activity here is SDL's own, not that port's launcher.

No ROM and no game assets are in this tree or in the APK. The first screen asks for a USA GAFE01 image and copies it into the app's private files. Uninstalling the app deletes that copy.

## ABI

The dedicated server is a 32-bit x86 build with `-malign-double`. That flag makes `double` 8-byte aligned, which is what `Save_t` was checked against. On that ABI `long` and pointers are 4 bytes.

| ABI | Pointers and `long` | `double` alignment | Fits the server |
| --- | --- | --- | --- |
| Linux i386 + `-malign-double` (the server) | 4 | 8 | yes |
| `armeabi-v7a` | 4 | 8 (the ARM EABI already aligns `double` to 8) | yes, in memory |
| `arm64-v8a` | 8 | 8 | the town image is 148128 bytes; `Save_t` matches that size |

`Save_t` on arm64 is the same 148128 bytes as the server. Pointers elsewhere in the process are 8 bytes, so display-list words store tokens instead of truncated addresses, and the game heap is reserved below 4 GB. The chat and room messages stay fixed-width (`PCChatSendMsg` 68, `PCChatMsg` 80, `PCRoomMsg` 300). This APK does not add a packet. `touch_controls` defaults to `on` when the binary is built with `__ANDROID__`.

## Foldables

The Galaxy Z Fold 6 cover screen is a tall narrow strip. The inner screen is close to square, and the camera hole eats a corner. The manifest sets `resizeableActivity` and lists `screenSize`, `smallestScreenSize`, `screenLayout`, and `orientation` in `configChanges`, so a fold does not recreate the activity. The window is allowed to draw into the cutout (`shortEdges`). `SafeInsets` writes the system-bar and cutout insets to `safe_insets.txt`. The touch HUD adds those insets and shrinks the buttons on a very wide or very tall aspect.

## What the APK runs

`libmain.so` is the game, built for arm64-v8a with `PC_GLES` (OpenGL ES 3). `SDL_main` is the game's `main`. The activity `chdir`s to internal storage and passes `--rom` with the image the picker saved, so Play Online is the same screen as the desktop client. versionCode 8. Audio starts off (`settings.ini` `[Audio] audio = 0`) so a bad audio thread does not close the game; set `audio = 1` to turn it on.

The phone's `Save_t` is the same 148128 bytes as the 32-bit server (every top-level member is checked). `pc_save_wire_store` / `pc_save_wire_load` copy it field by field for the memory card, the town fetch GCI, and the GCI validation path. The event packet's special and weekly blocks are asserted at 84 and 100 bytes. A host built from an older commit does not need a protocol change for this.

arm64 display lists cannot store a pointer in 32 bits, because the `.so` is loaded above 4 GB. Those pointers are tokens (`pc_gbi_intern`), and `seg2k0` resolves them. The game heap and the thread stack are reserved below 4 GB so the decomp's other `u32` pointer fields keep working. Matrices use 32-bit `int` instead of LP64 `long`.

`PC_GLES` is the renderer this APK links: `#version 300 es`, highp precision, `glClearDepthf` / `glDepthRangef`. Pause and resume use `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` and rebind the context on `SDL_APP_DIDENTERFOREGROUND`. `SDLK_AC_BACK` opens the same menu as Esc.

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

## Crash log

Tapping Start commits a `launch-in-progress` flag (and the process id) before `System.loadLibrary`. The flag is cleared on pause or when the activity finishes. If the process dies first, the next launch runs `logcat -d -v threadtime --pid=<previous pid>`, or a filtered `logcat -d` (`ACGC`, `DEBUG`, `libc`, `AndroidRuntime`, `linker`) if that is empty. The text is saved as `acgc-crash.log` and the Share log screen is shown. A Java uncaught-exception handler writes the same file. **Copy debug info** on the launcher shares the last session log and a fresh logcat slice at any time.

Native milestones also go to logcat with `__android_log_print` under the tag `ACGC`, including the library constructors (`diag ctor`, `lowheap init`, `heap reserve ok`). A fatal signal (`SIGSEGV`, `SIGBUS`, `SIGABRT`, `SIGILL`) still writes:

`/storage/emulated/0/Android/data/com.acgc.town/files/acgc-crash.log`

The same directory has `acgc-session.log`. If external storage is unavailable, both files are under the app's internal files directory. Java writes `files/acgc-logdir.txt` before the libraries load so the signal handler has that path even if it runs during `dlopen`.

If the low mapping is refused, `malloc` falls back to the system allocator so C++ startup does not crash on a null pointer, and `SDL_main` shows the refusal instead. The game stays on the stack ART created for SDL's thread. Moving that stack into the low heap makes the next JNI call abort with `StackOverflowError`. The low-address survey logs a high stack address and does not abort on Android. The EGL config does not request 4x MSAA or a 24-bit depth buffer, which Android configs often do not have. `acgc-session.log` records `entered SDL_main`, `heap reserve ok`, `sdl thread stack kept`, `game main`, `SDL_Init ok`, `GL context ok`, and `rom opened`.

## What is still open

The APK has not been installed on a Galaxy Z Fold 6. There is no disc image on the build machine, so a picked GAFE01, a fold, the cutout insets, and a joined town were not watched. A pointer the decomp still stores as a raw `u32` outside the below-4GB heap or the display-list token table will be wrong; startup logs that if the low-address check sees it.
