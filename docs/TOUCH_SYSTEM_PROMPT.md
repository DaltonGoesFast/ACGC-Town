# Touch system — phased agent plan

Use this when building the Android/mobile **touch-native** control system.  
**Assumption:** a **new agent** runs each phase. Give that agent (1) the **Shared brief** and (2) that phase’s **Kickoff** block only.

Do not ask one agent to “do the whole file.”

---

## How you (the owner) run this

1. Open a **new** agent chat.
2. Attach this file: `docs/TOUCH_SYSTEM_PROMPT.md`
3. Paste the kickoff for the phase you’re on (below).
4. When that agent finishes, **playtest** on phone if you can.
5. Have the agent (or you) update `docs/TOUCH_PHASE_STATUS.md` with what landed / what’s still broken.
6. Open another **new** chat for the next phase. Attach this file + `docs/TOUCH_PHASE_STATUS.md` and paste the next kickoff.

| Order | Phase | What it is | Start next when… |
|------:|-------|------------|-------------------|
| 1 | **Phase 1 — World intents** | Tap thing → approach → face → auto-act | Signs/fruit/trees/NPCs mostly work by tap |
| 2 | **Phase 2 — UI intents** | Dialogue, bag/map sheet, emotes, confirms, HUD | Offline menus/dialogue/emotes usable by touch |
| 3 | **Phase 3 — Direct hooks** | Only if Phase 1 still flakes | Optional — skip if Phase 1 is good enough |

Skip Phase 3 unless playtesting shows face+A still fails often.

---

## Shared brief (every agent must read this)

### Role

Implement a **touch-native** control layer for ACGC-Town (AC GameCube decomp + PC/Android port).  
Product target: **all phones**, any common size/orientation. A Fold is only a test device.  
Virtual GameCube pad is optional fallback, not the design center.

Do **not** rewrite the game. Keep vanilla actions/physics. Touch drives **intent**, then satisfies existing systems.  
Do **not** change game picture rendering / letterboxing — **input/HUD/intent only**.

### Problem

Touch today mostly **emulates a pad** (`pc_touch.c` → stick/A). The game still uses **facing + distance + A/B**.  
So “tap the sign” fails unless facing is right — pad emulation without intent.

### System shape

```
TOUCH SYSTEM
├── Move / camera     — Pocket Camp tap + drag-speed; pinch zoom
├── World intents     — tap world thing → approach, face, auto-act
└── UI intents        — dialogue, choices, bag/map sheet, emotes
```

### Locked design decisions

| Topic | Decision |
|---|---|
| Move | Pocket Camp: **tap = go there**; **press-drag = steer**; farther drag = faster. Keep current move feel. |
| Run | Distance-based (far tap / outer drag). No separate Run button for v1. |
| Camera | **Pinch to zoom**; don’t fight one-finger move. No big new camera UX. |
| World interact | **Auto-interact** (approach → face → act). |
| Tools | Equipped + applicable → **auto-use** on matching tap. |
| Confirms | Only for **risky** actions (sell, drop valuable, send mail, quit job, etc.). |
| Pathing | **Light auto-detour**; not perfect pathfinding; toast if stuck. |
| Context button | Tap-on-thing is primary; keep Action visible for testing/edge cases. |
| Dialogue | Tap anywhere = Next; tap choice bubble = choose. |
| Inventory | **Reuse/extend `MobileSheet`** + bag/map discs; new UI only if needed. Phone-style OK; art later. |
| HUD | Always show **Back / Bag / Map / Action** while testing. |
| Android Back | Short = cancel (B); long = pause. |
| Virtual pad | Optional (`touch_virtual_pad`); not required for core play. |
| Screens | Portrait + landscape; any phone. Sheet is portrait-gated today — landscape still needs bag/map. |
| Rendering | Leave framebuffer/letterbox alone. |
| Handedness | Right-hand default; keep left-hand mirror. |
| Scope | **Offline first.** |
| Social | **Simple emote list/grid** in v1; chat later. |
| Feedback | Haptic + short toast on interact success/fail and important HUD presses. |
| Avoid | Teaching “swipe up on character = interact.” |

### Key files

- `pc/src/pc_touch.c`, `pc/include/pc_touch.h`
- `pc/src/pc_pad.c`
- `docs/ANDROID.md`
- `docs/TOUCH_PHASE_STATUS.md` (create/update each phase — handoff for the next agent)
- Player/talk: `m_demo.c` (`weight_of_talk_position`), `m_player_common.c_inc` (pickup/sign/shake facing)

### Global constraints

- Match existing PC/touch code style; no drive-by refactors.
- No disc/assets in repo.
- `__ANDROID__`: touch defaults on.
- Offline path first; don’t break multiplayer code paths casually.
- Update `docs/ANDROID.md` when behavior changes; keep `docs/TOUCH_PHASE_STATUS.md` accurate for the next agent.
- Out of scope unless asked: chat, changing rendering, making virtual pad primary, desktop WASD polish.

### Handoff rule (end of every phase)

Before stopping, write/update `docs/TOUCH_PHASE_STATUS.md` with:

- Phase completed
- Files touched
- What works now
- Known bugs / follow-ups
- Anything the next phase must not regress

---

# Phase 1 kickoff — World intents

Copy from here through the end of this phase’s “Stop when” section into a **new** agent chat (plus Shared brief / this file).

```
You are doing PHASE 1 ONLY of the touch system plan in docs/TOUCH_SYSTEM_PROMPT.md.

Read:
- Shared brief in that file
- docs/TOUCH_PHASE_STATUS.md if it exists
- docs/ANDROID.md
- pc/src/pc_touch.c interact path: begin_move, ground_kind, steer_toward, s_interact, s_pulse_a, refresh_action_label, drag-follow move

GOAL (Phase 1): Replace “walk near + hope A” with a world TouchIntent:
  identify tap target → approach → face → auto-act → done/fail

Implement:
1. World intent state machine (kinds: talk, read_sign, pickup, shake_tree, use_tool when equipped+applicable, walk/ground).
2. Better hit-test than npc/item/ground — at least npc, sign, pickup, tree, tool-target, ground.
3. Face-before-act so vanilla facing gates can pass (talk/shake/pickup cones).
4. Light auto-detour (improve existing stall slides); toast+haptic if stuck/cancel.
5. Align action label radii with real interact distances.
6. Auto-act via latched A after face (smarter pad). No full decomp rewrite.
7. Preserve Pocket Camp move: tap-to-move + press-drag with distance-based speed. Do not regress it.
8. Do NOT change game rendering/letterbox.
9. Defer phone sheet / emotes / confirm modals to Phase 2 unless trivial.
10. End by creating/updating docs/TOUCH_PHASE_STATUS.md for Phase 1.

Acceptance (Phase 1):
- Tap villager → talk without manual facing + virtual A
- Tap sign → read
- Tap fruit → pick up
- Tap tree → shake (or axe if equipped+applicable)
- Applicable equipped tool auto-uses on matching taps
- Blocked path: light detour or clear fail feedback
- Move feel unchanged in spirit (tap go / drag speed)

Stop when Phase 1 acceptance is met (or blocked with a clear STATUS note). Do not start Phase 2.
```

**Stop when:** world taps mostly auto-work; STATUS.md updated; move feel preserved.

---

# Phase 2 kickoff — UI intents

Copy into a **new** agent chat after Phase 1 is playtested.

```
You are doing PHASE 2 ONLY of the touch system plan in docs/TOUCH_SYSTEM_PROMPT.md.

Read:
- Shared brief in that file
- docs/TOUCH_PHASE_STATUS.md (Phase 1 results — do not rebuild world intents unless broken)
- docs/ANDROID.md
- pc/src/pc_touch.c: MobileSheet, HIT_BAG/HIT_MAP/HIT_EMOTE/HIT_ACTION/HIT_BACK, dialogue/choice handling, HUD layout (portrait/landscape, safe insets)

GOAL (Phase 2): Make offline UI fully touch-driven using existing mobile UI where possible.

Implement:
1. Dialogue: tap anywhere = Next; choice bubbles select+confirm. Harden any gaps.
2. Bag / Map: reuse/extend MobileSheet + existing bag/map discs. Portrait sheet already exists — make landscape also usable (extend sheet or solid fallback). New UI only if reuse fails.
3. Always show Back / Bag / Map / Action during this testing phase; right-hand default.
4. Simple emote list/grid from touch (HIT_EMOTE exists — keep v1 simple, not a fancy radial). Chat out of scope.
5. Confirm prompts for risky actions only (sell/drop valuable/send mail/quit job, etc.).
6. Android Back already short=B / long=pause — verify; don’t break it.
7. Do not regress Phase 1 world intents or Pocket Camp move.
8. Do not change game rendering/letterbox.
9. End by updating docs/TOUCH_PHASE_STATUS.md for Phase 2.

Acceptance (Phase 2):
- Dialogue + choices work touch-only
- Bag and map reachable and usable in portrait and landscape
- Simple emotes work from touch
- Risky actions can confirm; normal world taps still don’t need confirm
- Back / Bag / Map / Action visible for testing
- Phase 1 world interact still works

Stop when Phase 2 acceptance is met. Do not start Phase 3 unless STATUS shows Phase 1 still unreliable after this.
```

**Stop when:** offline UI/emotes usable by touch; STATUS.md updated.

---

# Phase 3 kickoff — Direct action hooks (optional)

Only if Phase 1 still fails often after playtest (wrong facing, A too early, flaky tools). Otherwise skip.

```
You are doing PHASE 3 ONLY (optional) of the touch system plan in docs/TOUCH_SYSTEM_PROMPT.md.

Read:
- Shared brief
- docs/TOUCH_PHASE_STATUS.md (must list concrete Phase 1 failures — if none, STOP and say Phase 3 not needed)
- Relevant player request paths in m_player_common.c_inc / talk demo paths
- Current TouchIntent act phase in pc_touch.c

GOAL (Phase 3): Where smarter pad (face+A) is still unreliable, add thin TARGET_PC hooks so the touch intent layer can request the same vanilla actions with an explicit target (pickup / shake / talk / tool), instead of only pulsing A.

Rules:
1. Only fix failures documented in TOUCH_PHASE_STATUS.md.
2. Reuse existing player request functions; no new gameplay systems.
3. Touch intent layer is the only caller of these hooks when an intent is active.
4. Idle / non-touch play stays vanilla.
5. No network protocol changes; offline-safe; don’t break MP paths.
6. Do not rebuild UI (Phase 2) or change rendering.
7. Update docs/TOUCH_PHASE_STATUS.md with what hooks landed and residual risk.

Acceptance (Phase 3):
- Each documented flaky interact from STATUS now succeeds via tap in typical cases
- No regressions to move, Phase 1 happy paths, or Phase 2 UI

Stop when those failures are addressed or proven to need owner decisions.
```

**Stop when:** listed flakes fixed or explicitly deferred; STATUS.md updated.

---

## Quick owner checklist

- [ ] New chat → Phase 1 kickoff → playtest world taps  
- [ ] Confirm `docs/TOUCH_PHASE_STATUS.md` exists and looks right  
- [ ] New chat → Phase 2 kickoff → playtest bag/map/dialogue/emotes  
- [ ] Update STATUS again  
- [ ] Only if needed: New chat → Phase 3 kickoff  

Paste for any phase:

> Follow `docs/TOUCH_SYSTEM_PROMPT.md`. Do **[Phase N]** only. Read the Shared brief and `docs/TOUCH_PHASE_STATUS.md`. Paste/implement that phase’s kickoff. Stop when its acceptance criteria are met and STATUS is updated.
