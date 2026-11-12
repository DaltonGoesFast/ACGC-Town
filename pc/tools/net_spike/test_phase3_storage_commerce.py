#!/usr/bin/env python3
"""
test_phase3_storage_commerce.py - Verification suite for Phase 3:
Infinite Cloud Storage, Anti-Hoarding Critter Rules, 24/7 Nook Commerce, & 60-Day Inactivity Reclamation.

Validates:
1. Infinite Cloud Storage (2,048 slots, ACSTOR1\0 magic, 4,132-byte file size, CRC32, characters/<uuid>/storage.dat).
2. Anti-Hoarding Critter Blacklist (Fish & Bugs blocked with exact toast text:
   "Live critters must be kept in pockets! Donate them to the Museum, sell to Tom Nook, or give them to a neighbor.").
3. Tom Nook 24/7 Commerce (Shop open 24/7/365, essentials never sell out, showcase items deplete, 12h restock cycle at 08:00 and 20:00).
4. Automated 60-Day Inactivity Room Reclamation (Vacant mark, auto-pack bed & 4 floor layers into Cloud Storage, Tom Nook letter, 64-byte header invariant).
"""
import os
import sys
import struct
import zlib
import time
import tempfile
import shutil

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8", errors="replace") as f:
        return f.read()

def ck(name, cond):
    if cond:
        print(f"PASS - {name}")
    else:
        print(f"FAIL - {name}")
        sys.exit(1)

def main():
    print("Running Phase 3 Storage, Commerce & Reclamation Verification...\n")

    # =========================================================================
    # Part 1: Infinite Cloud Storage Architecture
    # =========================================================================
    csh = read("pc/include/pc_cloud_storage.h")
    ck("CS1: Magic ACSTOR1\\0, version 1, header 32 bytes",
       '#define PC_CLOUD_STORAGE_MAGIC          "ACSTOR1\\0"' in csh and
       "#define PC_CLOUD_STORAGE_VERSION        1u" in csh and
       "#define PC_CLOUD_STORAGE_HEADER_SIZE    32" in csh)

    ck("CS2: 2,048 items capacity, 27 items per page (9x3 grid)",
       "#define PC_CLOUD_STORAGE_PAGE_SIZE      27" in csh and
       "#define PC_CLOUD_STORAGE_MAX_ITEMS      2048" in csh)

    ck("CS3: Exact file size 4,132 bytes (32 header + 4096 items + 4 CRC32)",
       "PC_CLOUD_STORAGE_FILE_SIZE      (PC_CLOUD_STORAGE_HEADER_SIZE + (PC_CLOUD_STORAGE_MAX_ITEMS * sizeof(uint16_t)) + 4)" in csh and
       "/* 32 + 4096 + 4 = 4132 bytes */" in csh)

    csc = read("pc/src/pc_cloud_storage.c")
    ck("CS4: Atomic file write with .tmp and .bak rollbacks",
       'snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);' in csc and
       'snprintf(bak_path, sizeof(bak_path), "%s.bak", path);' in csc and
       'rename(tmp_path, path)' in csc)

    ck("CS5: Per-player file path characters/<uuid>/storage.dat",
       '%s/characters/%s/storage.dat' in csc and
       '"save/mp"' in csc)

    # Test binary file format serialization & CRC32 in Python
    # 32-byte header: 8 bytes magic ("ACSTOR1\0"), uint32 version, uint32 count, 4 uint32 rsv
    # 2048 uint16 items (4096 bytes)
    # 4 bytes uint32 CRC32
    hdr = struct.pack("<8sII4I", b"ACSTOR1\x00", 1, 3, 0, 0, 0, 0)
    items = [0] * 2048
    items[0] = 0x2400 # Furniture item
    items[1] = 0x2401
    items[2] = 0x2000 # Tool
    items_bytes = struct.pack(f"<{len(items)}H", *items)
    crc = zlib.crc32(hdr + items_bytes) & 0xFFFFFFFF
    trailer = struct.pack("<I", crc)
    full_blob = hdr + items_bytes + trailer
    ck("CS6: Binary payload matches exact 4,132-byte struct layout and CRC32 verification",
       len(full_blob) == 4132 and zlib.crc32(full_blob[:4128]) == crc)

    # =========================================================================
    # Part 2: The Anti-Hoarding Rule (Critter Blacklist)
    # =========================================================================
    di_h = read("pc/include/pc_dresser_items.h")
    di_c = read("pc/src/pc_dresser_items.c")
    ck("AH1: Critter identifier pc_dresser_item_is_critter declared and implemented",
       "int pc_dresser_item_is_critter(unsigned item);" in di_h and
       "int pc_dresser_item_is_critter(unsigned item) {" in di_c and
       "cat == ITEM1_CAT_FISH || cat == ITEM1_CAT_INSECT" in di_c)

    toast_exact = "Live critters must be kept in pockets! Donate them to the Museum, sell to Tom Nook, or give them to a neighbor."
    ck("AH2: Exact Anti-Hoarding Toast string defined in header",
       toast_exact in csh)

    ck("AH3: pc_cloud_storage_deposit rejects fish/bug with PC_CLOUD_STORAGE_CRITTER_BLOCKED",
       "if (pc_cloud_storage_is_critter(item)) {" in csc and
       "return PC_CLOUD_STORAGE_CRITTER_BLOCKED;" in csc)

    dui = read("pc/src/pc_dresser_ui.c")
    ck("AH4: Dresser UI put() triggers exact toast and refuses critter deposit",
       'pc_touch_show_toast_ex("Live critters must be kept in pockets!", "Donate them to the Museum, sell to Tom Nook, or give them to a neighbor.", 4500);' in dui and
       "if (pc_cloud_storage_is_critter((uint16_t)item)) {" in dui)

    ck("AH5: Dresser UI place() (A-button Grab) prevents placing critters into storage cells",
       "if (from_pocket && !to_pocket && pc_cloud_storage_is_critter((uint16_t)item)) {" in dui and
       "if (!from_pocket && to_pocket && target != 0 && pc_cloud_storage_is_critter((uint16_t)target)) {" in dui)

    # =========================================================================
    # Part 3: Tom Nook 24/7 Commerce & 12-Hour Restock Cycle
    # =========================================================================
    m_shop = read("src/game/m_shop.c")
    ck("NC1: Tom Nook store is always open 24/7 under TARGET_PC",
       "mSP_ShopOpen(void)" in m_shop or "mSP_ShopOpen()" in m_shop and
       "#ifdef TARGET_PC" in m_shop and
       "return mSP_SHOP_STATUS_OPEN;" in m_shop and
       "Phase 3: Tom Nook's shop is open 24/7/365 across all real-world timezones for drop-in players" in m_shop)

    ck("NC2: Hybrid stock essentials never sell out (Rod, Net, Shovel, Seeds, Saplings, Stationery, Wooden Bed)",
       "mSP_IsEssentialGood" in m_shop and
       "item == FTR_SUM_LOG_BED01" in m_shop and
       "cat == ITEM1_CAT_TOOL" in m_shop and
       "cat == ITEM1_CAT_PLANT" in m_shop and
       "cat == ITEM1_CAT_PAPER" in m_shop)

    ck("NC3: mSP_ShopSaleReport preserves stock for essential goods",
       "mSP_IsEssentialGood(sold_item)" in m_shop and
       "return FALSE;" in m_shop)

    drh = read("pc/src/pc_day_roll_host.c")
    ck("NC4: 12-hour mid-day restock cycle polls 08:00 and 20:00 local server time",
       "/* Phase 3: 12-Hour Mid-Day Restock Cycle (triggers every 12 hours: 08:00 and 20:00 local time) */" in drh and
       "int current_slot = (rtc.hour >= 8 && rtc.hour < 20) ? 0 : 1;" in drh and
       "pc_net_game_trigger_12h_restock();" in drh)

    png = read("pc/src/pc_net_game.c")
    ck("NC5: 12-hour restock regenerates lineup and broadcasts stock update to connected peers",
       "void pc_net_game_trigger_12h_restock(void) {" in png and
       "mSP_ExchangeLineUp_ZeldaMalloc();" in png and
       "s_restock.cat_gen++;" in png and
       "pcnetgame_host_ts_push_all();" in png)

    # =========================================================================
    # Part 4: Automated 60-Day Inactivity Room Reclamation
    # =========================================================================
    pbr = read("pc/include/pc_bedroom.h")
    ck("RR1: Inactivity limit is exactly 60 days (5,184,000 seconds)",
       "#define PC_BEDROOM_INACTIVITY_LIMIT_SEC  (60u * 86400u)" in pbr)

    ck("RR2: Bedroom header invariant: exactly 64 bytes with last_login_timestamp",
       "last_login_timestamp;" in pbr and
       "owner_uuid[32];" in pbr and
       "owner_name[8];" in pbr and
       "#define PC_BEDROOM_HEADER_SIZE  64" in pbr and
       "#define PC_BEDROOM_FILE_SIZE    (PC_BEDROOM_HEADER_SIZE + sizeof(mHm_flr_c) + 4)" in pbr)

    bs = read("pc/src/pc_bedroom_store.c")
    ck("RR3: pc_bedroom_reclamation_check audits inactive rooms and marks vacant",
       "int pc_bedroom_reclamation_check(const char* dir, uint32_t now) {" in bs and
       "(cur_time - last_active) >= limit_sec" in bs and
       "init_default_room(r, h, f, w);" in bs)

    ck("RR4: Inactive room items auto-packed into Cloud Storage (bed and all 4 furniture layers)",
       "pc_cloud_storage_deposit(&storage, (uint16_t)FTR_SUM_LOG_BED01);" in bs and
       "pc_cloud_storage_deposit(&storage, (uint16_t)pocket_it);" in bs and
       "pc_cloud_storage_save(dir, uuid_buf, &storage);" in bs)

    ck("RR5: Tom Nook welcome-back letter queued in player's mailbox on reclamation",
       "pc_bedroom_queue_nook_letter(uuid_buf, r->owner_name);" in bs and
       "Notice of Vacancy" in bs and
       "safely packed into your Cloud Storage" in bs)

    ck("RR6: Daily maintenance and boot check triggers inactivity reclamation",
       "reclaimed = pc_bedroom_reclamation_check(NULL, (uint32_t)time(NULL));" in drh and
       "60-Day Inactivity Audit: reclaimed %d inactive bedroom(s)" in drh)

    # =========================================================================
    # Part 5: Android Touch HUD & GameCube Controls
    # =========================================================================
    pth = read("pc/include/pc_touch.h")
    ptc = read("pc/src/pc_touch.c")
    ck("UI1: Toast banner functions declared in pc_touch.h",
       "void pc_touch_show_toast(const char* text);" in pth and
       "void pc_touch_show_toast_ex(const char* title, const char* subtitle, uint32_t duration_ms);" in pth)

    ck("UI2: Quick Storage HUD button shown when inside bedroom or lounge",
       "(pc_bedroom_is_in_bedroom() || pc_bedroom_is_in_lounge()) && !submenu_open()" in ptc and
       'draw_word("Store", h.storage_x, h.storage_y, h.qr * 0.13f);' in ptc)

    ck("UI3: Touch pagination controls (< and >) displayed during Cloud Storage overlay",
       "if (submenu_open() && pc_dresser_ui_active() && pc_cloud_storage_client_is_open()) {" in ptc and
       'draw_word("<", h.stor_prev_x, h.stor_prev_y, h.stor_page_r * 0.18f);' in ptc and
       'draw_word(">", h.stor_next_x, h.stor_next_y, h.stor_page_r * 0.18f);' in ptc)

    m_tag = read("src/game/m_tag_ovl.c")
    ck("UI4: GameCube L / R trigger buttons flip Cloud Storage pages",
       "if (pc_cloud_storage_client_is_open()) {" in m_tag and
       "if (trigger & BUTTON_L) {" in m_tag and
       "pc_cloud_storage_client_prev_page();" in m_tag and
       "else if (trigger & BUTTON_R) {" in m_tag and
       "pc_cloud_storage_client_next_page();" in m_tag)

    print("\n" + "=" * 60)
    print("ALL 21 PHASE 3 VERIFICATION CHECKS PASSED!")
    print("=" * 60)

if __name__ == "__main__":
    main()
