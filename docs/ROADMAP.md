# ACGC-Town roadmap

ACGC-Town is a fork of [vsimochi/ACGC-PC-Multiplayer](https://github.com/vsimochi/ACGC-PC-Multiplayer) (`multiplayer` branch), itself a fork of [flyngmt/ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port), the native PC port of GameCube Animal Crossing on the matched [ac-decomp](https://github.com/ACreTeam/ac-decomp). This document is the plan for a persistent dedicated server that PC, Android and browser players can join. It is an unofficial fan project. It does not contain game assets. Each player supplies their own USA GAFE01 disc image.

## Architecture

The dedicated server is the authority for one town.

- It owns the town save (`servers/<id>/town/`, default id `default`), the in-game clock (`time_delta` against the GameCube RTC, broadcast to clients), and the host-side simulation that already exists in the multiplayer branch (field changes, shops, mail, guests, wildlife authority, day rollover at 6 AM).
- Clients are thin. They send input and present a view. They do not each keep a second copy of the town that can diverge: a client that joins can download a sanitized town image (`town_serve`) and then follows host deltas.
- Residents (up to 4, each with a house) and guests (a visitor with no house until they buy one) are the same roles the multiplayer branch already implements.
- The message protocol is the existing ACNP datagram layer in `pc/src/pc_net.c` (HELLO / reliable ordered payloads / unreliable movement). The bytes do not change with the socket type.
- `pc/src/pc_xport.c` is the transport under that protocol:
  - UDP, one datagram per packet, for native clients (default port 7777).
  - WebSocket (RFC 6455), one binary frame per datagram, for browsers that cannot send raw UDP (default TCP port 7778). Ping, pong and close are handled. Fragmented frames are rejected. Client frames are masked; server frames are not.

A Linux `--dedicated` process does not open a window, an OpenGL context, or an audio device. Display lists are not executed. A null consumer drains the audio ring at 32 kHz so the existing audio producer keeps the clock that fades and message waits use. The game loop, network poll, and save path still run. Windows `--dedicated` is unchanged: hidden window and SDL's dummy audio driver.

## Milestone 1 (this branch)

1. Dedicated server builds as the existing `AnimalCrossing` binary and, on Linux, runs headless.
2. Town files live in `servers/default/` and are what the next process loads. The on-disk layout was already implemented (`pc/src/pc_server.c`): `server.ini`, `town/card_a/DobutsunomoriP_MURA.gci`, resident and guest sidecars, `logs/server.log`.
3. UDP and WebSocket carry the same datagrams. `pc/tools/xport_selftest.c` checks the RFC 6455 accept string, a binary-frame round trip, and an ACNP reliable ping/ack on both transports. It does not need a disc image.
4. `pc/server/start-server.sh` reads `server_config.ini` and starts `--host --dedicated`.

What this milestone does not claim:

- A disc image is required before the process can load assets and simulate a town. This repository must not contain one. Without a GAFE01 image the binary is expected to stop at the disc check. Save load of a real town across restarts was therefore not executed here. The code path is the one the multiplayer branch already uses: first launch generates or adopts a town into `servers/default/town/`, later launches open that directory as Card A.
- There is no Android or browser client in this milestone. Native clients still join over UDP.
- Villager and wildlife behaviour was not re-verified on a headless Linux process.

## Known constraints

These come from the original game and from the multiplayer branch. They are limits of the simulation, not of the socket layer.

- **4 residents + 1 visitor.** `PLAYER_NUM` is 4 and `FOREIGNER_NUM` is 1 (`include/m_personal_id.h`). The transport can accept more TCP/UDP peers (`max_peers`, up to 254) but the town still has four resident slots. Extra people are guests. The original "one foreigner in town" rule is what the guest work in the multiplayer branch is gradually replacing; the save format still has one foreigner slot.
- **6 AM daily reset.** `mTM_FIELD_RENEW_HOUR` is 6 (`include/m_time.h`). The host applies the new day (growth, shop, mail, events). Clients follow the host clock. A change to `Save_t.time_delta` is a discontinuity the host broadcasts; it is not a second clock owned by each client.
- **Year cap.** The original game's `mTM_MAX_YEAR` is 2030. This PC port raises that macro to 2100 under `TARGET_PC` (`include/m_time.h`), but the calendar tables in `src/lb_reki.c` stop at 2030 (`lbRk_HARVEST_MOON_YEAR_MAX`). Dates past 2030 are not a solved problem.
- **Villager simulation follows loaded acres.** The GameCube game simulates villagers in the acres loaded around the active player, not as an independent server-side population of the whole town. A dedicated host runs as an observer parked in the field (`--host-observer`) rather than as a resident walking the map. Upstream notes (see `docs/multiplayer-guest-roadmap.md`) that a villager inside a house is simulated by whatever process has that room loaded, and that a dedicated host has no avatar and does not load every interior. Event NPCs were special-cased so the host can publish presence without a "player nearby" check; ordinary villagers were not. Expect villagers far from the observer, and villagers inside houses nobody is visiting, to be idle or absent until a client is actually there. Do not promise a Pocket Camp-style town that lives while every player is logged out beyond what the host's loaded scene already ticks (clock, daily reset, shops, the host wildlife table).

## Later milestones

### One codebase, three clients

Build the same `pc/` sources three ways:

- Linux/Windows native (what this branch builds), UDP to the server.
- Android, GLES 3, touch overlay. Prior art: [zwaetschge/ACGC-Android-Port](https://github.com/zwaetschge/ACGC-Android-Port) (same CC0 decomp + MIT PC-port licence as upstream). Study its EGL/GLES context and touch overlay. Do not copy files in blindly; keep this repo's MIT header on new code and do not take assets.
- Browser, Emscripten + WebGL 2, WebSocket to `ws_port`. Prior art: [turtlekiosk/slider](https://github.com/turtlekiosk/slider) `wasm` branch (Emscripten, WebGL 2, touch joystick). Slider's own web/Android modifications are MIT (Copyright 2026 turtlekiosk). It also vendors nipplejs (MIT) and the Work Sans font (SIL OFL). Those are not imported here. The WebSocket server in this branch speaks raw RFC 6455 so a later Emscripten client can use the browser `WebSocket` API without a UDP shim.

[Dia2809/ACGC-PC-Port](https://github.com/Dia2809/ACGC-PC-Port) is a Linux fork of the single-player port (CC0 + MIT, same shape as flyngmt). Useful for Linux build notes. It has no dedicated server and no WebSocket layer; this branch does not vendor it.

The client binary should stay one code path with a platform file for window, GL, audio and input. The server stays the native headless binary.

### Touch controls

Pocket Camp-style, not a GameCube pad drawn on the screen:

- Tap the ground to walk there (tap-to-move), instead of a virtual stick as the primary control. A small joystick can exist as a fallback; slider's nipplejs approach is a reference, not the layout to copy.
- One contextual action button (the GameCube A button) whose label follows what the player is standing next to: talk, pick up, drop, shop, confirm.
- Camera drag, and a way to open the menu. L/R/X/Y/Z stay available but are not the first thing on screen.

The desktop client has a first pass of this (`settings.ini` `touch_controls` = `auto`, `on`, or `off`, `pc/src/pc_touch.c`). A tap or a left click steers the stick toward that ground point. On-screen A, B, Tool, and Emote buttons hold those controls. `auto` turns the overlay on only when SDL reports a touch device, so a normal mouse-and-keyboard session stays as it was until the setting is `on`. The action button is still labelled A; it does not yet rename itself for talk or pick up. Camera drag is not in this pass. The same functions are what an Android or browser build should call. Nothing is added to the network protocol.

### Pocket Camp-style HUD

An overlay drawn by the client, not a rewrite of every GameCube menu on day one.

- First: inventory and the town map.
- Later: catalog, nook phone-style contacts, events. The GameCube menus remain the fallback until each screen has an overlay.

The server does not render this HUD. It already sends the inventory and field state the overlay would show.

## Build notes for the server

A dedicated server under systemd has no terminal. Operator lines (`help`, `status`, `players`, `kick`, `promote`, `save`, `stop`) can be written to `servers/<id>/console.fifo`.

The Linux server that was built and started is 32-bit (`-m32 -malign-double`), which matches the GameCube/`long`-is-32-bit ABI and the 8-byte alignment of `double` the save layout expects. `u32` and `s32` are `int` on LP64 so a future 64-bit Linux build does not treat them as 8-byte `long`, but many other headers still use `long` for 32-bit fields, so `-DPC_ALLOW_64BIT=ON` is not a finished Linux configuration. See the README for the exact commands.

Do not commit disc images, extracted assets, `servers/`, or `save/`.
