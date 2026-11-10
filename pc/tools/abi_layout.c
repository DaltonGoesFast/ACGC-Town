/* Prints sizeof/offsetof for Save_t and the network structs the dedicated server shares.
 * Built twice (Linux i386 -malign-double, and armeabi-v7a) and diffed. Any difference fails the check. */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include "m_common_data.h"
#include "m_home_h.h"
#include "m_private.h"
#include "m_mail.h"
#include "pc_chat.h"
#include "pc_roommates.h"

#define ROW(name, value) printf("%-40s %u\n", name, (unsigned)(value))

/* Compared as bytes between the two ABIs. Do not run the ARM binary (Bionic will not start under qemu). */
__attribute__((section(".abi_nums"), used))
const unsigned abi_nums[] = {
    (unsigned)sizeof(void*),
    (unsigned)sizeof(long),
    (unsigned)_Alignof(double),
    (unsigned)sizeof(Save_t),
    (unsigned)offsetof(Save_t, private_data),
    (unsigned)offsetof(Save_t, homes),
    (unsigned)offsetof(Save_t, house_arrangement),
    (unsigned)sizeof(Private_c),
    (unsigned)offsetof(Private_c, inventory.loan),
    (unsigned)sizeof(mHm_hs_c),
    (unsigned)offsetof(mHm_hs_c, ownerID),
    (unsigned)offsetof(mHm_hs_c, floors),
    (unsigned)offsetof(mHm_hs_c, mailbox),
    (unsigned)sizeof(Mail_c),
    (unsigned)sizeof(mHm_flr_c),
    (unsigned)sizeof(PCRoomMsg),
    (unsigned)offsetof(PCRoomMsg, op),
    (unsigned)offsetof(PCRoomMsg, e),
    (unsigned)sizeof(PCChatSendMsg),
    (unsigned)offsetof(PCChatSendMsg, text),
    (unsigned)sizeof(PCChatMsg),
    (unsigned)offsetof(PCChatMsg, text),
};

int main(void) {
    ROW("sizeof_void", sizeof(void*));
    ROW("sizeof_long", sizeof(long));
    ROW("alignof_double", _Alignof(double));
    ROW("sizeof_Save_t", sizeof(Save_t));
    ROW("off_private_data", offsetof(Save_t, private_data));
    ROW("off_homes", offsetof(Save_t, homes));
    ROW("off_house_arrangement", offsetof(Save_t, house_arrangement));
    ROW("sizeof_Private_c", sizeof(Private_c));
    ROW("off_Private_loan", offsetof(Private_c, inventory.loan));
    ROW("sizeof_mHm_hs_c", sizeof(mHm_hs_c));
    ROW("off_home_owner", offsetof(mHm_hs_c, ownerID));
    ROW("off_home_floors", offsetof(mHm_hs_c, floors));
    ROW("off_home_mailbox", offsetof(mHm_hs_c, mailbox));
    ROW("sizeof_Mail_c", sizeof(Mail_c));
    ROW("sizeof_mHm_flr_c", sizeof(mHm_flr_c));
    ROW("sizeof_PCRoomMsg", sizeof(PCRoomMsg));
    ROW("off_PCRoomMsg_op", offsetof(PCRoomMsg, op));
    ROW("off_PCRoomMsg_e", offsetof(PCRoomMsg, e));
    ROW("sizeof_PCChatSendMsg", sizeof(PCChatSendMsg));
    ROW("off_PCChatSendMsg_text", offsetof(PCChatSendMsg, text));
    ROW("sizeof_PCChatMsg", sizeof(PCChatMsg));
    ROW("off_PCChatMsg_text", offsetof(PCChatMsg, text));
    return 0;
}
