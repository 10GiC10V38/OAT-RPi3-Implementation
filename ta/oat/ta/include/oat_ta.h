/* include/oat_ta.h */
#ifndef OAT_TA_H  /* <--- CHANGED FROM USER_TA_HEADER_DEFINES_H */
#define OAT_TA_H

/* The UUID */
#define TA_OAT_UUID \
    { 0x92b192d1, 0x9686, 0x424a, \
      { 0x8d, 0x18, 0x97, 0xc1, 0x18, 0x12, 0x95, 0x70} }

/* Command IDs */
#define CMD_HASH_INIT    4
#define CMD_HASH_UPDATE  5
#define CMD_HASH_FINAL   6
#define CMD_STACK_PUSH   0x10
#define CMD_STACK_POP    0x11

/* NOTE: TA_FLAGS, STACK_SIZE, etc. removed from here 
   because they belong in user_ta_header_defines.h */

#endif /* OAT_TA_H */
