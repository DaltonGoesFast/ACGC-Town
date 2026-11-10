# Android

The phone build is `pc/android/`. It is a Gradle project whose activity is SDL2's `SDLActivity` (zlib, SDL 2.30.10, vendored under `pc/android/app/src/main/java/org/libsdl/app/`). The native libraries are CMake and OpenGL ES 3. This APK is **arm64-v8a** (versionCode 17), which is the only ABI a Galaxy Z Fold 6 can load. minSdk is 24, targetSdk is 34, and the debug APK is signed with the standard Android debug key (`androiddebugkey` / `android`).

[zwaetschge/ACGC-Android-Port](https://github.com/zwaetschge/ACGC-Android-Port) was read for the shape of the problem: an SDL2 Android activity, a GLES 3 context instead of desktop GL, a Storage Access Framework picker so the disc is not in the APK, and NDK CMake. Its licence is CC0 for the decomp and MIT for the PC-port layer (Copyright 2026 FlyingMeta), plus MIT for FixNES. None of its Java, Python, texture tools, or game patches were copied. The comparison is `docs/ANDROID_ARM64_AUDIT.md`. The Java activity here is SDL's own, not that port's launcher.

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

The Galaxy Z Fold 6 cover screen is a tall narrow strip. The inner screen is close to square, and the camera hole eats a corner. The manifest sets `resizeableActivity` and lists `screenSize`, `smallestScreenSize`, `screenLayout`, and `orientation` in `configChanges`, so a fold does not recreate the activity. The window is allowed to draw into the cutout (`shortEdges`). `SafeInsets` writes the system-bar and cutout insets to `safe_insets.txt`. The touch HUD adds those insets. On the tall cover the 4:3 picture sits at the top and the controls sit in the band under it. On the near-square inner screen, and in landscape, the picture is centered with bars and the controls float at the edges. A fold sends `SDL_WINDOWEVENT_SIZE_CHANGED` and the layout is computed again from the new drawable size.

## Touch

The HUD is original drawing: soft discs in a sage and cream palette, not a copy of Pocket Camp's art. By default there is no stick and no A or B disc. Tap or hold the ground to walk. Dragging makes the character follow the finger. A second tap, or a long drag, runs. A tap on a person or an object walks there and then presses A. The log lines are `tap npc`, `tap item`, `tap ground`, and `drag follow`. Dialogue advances on a tap anywhere. A choice list is a stack of bubbles (One, Two, ...) that select that row and confirm it. Menu rows select and confirm on tap. A swipe moves the menu cursor. A small Back chip closes a menu. The leaf, inventory, and map sit in a corner. The band under the picture is a clock and, once a save is in memory, bells and how many pockets are filled. Options → Controls → Touch HUD → Virtual pad turns the old stick and A/B discs back on. Android back is still B, and a long press still opens pause. The picture keeps a 4:3 frame (16:9 while the widescreen hack is on) and is never stretched.

Play Offline on the title starts the same local town as Start Game, after Card A is pointed at `save/offline`. That directory is not `save/card_a` and not `save/mp/towns/<key>`. The choice does not open a socket, and a later Play Online request in that process is ignored. If a server town directory was already selected, Play Offline refuses and leaves it alone.

versionCode 16 latches a tap's A, B, X, Y, or Start for three polls so the game reads it, and the heartbeat ORs those bits until the next line. That latch is unchanged. A redundant Android orientation request is skipped so a surface recreate does not pause the game thread.

versionCode 15 adds a Touch HUD page under Options → Controls: scale, opacity, left-handed mirror, stick, tap-to-move, and haptics. Those values are written to `settings.ini` on Apply, and the stick nub saves immediately. A reached tap and a button press buzz when haptics are on. A tap that starts a walk leaves a short marker. The action disc is labeled Talk, Pick, Shake, Open, Next, or OK. Two smaller discs open the existing inventory (Y) and map (X). If a walk stops making progress it slides, then walks straight, then lets go.

## What the APK runs

`libmain.so` is the game, built for arm64-v8a with `PC_GLES` (OpenGL ES 3). `SDL_main` is the game's `main`. The activity `chdir`s to internal storage and passes `--rom` with the image the picker saved, so Play Online is the same screen as the desktop client. versionCode 17 adds Play Offline (`save/offline`) and the tap controls above. Audio defaults to off on Android until a device with a working audio output is verified; Options → Audio → Sound turns it on. While it is off, or the device did not open, one audio tick still runs on the game thread so the title is not stuck. Backgrounding the app fsyncs the 148128-byte town image. Play Online shows a small Connecting / Connected / Disconnected line.

versionCode 16. Audio starts off (`settings.ini` `[Audio] audio = 0`); set `audio = 1` to turn it on. `libmain.so` is loaded with `android_dlopen_ext` into a reserved mapping below 4 GB, so its code, `.data`, and `.bss` (audiomemory, DAC buffers, `g_gx`, texture caches, JKR/JUT heaps) are real 32-bit addresses. `malloc` in that library still comes from the separate low arena. The ART thread stack stays where ART put it. A debug build aborts on any other game pointer at or above 4 GB and logs `lowaddr AUDIT` with the call site. A file named `audio_init_test` in internal storage runs `boot_main` after the GL context is up and exits before the game loop, so a missing disc still reaches the logo cleanup. `boot: <stage>` lines in logcat mark each step. After the game thread starts, a side thread logs `hb frame=…` once a second (scene, graph step, retrace, last DVD read, drawable size, the letterboxed picture rect, and the pad: buttons, stick, finger count, HUD mode). If that thread sees no finished frame for 5 seconds it signals the game thread, which writes a `STALL` backtrace into `acgc-crash.log`.

The phone's `Save_t` is the same 148128 bytes as the 32-bit server (every top-level member is checked). `pc_save_wire_store` / `pc_save_wire_load` copy it field by field for the memory card, the town fetch GCI, and the GCI validation path. The event packet's special and weekly blocks are asserted at 84 and 100 bytes. A host built from an older commit does not need a protocol change for this.

Pointers that still live outside `libmain` (a texture the driver allocated, for example) stay tokens (`pc_gbi_intern`), and `seg2k0` resolves them. The game heap is a second mapping below 4 GB. Matrices use 32-bit `int` instead of LP64 `long`.

`PC_GLES` is the renderer this APK links: `#version 300 es`, highp precision, `glClearDepthf` / `glDepthRangef`. Pause and resume use `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` and rebind the context on `SDL_APP_DIDENTERFOREGROUND`. A short `SDLK_AC_BACK` is B. Holding it opens the pause menu. Esc still toggles pause immediately. Bluetooth and USB controllers are SDL game controllers, opened when they connect, and use the same pad mapping as a desktop gamepad.

`--connect` with a guest profile that already has a name leaves the desktop title on its own. A profile that does not exist yet stays on the title so the player can type a name.

## Layout check

`pc/tools/check_abi_layout.sh` compiles `pc/tools/abi_layout.c` twice and compares the `.abi_nums` section. Gradle runs it before the APK is built. A mismatch prints both rows and exits 1. The script needs Linux `gcc -m32` (the i386 server ABI, with `-malign-double`) and the Linux NDK clang `armv7a-linux-androideabi26-clang` (it looks at `ANDROID_NDK_HOME`, or `/tmp/android-sdk/ndk/26.1.10909125` if that is unset). It does not compile the arm64 game.

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
