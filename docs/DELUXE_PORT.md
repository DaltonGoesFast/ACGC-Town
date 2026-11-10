# Animal Crossing Deluxe, rewritten here

This is an inventory of the public Deluxe quality-of-life list, compared with this tree. The features were read from the Deluxe feature pages ([dxfeatures](https://dethl.neocities.org/acdx/dxfeatures.html), [cuyler36 features](https://cuyler36.github.io/acdx/features/)) and the [FAQ](https://dethl.neocities.org/acdx/faq). Nothing in this document, and nothing in the code that implements a row below, is copied from a Deluxe binary, patch, or asset. The FAQ says the Deluxe patch is not compatible with the PC port. Nookery (flyngmt/Nookery) is a different architecture (Aurora / Vulkan). Where a row says "Nookery has a setting", that is a hook to reimplement, not a file to copy. Licenses on that tree are the same CC0 decomp plus the MIT PC layer.

A row marked **needs matching server** changes shared world state or the save layout. A client that does it alone will disagree with the host. New villagers (e+ / Wild World models, dialogue, NPC tables, save layout) are out of scope for this pass. What that work would take is at the bottom.

Difficulty is about this decomp, not about how Deluxe shipped it.

- **easy** — local presentation or input. No save bytes, no field mutation.
- **medium** — a real system (camera, audio scheduler, menu pages, a small host-owned field change).
- **hard** — new items, NPCs, save layout, calendar tables, or a lot of content.

## Already in this tree

Checked in the source, not assumed from a settings name.

| Feature | Where | Shared state |
| --- | --- | --- |
| D-pad tool cycle outdoors (left/right next tool, down puts it away) | `Player_actor_check_and_switch_tool` in `src/game/m_player.c`, under `PC_ENHANCEMENTS` | No. Each client cycles its own held tool. |
| Shake a tree while holding the net or the rod | `Player_actor_CheckController_forShake_tree` in `src/game/m_player_controller.c_inc` | The shake itself is already host-owned (`pc_net_game_request_tree_shake`). The tool check is local. |
| Picked-up bell bags go to the wallet when the whole amount fits | `mPr_GivePossessionBells` and the pickup path in `m_player_main_pickup.c_inc` | Wallet is player save data. Multiplayer already has a host path for the pickup. |
| Holding B latches the fast dialogue reveal for that message; L/R and the D-pad do the same | `mMsg_Main_Cursol_ControlCursol` | No. The reveal speed is local. The text is the same. |
| Borderless acres (camera and movement do not stop on the acre edge) | `settings.ini` `borderless_acres`, default on | Movement across acres is already how multiplayer walks. This is the local camera/load, not a new field rule. |
| Optional Resetti, optional Nookington's visitor requirement, NES 4:3 | `disable_resetti`, `disable_shop_visitor_req`, `nes_aspect` | Resetti and the shop rule change what the local game requires. In a shared town the host should use the same shop rule. **needs matching server** if the shop flag is on. |
| Outdoor furniture place / rotate / pick up | dedicated server furniture commands and `pc_outdoor_furniture` | **needs matching server**. Already a host feature. |
| Keyboard and controller bindings, including a C-stick | `keybindings.ini` | No. The C-stick is still the GameCube C buttons (first-person and so on), not a free camera. |

`fast_text` and `show_acre` are the two toggles added with this document. Both default on. Both are local.

## Added with this document

| Setting | Default | What it does |
| --- | --- | --- |
| `fast_text` | 1 | Dialogue uses the existing fast reveal (the cursor timer drains at 2x) without holding B. `0` restores the B latch. Gameplay menu: Fast text. |
| `show_acre` | 1 | Outdoors, under the clock, the acre letter and number the bulletin board already uses (`QABCDEF` for the row, digit for the column) plus a short kind (Grass, River, Station, …). Gameplay menu: Acre label. |

Neither writes the save and neither sends a packet.

## Easy, still open

Local only. Safe to default on once they exist. Not done in this pass because each one is a real edit in a system that was not a one-line hook.

| Feature | Why it is still open |
| --- | --- |
| Z+R goes to the previous inventory tab | The submenu page index lives in the inventory overlay. No existing "previous tab" input was found. Client UI only. |
| Item and donation fanfares do not stop the hourly field music | Fanfares are `mBGMPsComp_make_ps_fanfare` with stop type `0x168`. Whether that stops the hourly `FIELDNORM` / scheduled field track is inside the BGM priority scheduler (`m_bgm.c`). Local audio, but not a flag that already exists. |
| HUD pulled to the window edges | Nookery's `hud_at_edges`. This tree stretches some 2D to the window (`g_pc_widescreen_stretch`) and does not reflow the clock, pockets, or map to a wide frame. Client only. |

## Medium

| Feature | Shared state |
| --- | --- |
| Outdoor C-stick camera (orbit / pitch while outside) | Local camera. The C-stick is currently digital C buttons in `padmgr.c`. A free camera must not change the position that is sent to the host. |
| True widescreen play (not a stretched 4:3 framebuffer) | Local render. Collision and acres stay the same. |
| Disable the acre camera transition as its own switch | Overlaps `borderless_acres`. Confirm they are the same before adding a second key. |
| Faster save and load | Local I/O. Must not change the GCI layout. |
| Keyboard town tune entry | The tune is save data. **needs matching server** if other players hear the town tune from the host. |
| Hold Z for pattern transparency, Z+R previous menu tab, Z+D-pad re-place last design, Z+B clear the ground pattern | Transparency can be local. Placing or clearing a ground pattern writes the acre. **needs matching server**. |
| R opens a more detailed map | Local UI if it only draws more labels. A new map layout stored in the save is **needs matching server**. |
| Fill a hole without a shovel, holes that do not despawn by distance | Field tiles. **needs matching server**. |
| Villager move-out chance while a visitor is in town (Deluxe's lower rate) | `m_npc` / travel. Nookery has `travel_move_out_chance`. **needs matching server**. |
| Golden tools tied to perfect-town or sapling counts | Those counters are save data. **needs matching server**. |
| L+R+Z sets the clock from the time menu | `time_delta` and the 6 AM reset are host-owned. **needs matching server**. |
| Z+Start quicksave | A second save of the town. **needs matching server** on a dedicated host (the host already has `save` on the console). |
| Stack fruit, mushrooms, and candy to 10 | Pocket and field item counts. **needs matching server** and a save-layout decision. |
| Catalog sort, Nook's Sunday KK, refusing the statue, Nookington's without a second memory card | Shop state is host-owned in multiplayer. **needs matching server**. |
| Redd's four items and a guaranteed painting | Shop / visitor inventory. **needs matching server**. |
| Island without a GBA, and the island visitor rules | Island field and items. **needs matching server**. |
| Drop bugs and fish, letters thrown indoors, multi-select letters | Inventory mutations. **needs matching server** for anything another player can see. |
| Sit facing the camera on chairs, stumps, and rocks; rotate tabletop items; gyroids on tables | Actor pose can be local if it is not replicated. Placed rotation is furniture state. **needs matching server** when it is stored. |
| House top floor 8x8, basement wallpaper, more tabletops | House save layout. **needs matching server**. |
| Blathers assesses after 15 fossils, mail fossils, multi-piece price, extra fossils | Museum save and item tables. **needs matching server**. |
| Feng shui color updates, extra wishing-well hints, Gracie's car wash discount, quests that progress in igloos | Dialogue and price tables. A price the host charges is **needs matching server**. A hint string can be local. |
| Balloon sound, tree collision bugfix, net on cliff edges | Bugfixes. A collision change that lets you stand somewhere new is **needs matching server** if the host rejects the position. |
| Clock skins | Local HUD, unless the skin id is saved. |

## Hard (content)

All of these add items, models, songs, or calendar rows. They change the save or the item tables, so they are **needs matching server**. They also need original models and text, not Deluxe's assets.

- Storage expanded to 160 slots (Wild World style). Pocket and house item arrays, and every place that assumes 15.
- Player skin, tan, and hats.
- New furniture, clothes, wallpaper, carpets, fossils, gyroids, and patterns (DnM / DnM+ restorations, e+ decor, iQue, Wild World floors and walls, mushrooms, chestnuts, Jacob's ladder, roses).
- KK songs through New Leaf, and looser song-request typing.
- Pattern import and export, Sable palettes, town flag, pattern as the item-screen background.
- Ordinances, pattern stumps, four map layouts.
- Dates past the calendar tables. `lb_reki` stops at 2030. This port's `mTM_MAX_YEAR` is 2100. Nookery's year 3000 cap is the same class of problem. Extending the year without the tables desyncs events.
- Saturday August fireworks, July 4, shrine versus wishing well, Japanese New Year.
- e+ sale day, Chip giving the rod at the tourney, letters and patterns in their own memory-card files.
- Unlocked frame rate with a real delta time. This tree already scales some timers by `dt_num_60fps_frames`. A 200 FPS mode that does not speed the simulation is a separate pass, and the dedicated server must stay the clock.

## Villagers, out of scope

Adding e+, Wild World, or monkey villagers is not part of this milestone. A later milestone would have to do all of the following together, on the host and on every client:

- NPC model, animation, and texture for each new species, drawn by the existing actor, not by a Deluxe asset pack.
- Dialogue banks and the message ids that point at them.
- The villager personality / NPC tables the game indexes by species (`m_npc`, animal personal id, house interior item lists).
- Save layout: the animal array size, house slots, and any new fields. Old GCI files need a versioned migration. The dedicated server's `servers/<id>/town/` copy and the guest town-fetch must use that same layout.
- Move-in, move-out, birthday, and lost-and-found item pools so a new species does not roll an item id the other side does not have.
- The host is the authority for which villager is in which house. A client-only model swap will not survive a reconnect.

Until that exists, the roster stays the GameCube villagers this decomp already simulates.

## What this pass did not run

No GAFE01 disc image was available. `fast_text` and `show_acre` compile in the 32-bit Linux client. They were not exercised in a town: no dialogue box was opened, and the acre label was not looked at on a screen. Windows was not linked here.
