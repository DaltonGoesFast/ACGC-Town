# Touch system — phase status (handoff)

Updated by each phase agent at the end of their work. The next agent must read this before coding.

## Current phase

**Phase 3 complete (code)** — next up: **playtest on phone** (no disc in this environment).  
Phase 2 UI (sheet polish, emotes, confirms) remains out of scope until after semantic-touch playtest.

## Phase 1 — World intents

- Status: **superseded by Phase 3** (approach + A replaced by semantic hooks)
- Residual value: Pocket Camp move, stall detours, action label, HUD chips

## Phase 2 — UI intents

- Status: deferred (sheet, emotes, confirm modals)
- Do not regress: Phase 3 world intents + Pocket Camp move

## Phase 3 — Semantic touch (direct hooks)

- Status: **implemented in tree** (needs device playtest)
- Files touched:
  - `src/game/m_player.c` — `mPlib_PC_Touch*` `TARGET_PC` wrappers (equip, talk via `mDemo`, sign reading, pickup with Bell wallet handling, grass, live-checked tree/axe/rock, `mFI_GetDigStatus` dig/client seams, `READY_ROD` cast, mailbox, door `mDemo`/forward check, outdoor 3D/leaf furniture, busy)
  - `src/game/m_player_main_ready_rod.c_inc` — `s_pc_touch_rod_target_*` cast target override in `Player_actor_request_proc_index_fromReady_rod`
  - `include/m_player_lib.h` — declarations (including `mPlib_PC_TouchRequestReadSign`)
  - `pc/src/pc_touch.c` — bounded screen-space hit test, player-side cliff/obstacle-free shore stands, per-building doorstep offsets, preview, approach → equip → one hook (`Reel` tap while `RELAX_ROD`), `intent_steer` before latch consume
  - `pc/include/pc_touch.h` — comment
  - `pc/android/app/build.gradle` — bumped Android `versionCode 48` (`0.27.9`) for semantic-touch test release (`v0.27.9-semantic-touch`)
  - `docs/ANDROID.md` — touch behavior notes
  - `docs/TOUCH_PHASE_STATUS.md` — this handoff
- Works now (compiled cleanly on PC & Android debug APK `versionCode 48`; verify on device):
  - Finger-down preview shows verb (`Talk`, `Read`, `Pick`, `Shake`, `Chop`, `Catch`, `Cast`, `Reel`, `Dig`, `Hit`, `Mail`, `Open`, `Grab`, `Pull`)
  - Short release commits; drag cancels preview and steers; approach stops at `TOUCH_STAND_ARRIVE` on computed stand points
  - Bounded screen-space hit test prevents nearby items/actors from hijacking empty ground taps
  - Covers insect, fish (plus water tap while holding a rod), NPC, mailbox, signs/boards (`aSIGN`, `mAc_PROFILE_AC_SIGN`, `RESERVE`, `NAMEPLATE`, `MESSAGE/MAP/MUSIC_BOARD*`), all outdoor building doors, `SHRINE`, `DUMP`, and FG tiles (fruit-first non-contiguous tree check, tiny saplings excluded, buried deposits, `SHINE_SPOT`, `HOLE_SHINE`, stumps, dead saplings, holes, buried pitfalls, flowers, items, weeds, rocks, outdoor 3D & leaf furniture)
  - Auto-equip swaps `priv->equipment` + `NA_SE_5E` cleanly without priority-37 `TAKEOUT_ITEM` stall; `TouchHoldingTool` guards on `!TouchBusy`
  - Villager talk and building doors trigger via `Player_actor_check_forward` + `g_pc_demo_force_talk_actor` + `PAD_BUTTON_A`; rod casting goes through `READY_ROD` so `UKI_ACTOR` spawns cleanly; tapping while `RELAX_ROD` active reels in
  - Pickups snap to tile center, allow full-pocket Bell bags that fit in wallet without spurious failure toasts, and route placed 3D outdoor furniture through `pc_ofurn_front_press(game, 0, 1)`
- Known bugs / follow-ups:
  - Phone playtest still required (compiled cleanly via `pc/build64` `ac_pc` and Android debug APK `versionCode 48`; on-device playtest pending)
  - Balloons / slingshot still out of scope
- Do not regress:
  - Pocket Camp move: tap-to-move + press-drag distance speed + run via far/double tap
  - No rendering / letterbox changes
  - Offline-first; virtual pad remains optional
  - HUD Back / Bag / Map / Action still present for testing
  - No packet / network protocol changes
