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
  - `src/game/m_player.c` — `mPlib_PC_Touch*` TARGET_PC wrappers (equip, talk, pickup, shake, axe, dig, rock, net, rod, mailbox, door, furniture, busy)
  - `include/m_player_lib.h` — declarations
  - `pc/src/pc_touch.c` — screen-space hit test, shore stands, preview, approach → equip → one hook
  - `pc/include/pc_touch.h` — comment
  - `docs/ANDROID.md` — touch behavior notes
  - `docs/TOUCH_PHASE_STATUS.md` — this handoff
- Works now (intended; verify on device):
  - Finger-down preview shows verb (`Talk`, `Read`, `Pick`, `Shake`, `Chop`, `Catch`, `Cast`, `Dig`, `Hit`, `Mail`, `Open`, `Grab`, `Pull`)
  - Short release commits; drag cancels preview and steers
  - Screen-space closest hit among insect / fish / NPC / mailbox / house / FG (sign, tree, fruit, flower, weed, rock, hole, furniture, warp)
  - Approach at walk for bugs and fishing shore; run allowed for trees/signs/villagers until last stretch
  - Live retarget for bugs/fish; lead cast along fish motion; cancel if actor despawned
  - Auto-equip from pockets (axe / net / rod / shovel) then one vanilla request — no A retry
  - Tree without axe → shake; missing required tool → toast + cancel sting, no walk onto target
  - Rocks / money rocks → shovel hit; buried pitfalls refused; balloons/slingshot still ground walk
  - Mailbox uses `aMBX_REQUEST_OPEN` + mail jump; door/furniture toast on false
- Known bugs / follow-ups:
  - Phone playtest still required (disc not on build machine)
  - Indoor tool refusal relies on vanilla cancel sting (no new rule)
  - Balloons / slingshot still out of scope
- Do not regress:
  - Pocket Camp move: tap-to-move + press-drag distance speed + run via far/double tap
  - No rendering / letterbox changes
  - Offline-first; virtual pad remains optional
  - HUD Back / Bag / Map / Action still present for testing
  - No packet / network protocol changes
