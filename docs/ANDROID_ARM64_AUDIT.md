# arm64 audit against zwaetschge/ACGC-Android-Port

Compared with [zwaetschge/ACGC-Android-Port](https://github.com/zwaetschge/ACGC-Android-Port) (shallow checkout of the default branch, November 2026). That tree ships an arm64-v8a and armeabi-v7a APK that has been run on 64-bit-only hardware (their notes say an AYN Odin 3).

Licence, so this note does not become a copy: the decomp is CC0 (ACreTeam/ac-decomp). The PC-port layer is MIT, Copyright 2026 FlyingMeta. FixNES is MIT, Copyright 2017 FIX94. Their Android launcher, touch view, HD-texture tools, and Python were not copied. This file only records which 64-bit ideas apply here.

Their approach and ours are different. They let the `.so` load anywhere and widen the places that hold a pointer (`Gfx.w1` is `uintptr_t`, display lists are 16 bytes). We keep those words 32 bits, map `libmain.so` and the game heap below 4 GB, and turn a pointer that still does not fit into a token (`pc_gbi_intern`). A low address does not fix a struct that is overlaid on a 4-byte-per-pointer disc record. Those records stay 4 bytes (`PcLowPtr`, or a `u32` offset) on both approaches.

## The v0.11 null dereference

`libmain.so+0xcb6fb8` in the v0.11 library is `Matrix_copy_MtxF`, called from `Matrix_push`. The caller is `mFont_SetMatrix`, from `pc_pause_menu_draw`, from `graph_main`, from `mainproc` / `ac_entry` / `boot_main`.

`Matrix_now` was still null. `first_game` and `second_game` never call `new_Matrix`. The trademark scene is the first one that does. Android back opens the same menu as Esc (`SDLK_AC_BACK`), and that menu pushes a matrix on whatever scene is current. The copy then reads address 0.

Their tree does not have this pause menu, so they never hit this path. Their `Mtx_t` change (below) is a different bug. `Matrix_push` here now keeps a small identity stack until `new_Matrix` allocates the scene stack, and `Matrix_pull` does not walk off the bottom.

`graph_main` builds the font display list (`graph_setup_double_buffer`) before the pause menu draws. The instruction after the null copy is a store into that list, not another null pointer. The next real failure on a full disc is whatever the trademark and play scenes do with archive resources, not this stack.

## What they fixed, and what we already do

| Their change | Where | Here |
| --- | --- | --- |
| `Mtx_t` is `int`, not `long`, when pointers are 8 bytes. `long` makes each matrix element 8 bytes, so `(*mtx)[1][3]` reads the wrong word and projection type is nonsense. | `include/PR/gbi.h` `_GBI_LONG`; they also special-case the perspective test in `emu64.c` because of that. | `include/PR/gbi.h` already uses `int` for `Mtx_t` under `TARGET_PC && __LP64__`. The `(*mtx)[1][3]` test is valid again, so their `fabsf` workaround is not needed. |
| `Gfx` word 1 is `uintptr_t`. A display-list pointer is stored whole. `Gfx` becomes 16 bytes. | `include/PR/gbi.h` `Gwords` | Words stay 32 bits. Pointers inside `libmain` fit because the library is mapped below 4 GB. Anything still above 4 GB (a GL texture, the ART stack) goes through `pc_gbi_intern`. Widening `Gfx` would change every display list we share with the 32-bit server build. Not ported. |
| Jaudio ROM records (`smzwavetable`, perc, voice) are parsed with `u32` fields, then copied into a native struct whose pointers are 8 bytes. | `src/static/jaudio_NES/internal/system.c` `smzwavetable_gc`, `__WaveTouch_PC` | `PcLowPtr` in `include/jaudio_NES/ja_fileptr.h` keeps the on-disc field 4 bytes and resolves it to a host pointer. Same constraint, one type instead of a parallel struct. Low addresses are what make the stored 32-bit value a real pointer. |
| `initial_menu_cleanup` receives into `OSMessage` on 64-bit, not `int`. | `src/static/initial_menu.c` | Done in v0.11, for `proc` as well. Their `OSMessageQueue_PC` still uses a 16-byte thread-queue prefix. On LP64 each `OSThreadQueue` is 16 bytes, so that prefix covers only one queue and `usedCount` lands on `msgArray`. Ours uses two pointer pairs so it matches `include/dolphin/os/OSMessage.h`. |
| System heap request is `arena_hi - arena_lo - 0x200` instead of `0xD0`, because heap headers grew. | `src/static/jsyswrap.cpp` `JW_Init` | `JFWSystem::firstInit` already shrinks the request to the root heap's free space. `sizeof(JKRExpHeap)` is 0x100 with 8-byte pointers, and `0xD0` does not fit. The clamp is the stronger form of their constant. |
| `DVDFileInfo` field offsets are computed from the real struct, because `DVDCommandBlock` contains pointers. | `pc/src/pc_dvd.c` | Same. The 32-bit build still `static_assert`s the GameCube size 0x3C. |
| RARC `SDIFileEntry` stays 0x14. The last field is a pointer in memory and zero on disc. | their archive headers | `PcLowPtr<void> mData` plus `static_assert(sizeof == 0x14)`. Also asserted: `SDIDirEntry` 0x10, `SArcDataInfo` 0x20, `SArcHeader` 0x20, `ResTIMG` 0x20, font `BlockHeader` 8, `INF1` 0x14, `ResFONT` blocks at 0x20. |
| Actor profiles stay source structs. `class_size` is `size_t` and the five procs are function pointers, so the struct is larger than the comment's 0x24. | `include/m_actor.h` `actor_profile_s` | Identical. Profiles are compiled into the binary, not memcpy'd from a 0x24 disc record, so the growth is consistent on both sides. A low address is not what keeps this correct. `SDirEntry` is the same kind of thing: runtime only, `mName` points at the archive string table. |

## GLES and touch

They compile the desktop GL calls against a `glad/gl.h` shim that includes `GLES3/gl3.h`, maps `glClearDepth` to `glClearDepthf` and `glDepthRange` to `glDepthRangef`, and drops `glPointSize`. We do that in the `PC_GLES` sources instead of a shim. Same three entry points. Their launcher, language picker, and ASTC texture pipeline were not taken.

Their on-screen control is a GameCube button pad (`TouchControlsView`). Ours is the tap-to-move overlay already in `pc/src/pc_touch.c` (A, B, Tool, Emote), which is what the roadmap asked for. They trap the Android back button (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`). We let it through and treat `SDLK_AC_BACK` as Esc, which is what opened the pause menu on top of `second_game` and produced the null `Matrix_push`.

## What this does not claim

The emulator run for v0.12 mounted a stub `forest_1st.arc` and parsed a stub BFN1 (font width 8), then pushed the idle matrix and stopped. It did not boot a real GAFE01 into the town. The Fold 6 has not been re-run on this commit.
