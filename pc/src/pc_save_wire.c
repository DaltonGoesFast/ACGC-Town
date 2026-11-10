/* Field-by-field copy between the native Save_t and the 148128-byte image the 32-bit server writes.
 * Each row is a member. The build fails if that member does not sit at the server's offset. */
#include "pc_save_wire.h"

#include <string.h>

#define SPAN(member, wire) { (unsigned)offsetof(Save_t, member), (unsigned)(wire) }

typedef struct {
    unsigned native_off;
    unsigned wire_off;
} PcSaveSpan;

static const PcSaveSpan k_spans[] = {
    SPAN(save_check, 0x000000),
    SPAN(scene_no, 0x000014),
    SPAN(now_npc_max, 0x000018),
    SPAN(remove_animal_idx, 0x000019),
    SPAN(copy_protect, 0x00001A),
    SPAN(pad_1C, 0x00001C),
    SPAN(private_data, 0x000020),
    SPAN(land_info, 0x009120),
    SPAN(noticeboard, 0x00912C),
    SPAN(pad_9CE4, 0x009CE4),
    SPAN(homes, 0x009CE8),
    SPAN(fg, 0x0137A8),
    SPAN(combi_table, 0x0173A8),
    SPAN(animals, 0x017438),
    SPAN(last_removed_animal_id, 0x020330),
    SPAN(shop, 0x020340),
    SPAN(kabu_price_schedule, 0x020480),
    SPAN(event_save_data, 0x020498),
    SPAN(event_save_common, 0x020554),
    SPAN(fruit, 0x020688),
    SPAN(house_arrangement, 0x02068A),
    SPAN(num_statues, 0x02068B),
    SPAN(all_grow_renew_time, 0x02068C),
    SPAN(post_office, 0x020694),
    SPAN(police_box, 0x020ED0),
    SPAN(snowmen, 0x020EF8),
    SPAN(melody, 0x020F08),
    SPAN(config, 0x020F10),
    SPAN(renew_time, 0x020F14),
    SPAN(station_type, 0x020F18),
    SPAN(weather, 0x020F19),
    SPAN(save_exist, 0x020F1A),
    SPAN(npc_force_go_home, 0x020F1B),
    SPAN(deposit, 0x020F1C),
    SPAN(last_grow_time, 0x0212DC),
    SPAN(mother_mail, 0x0212E4),
    SPAN(mushroom_time, 0x02131C),
    SPAN(_021322, 0x021322),
    SPAN(_021326, 0x021326),
    SPAN(npc_used_tbl, 0x02134E),
    SPAN(_02136E, 0x02136E),
    SPAN(cheated_flag, 0x021376),
    SPAN(_021377, 0x021377),
    SPAN(treasure_buried_time, 0x02137E),
    SPAN(treasure_checked_time, 0x021386),
    SPAN(saved_rom_debug, 0x02138E),
    SPAN(snowman_year, 0x02138F),
    SPAN(snowman_month, 0x021390),
    SPAN(snowman_day, 0x021391),
    SPAN(snowman_hour, 0x021392),
    SPAN(haniwa_scheduled, 0x021393),
    SPAN(dust_flag, 0x021394),
    SPAN(clear_grass, 0x021395),
    SPAN(_021396, 0x021396),
    SPAN(event_year, 0x021398),
    SPAN(unused_2139C, 0x02139A),
    SPAN(keep_house_size, 0x0213A0),
    SPAN(force_remove_date, 0x0213A4),
    SPAN(museum_display, 0x0213A8),
    SPAN(_tmp6, 0x0213E7),
    SPAN(bridge, 0x0213F0),
    SPAN(needlework, 0x021400),
    SPAN(_tmp7, 0x022500),
    SPAN(time_delta, 0x022528),
    SPAN(island, 0x022540),
    SPAN(allgrow_ss_pos_info, 0x023E40),
    SPAN(fishRecord, 0x023E68),
    SPAN(mask_cat, 0x023F20),
    SPAN(return_animal, 0x024160),
    SPAN(LightHouse, 0x02416C),
    SPAN(insect_term, 0x024174),
    SPAN(insect_term_transition_offset, 0x024175),
    SPAN(gyoei_term, 0x024176),
    SPAN(gyoei_term_transition_offset, 0x024177),
    SPAN(good_field, 0x024178),
    SPAN(bg_tex_idx, 0x024184),
    SPAN(rainbow_month, 0x024185),
    SPAN(rainbow_day, 0x024186),
    SPAN(rainbow_reserved, 0x024187),
    SPAN(_24188, 0x024188),
    SPAN(first_present, 0x024189),
    SPAN(town_day, 0x02418A),
    SPAN(_2418B, 0x02418B),
    SPAN(travel_hard_time, 0x024198),
    SPAN(saved_auto_nwrite_time, 0x0241A0),
    SPAN(_241A8, 0x0241A8),
};

_Static_assert(offsetof(Save_t, save_check) == 0x0, "Save_t.save_check moved off the server layout");
_Static_assert(offsetof(Save_t, scene_no) == 0x14, "Save_t.scene_no moved off the server layout");
_Static_assert(offsetof(Save_t, now_npc_max) == 0x18, "Save_t.now_npc_max moved off the server layout");
_Static_assert(offsetof(Save_t, remove_animal_idx) == 0x19, "Save_t.remove_animal_idx moved off the server layout");
_Static_assert(offsetof(Save_t, copy_protect) == 0x1A, "Save_t.copy_protect moved off the server layout");
_Static_assert(offsetof(Save_t, pad_1C) == 0x1C, "Save_t.pad_1C moved off the server layout");
_Static_assert(offsetof(Save_t, private_data) == 0x20, "Save_t.private_data moved off the server layout");
_Static_assert(offsetof(Save_t, land_info) == 0x9120, "Save_t.land_info moved off the server layout");
_Static_assert(offsetof(Save_t, noticeboard) == 0x912C, "Save_t.noticeboard moved off the server layout");
_Static_assert(offsetof(Save_t, pad_9CE4) == 0x9CE4, "Save_t.pad_9CE4 moved off the server layout");
_Static_assert(offsetof(Save_t, homes) == 0x9CE8, "Save_t.homes moved off the server layout");
_Static_assert(offsetof(Save_t, fg) == 0x137A8, "Save_t.fg moved off the server layout");
_Static_assert(offsetof(Save_t, combi_table) == 0x173A8, "Save_t.combi_table moved off the server layout");
_Static_assert(offsetof(Save_t, animals) == 0x17438, "Save_t.animals moved off the server layout");
_Static_assert(offsetof(Save_t, last_removed_animal_id) == 0x20330, "Save_t.last_removed_animal_id moved off the server layout");
_Static_assert(offsetof(Save_t, shop) == 0x20340, "Save_t.shop moved off the server layout");
_Static_assert(offsetof(Save_t, kabu_price_schedule) == 0x20480, "Save_t.kabu_price_schedule moved off the server layout");
_Static_assert(offsetof(Save_t, event_save_data) == 0x20498, "Save_t.event_save_data moved off the server layout");
_Static_assert(offsetof(Save_t, event_save_common) == 0x20554, "Save_t.event_save_common moved off the server layout");
_Static_assert(offsetof(Save_t, fruit) == 0x20688, "Save_t.fruit moved off the server layout");
_Static_assert(offsetof(Save_t, house_arrangement) == 0x2068A, "Save_t.house_arrangement moved off the server layout");
_Static_assert(offsetof(Save_t, num_statues) == 0x2068B, "Save_t.num_statues moved off the server layout");
_Static_assert(offsetof(Save_t, all_grow_renew_time) == 0x2068C, "Save_t.all_grow_renew_time moved off the server layout");
_Static_assert(offsetof(Save_t, post_office) == 0x20694, "Save_t.post_office moved off the server layout");
_Static_assert(offsetof(Save_t, police_box) == 0x20ED0, "Save_t.police_box moved off the server layout");
_Static_assert(offsetof(Save_t, snowmen) == 0x20EF8, "Save_t.snowmen moved off the server layout");
_Static_assert(offsetof(Save_t, melody) == 0x20F08, "Save_t.melody moved off the server layout");
_Static_assert(offsetof(Save_t, config) == 0x20F10, "Save_t.config moved off the server layout");
_Static_assert(offsetof(Save_t, renew_time) == 0x20F14, "Save_t.renew_time moved off the server layout");
_Static_assert(offsetof(Save_t, station_type) == 0x20F18, "Save_t.station_type moved off the server layout");
_Static_assert(offsetof(Save_t, weather) == 0x20F19, "Save_t.weather moved off the server layout");
_Static_assert(offsetof(Save_t, save_exist) == 0x20F1A, "Save_t.save_exist moved off the server layout");
_Static_assert(offsetof(Save_t, npc_force_go_home) == 0x20F1B, "Save_t.npc_force_go_home moved off the server layout");
_Static_assert(offsetof(Save_t, deposit) == 0x20F1C, "Save_t.deposit moved off the server layout");
_Static_assert(offsetof(Save_t, last_grow_time) == 0x212DC, "Save_t.last_grow_time moved off the server layout");
_Static_assert(offsetof(Save_t, mother_mail) == 0x212E4, "Save_t.mother_mail moved off the server layout");
_Static_assert(offsetof(Save_t, mushroom_time) == 0x2131C, "Save_t.mushroom_time moved off the server layout");
_Static_assert(offsetof(Save_t, _021322) == 0x21322, "Save_t._021322 moved off the server layout");
_Static_assert(offsetof(Save_t, _021326) == 0x21326, "Save_t._021326 moved off the server layout");
_Static_assert(offsetof(Save_t, npc_used_tbl) == 0x2134E, "Save_t.npc_used_tbl moved off the server layout");
_Static_assert(offsetof(Save_t, _02136E) == 0x2136E, "Save_t._02136E moved off the server layout");
_Static_assert(offsetof(Save_t, cheated_flag) == 0x21376, "Save_t.cheated_flag moved off the server layout");
_Static_assert(offsetof(Save_t, _021377) == 0x21377, "Save_t._021377 moved off the server layout");
_Static_assert(offsetof(Save_t, treasure_buried_time) == 0x2137E, "Save_t.treasure_buried_time moved off the server layout");
_Static_assert(offsetof(Save_t, treasure_checked_time) == 0x21386, "Save_t.treasure_checked_time moved off the server layout");
_Static_assert(offsetof(Save_t, saved_rom_debug) == 0x2138E, "Save_t.saved_rom_debug moved off the server layout");
_Static_assert(offsetof(Save_t, snowman_year) == 0x2138F, "Save_t.snowman_year moved off the server layout");
_Static_assert(offsetof(Save_t, snowman_month) == 0x21390, "Save_t.snowman_month moved off the server layout");
_Static_assert(offsetof(Save_t, snowman_day) == 0x21391, "Save_t.snowman_day moved off the server layout");
_Static_assert(offsetof(Save_t, snowman_hour) == 0x21392, "Save_t.snowman_hour moved off the server layout");
_Static_assert(offsetof(Save_t, haniwa_scheduled) == 0x21393, "Save_t.haniwa_scheduled moved off the server layout");
_Static_assert(offsetof(Save_t, dust_flag) == 0x21394, "Save_t.dust_flag moved off the server layout");
_Static_assert(offsetof(Save_t, clear_grass) == 0x21395, "Save_t.clear_grass moved off the server layout");
_Static_assert(offsetof(Save_t, _021396) == 0x21396, "Save_t._021396 moved off the server layout");
_Static_assert(offsetof(Save_t, event_year) == 0x21398, "Save_t.event_year moved off the server layout");
_Static_assert(offsetof(Save_t, unused_2139C) == 0x2139A, "Save_t.unused_2139C moved off the server layout");
_Static_assert(offsetof(Save_t, keep_house_size) == 0x213A0, "Save_t.keep_house_size moved off the server layout");
_Static_assert(offsetof(Save_t, force_remove_date) == 0x213A4, "Save_t.force_remove_date moved off the server layout");
_Static_assert(offsetof(Save_t, museum_display) == 0x213A8, "Save_t.museum_display moved off the server layout");
_Static_assert(offsetof(Save_t, _tmp6) == 0x213E7, "Save_t._tmp6 moved off the server layout");
_Static_assert(offsetof(Save_t, bridge) == 0x213F0, "Save_t.bridge moved off the server layout");
_Static_assert(offsetof(Save_t, needlework) == 0x21400, "Save_t.needlework moved off the server layout");
_Static_assert(offsetof(Save_t, _tmp7) == 0x22500, "Save_t._tmp7 moved off the server layout");
_Static_assert(offsetof(Save_t, time_delta) == 0x22528, "Save_t.time_delta moved off the server layout");
_Static_assert(offsetof(Save_t, island) == 0x22540, "Save_t.island moved off the server layout");
_Static_assert(offsetof(Save_t, allgrow_ss_pos_info) == 0x23E40, "Save_t.allgrow_ss_pos_info moved off the server layout");
_Static_assert(offsetof(Save_t, fishRecord) == 0x23E68, "Save_t.fishRecord moved off the server layout");
_Static_assert(offsetof(Save_t, mask_cat) == 0x23F20, "Save_t.mask_cat moved off the server layout");
_Static_assert(offsetof(Save_t, return_animal) == 0x24160, "Save_t.return_animal moved off the server layout");
_Static_assert(offsetof(Save_t, LightHouse) == 0x2416C, "Save_t.LightHouse moved off the server layout");
_Static_assert(offsetof(Save_t, insect_term) == 0x24174, "Save_t.insect_term moved off the server layout");
_Static_assert(offsetof(Save_t, insect_term_transition_offset) == 0x24175, "Save_t.insect_term_transition_offset moved off the server layout");
_Static_assert(offsetof(Save_t, gyoei_term) == 0x24176, "Save_t.gyoei_term moved off the server layout");
_Static_assert(offsetof(Save_t, gyoei_term_transition_offset) == 0x24177, "Save_t.gyoei_term_transition_offset moved off the server layout");
_Static_assert(offsetof(Save_t, good_field) == 0x24178, "Save_t.good_field moved off the server layout");
_Static_assert(offsetof(Save_t, bg_tex_idx) == 0x24184, "Save_t.bg_tex_idx moved off the server layout");
_Static_assert(offsetof(Save_t, rainbow_month) == 0x24185, "Save_t.rainbow_month moved off the server layout");
_Static_assert(offsetof(Save_t, rainbow_day) == 0x24186, "Save_t.rainbow_day moved off the server layout");
_Static_assert(offsetof(Save_t, rainbow_reserved) == 0x24187, "Save_t.rainbow_reserved moved off the server layout");
_Static_assert(offsetof(Save_t, _24188) == 0x24188, "Save_t._24188 moved off the server layout");
_Static_assert(offsetof(Save_t, first_present) == 0x24189, "Save_t.first_present moved off the server layout");
_Static_assert(offsetof(Save_t, town_day) == 0x2418A, "Save_t.town_day moved off the server layout");
_Static_assert(offsetof(Save_t, _2418B) == 0x2418B, "Save_t._2418B moved off the server layout");
_Static_assert(offsetof(Save_t, travel_hard_time) == 0x24198, "Save_t.travel_hard_time moved off the server layout");
_Static_assert(offsetof(Save_t, saved_auto_nwrite_time) == 0x241A0, "Save_t.saved_auto_nwrite_time moved off the server layout");
_Static_assert(offsetof(Save_t, _241A8) == 0x241A8, "Save_t._241A8 moved off the server layout");

_Static_assert(sizeof(Save_t) == PC_SAVE_WIRE_BYTES, "Save_t is not the server's 148128 bytes");
_Static_assert(sizeof(mEv_special_c) == 84, "event packet special block");
_Static_assert(sizeof(mEv_weekly_u) == 100, "event packet weekly block");

static void copy_spans(uint8_t* dst_base, const uint8_t* src_base, int to_wire) {
    unsigned i;
    for (i = 0; i < sizeof(k_spans) / sizeof(k_spans[0]); i++) {
        unsigned wire = k_spans[i].wire_off;
        unsigned native = k_spans[i].native_off;
        unsigned next = (i + 1 < sizeof(k_spans) / sizeof(k_spans[0])) ? k_spans[i + 1].wire_off : PC_SAVE_WIRE_BYTES;
        unsigned span = next - wire;
        if (to_wire) memcpy(dst_base + wire, src_base + native, span);
        else memcpy(dst_base + native, src_base + wire, span);
    }
}

void pc_save_wire_store(const Save_t* native, void* wire) {
    memset(wire, 0, PC_SAVE_WIRE_BYTES);
    copy_spans((uint8_t*)wire, (const uint8_t*)native, 1);
}

void pc_save_wire_load(const void* wire, Save_t* native) {
    memset(native, 0, sizeof(*native));
    copy_spans((uint8_t*)native, (const uint8_t*)wire, 0);
}
