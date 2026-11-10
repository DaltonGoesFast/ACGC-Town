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

Shared ownership of one house, and rooms past the main floor / upstairs / basement, are under [Custom features](#custom-features). They are **needs matching server** and were not implemented.

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

## Custom features

Research only. Neither change is a local toggle, and neither is small enough to land without a host transaction and a save decision. Nothing here was implemented.

### Shared houses

**Difficulty:** medium. The host already has the pieces (house sync, a loan mirror, mailbox transactions, and a guest roommate list). Co-ownership is a new rule on top of those, not a new room.

**How a house is tied to one player today**

| Thing | Where it lives | Who may change it |
| --- | --- | --- |
| Which of the 4 houses a resident occupies | `Save_t.house_arrangement`, one byte. Two bits per resident slot (`PLAYER_NUM` is 4). `mHS_get_arrange_idx` / `mHS_set_use` in `src/game/m_house.c`. | The host, when a house is bought or swapped. `mHS_get_pl_no` returns the first slot whose bits match, so two slots cannot share one id. |
| Owner identity | `homes[h].ownerID`, a `PersonalID_c` (20 bytes) inside `mHm_hs_c`. Set by `mHm_InitHomeInfo`. | The host. A guest whose key equals an owner is refused as a guest. |
| Loan / debt | `private_data[slot].inventory.loan`, a `u32` on the **player**, not on the house. Post-office repay (`m_repay_ovl.c`) writes `Now_Private`. Nook's "built" line sets the next debt from the size: basement 49,800; medium 148,000; large 398,000; upper 798,000 (`pc_house_upgrade_loan_after_ack`). | Host only. A resident client asks with transaction kind 20 (`pc_house_upgrade.h`). The host checks **that resident's** loan, writes `homes[h].size_info`, and stores the new loan on that resident's record. |
| Upgrades | `homes[h].size_info` (6 bytes: size and next size are 3 bits each, plus basement-ordered and statue-ordered) and `flags.has_basement`. Sizes are small, medium, large, upper, statue. | Same kind-20 path. The exterior palette is `outlook_pal` on the same house. |
| Mailbox | `homes[h].mailbox[10]`. Each `Mail_c` is `0x12A` bytes, so one box is `0xBA4` (2,980) bytes. Delivery looks up the house with `mHS_get_arrange_idx(player_no)` (`mMl_send_mail_box_com`). | Host. Send is transaction kind 12, take is kind 13. The client only opens the box after the host has sent all 10 slots. |
| Furniture, dressers, music box | `homes[h].floors[]` | Host. A roommate guest may already commit edits to one house. |

`mHm_hs_c` is `0x26B0` (9,904) bytes. Four of them are `0x9AC0` (39,616) bytes, from `Save_t` offset `0x009CE8` through `0x0137A8`.

**What already shares a house**

Roommates (`pc/include/pc_roommates.h`, `servers/<id>/roommates.ini`, message 104) let a **guest** live in a resident's house: furniture, dressers, the music box, and entering without knocking. The file says house upgrades and house mail stay the holder's, and a resident cannot ask to move in (`PC_ROOM_R_NOT_GUEST`) because a resident already has a house. The relation is not in `Save_t`.

**Save-format change**

Do not widen `house_arrangement` and do not append co-owners inside `mHm_hs_c`. Either one shifts or redefines bytes that every GCI, town-fetch, and `pc_save_bswap.c` already lays out. The 36-byte `unk_286C` pad can hold one extra `PersonalID` and not a list of guests.

Keep co-owners beside roommates, in a host file such as `servers/<id>/coowners.ini` (same 20-byte identity keys, house 0..3, a generation counter). Old towns load with an empty file: one owner, today's loan, today's mailbox. **needs matching server.** Town-fetch of the GCI does not carry this file; the host has to tell clients the member list.

**Plan**

1. The host is the only writer. A client never subtracts `inventory.loan` or edits `mailbox[]` because it is a co-owner.
2. Membership: the owner invites a resident or a guest. Cap it (four co-owners is enough for the four resident slots plus guests already capped per house by roommates). Removing the owner dissolves the list or promotes the next member and rewrites `ownerID` in one durable save.
3. The canonical debt stays `private_data[owner].inventory.loan` so Nook's kind-20 checks keep working. A co-owner payment is a new transaction: the host checks the **payer's** wallet on the payer's record, subtracts the bells there, subtracts the same amount from the **owner's** loan, saves, then pushes both records. The post-office overlay on a client must call that transaction instead of `m_repay_ovl.c`'s local write.
4. Leave upgrades on the owner for the first version. Kind 20 already requires the owner's loan to be zero and writes that house's `size_info`. Co-owners paying the loan is what unblocks the owner's existing Nook dialogue.
5. Leave the mailbox as the owner's box in the first version. Kind 13 already refuses everyone except the owner. Opening it for co-owners is a later change to that same check, not a second mailbox.
6. Roommates and co-owners should be one relation or explicitly allowed together. Two lists that both grant furniture writes will race.

### Bigger houses

**Difficulty:** hard for a new room or a room larger than the existing large layout. Medium only for a host flag that loads a scene the disc already has, and even that flag is not a one-file change.

**What a house can already be**

`mHm_ROOM_NUM` is 3: main (`floors[0]`), upstairs (`floors[1]`), basement (`floors[2]`). Each floor is an `mHm_flr_c` of `0x8A8` (2,216) bytes. A floor holds four 16×16 layers (`UT_X_NUM` / `UT_Z_NUM` are 16; one `mHm_lyr_c` is `0x228`). `layer_main` is the furniture the player walks around. `layer_secondary` is the other item layer of that same room (wall and door templates, feng shui). `layer_storage1` and `layer_storage2` are more grids in the same floor blob. House sync copies them. They are not extra scenes.

The walkable square is smaller than the 16×16 grid (`m_huusui_room_ovl.c`): small 6, medium 8, large 10. Upstairs uses the medium cap (8). The basement uses the large cap (10). Wall and door items for those sizes are copied across the full 16×16 in `mHm_ChangeWallDoorFG`.

Scenes (`include/m_scene_table.h`), and the floor `mFI_GetPlayerHouseFloorNo` assigns:

| Scene | Floor index | Role |
| --- | --- | --- |
| `SCENE_MY_ROOM_S`, `_M`, `_L`, `_LL1` | 0 | Main room. Small / medium / large / the second large layout. |
| `SCENE_MY_ROOM_LL2` | 1 | Upstairs only. |
| `SCENE_MY_ROOM_BASEMENT_S`, `_M`, `_L`, `_LL1` | 2 | Basement. Reuses the main-room sizes. |
| `SCENE_COTTAGE_MY` | 0, island | The cottage (`mHm_cottage_c`, `0x8C8`), not one of the four town houses. |

Player interiors use four field ids, `mFI_FIELD_PLAYER0_ROOM` through `PLAYER3_ROOM`. `mFI_GET_PLAYER_ROOM_NO` keeps only the low two bits, so a fifth field id is not a free slot. Stairs and the room shell come from that scene's data on the disc (`src/data/scene/player_room_*.c` points at object banks and door actors). `size_info.size` is 3 bits and already uses 0..4.

**Save-size impact**

| Change | Bytes | GCI |
| --- | --- | --- |
| Another floor on every house (`floors[4]`) | `4 * 0x8A8` = 8,864 bytes inserted before `fg[]` | Breaks the layout. Everything after `homes[]` moves, including acres, villagers, and the shop. Town-fetch and the bswap have to move with it, and old files need a migration. |
| One extra floor for one house, stored beside the GCI | 2,216 bytes plus a header | The GCI stays the vanilla size. The host owns the blob the way it owns `roommates.ini`. |
| A larger **scene** for a size the house already has (load `SCENE_MY_ROOM_LL1` where the game loads `SCENE_MY_ROOM_L`) | 0 in `Save_t` | The 16×16 grid already has room for a 10-unit interior. No new furniture slots. |

There is no scene whose walkable area is bigger than 10. A 12-unit room or a fourth shape needs a new background and collision. Those have to be original assets. Deluxe's room models are not usable here.

**Plan**

1. Do not grow `mHm_hs_c`. A new visitable room is a host-side floor blob plus a scene the clients already share.
2. First, if "bigger" means the existing large upstairs shell: a host-owned size flag that selects `SCENE_MY_ROOM_LL1` for the main floor. Every place that switches on `SCENE_MY_ROOM_*` has to agree (`m_play.c`, `m_field_make.c`, `m_room_type.c`, `m_kankyo.c`, `m_collision_bg.c`, the HRA unit cap). Clients must be told the scene by the host before they walk in. **needs matching server.** Still easy to get wrong, which is why it is not in this change.
3. A further room reuses the basement or LL2 **geometry** only. The floor index is a function of the scene id, so the new room needs its own scene id and a new arm in `mFI_GetPlayerHouseFloorNo`, while the field id stays one of the four player rooms. The stairs actor in the current scene data does not point at a fourth floor.
4. Wallpaper, flooring, and the music box are already per floor (`mHm_wf_c`, `tempo_beat`). A new floor copies that header. It does not need a new item id.
5. HRA scoring walks the layers it knows (`m_huusui_room_ovl.c`, `m_mark_room_ovl.c`). A new floor is invisible to the Happy Room Academy until those tables grow, on the host, with the same rules on every client.

## What this pass did not run

No GAFE01 disc image was available. `fast_text` and `show_acre` compile in the 32-bit Linux client. They were not exercised in a town: no dialogue box was opened, and the acre label was not looked at on a screen. Windows was not linked here.
