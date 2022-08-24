//------------------------------------------------
// Copyright (c) 2022 stickLabs.io  All rights reserved.
//------------------------------------------------

#include "stickLabs.h"

#define STK_ONSTICK_DATA_VERSION1 (1)
#define STK_ONSTICK_DATA_VERSION2 (2)
#define STK_ONFLASH_DATA_VERSION  (1)
#define STK_NFCID_LENGTH    (64)
#define NFCV_BLOCK_LEN       (4)  // See ST25TV datasheet
#define NFCV_READ_BLOCK_LEN (1 + NFCV_BLOCK_LEN + RFAL_CRC_LEN) // Includes metadata: Flags + Block Data + CRC
#define NFCV_READ_RET_LEN    (5)  // From RFAL API for NFCV: Oddly we request to
                                  // read 7 bytes and it says it returned 5
                                  // bytes (but all 7 bytes (CRC) are actually
                                  // there.

// WHY_START_AT_BLOCK_1 ?
#define STK_DATA_START_OFFSET (4) // Offset in bytes verses start block below
#define STK_DATA_START_BLOCK  (1) // Start at block 1 because this allows the flexibility to:
                                  //     - Password protect *all* our data in Area1
                                  //     - Leave block 0 open for customer applications
                                  //     - See ST25TV02K datasheet regarding Area1 password
                                  // Historical note: Start at block 1 originates from
                                  // Applications/PollingTagDetect/Src/demo_polling.c::demoNfcv()


// Define number of hours using seconds (60*60 seconds in an hour).
// This is tuned based on testing real units in production, so it is not
// exactly 60*60:
#define RTC_FUDGE_FACTOR     ((4*60)+51)   // 4min 51sec
#define TIME_ONE_HOUR_IN_SEC ((60*60)+(RTC_FUDGE_FACTOR))

#define TIME_OPEN_1_HOUR     (TIME_ONE_HOUR_IN_SEC * 1)
#define TIME_OPEN_2_HOURS    (TIME_ONE_HOUR_IN_SEC * 2)
#define TIME_OPEN_4_HOURS    (TIME_ONE_HOUR_IN_SEC * 4)
#define TIME_OPEN_8_HOURS    (TIME_ONE_HOUR_IN_SEC * 8)
#define TIME_OPEN_12_HOURS   (TIME_ONE_HOUR_IN_SEC *12)
#define TIME_OPEN_18_HOURS   (0xFFFF) // Don't overflow the 16-bit value! (0xFFFF or 65535)
// Don't overflow the 16-bit value! This would overflow: (TIME_ONE_HOUR_IN_SEC *18)
// If the fudge factor is 4min 51sec, then
// ((60*60)+((4*60)+51)) = 3891;  65535/3891 = 16.84hrs
// So in reality, the 18HR unlock will unlock for 16hrs 50min.

//------------------------------------------------
//               TYPES AND ENUMS
//------------------------------------------------

// NOTE: Always add to the end!
//       Or edit a placeholder
typedef enum
{
    STKFUNC_UNDEFINED = 0,
    PLACEHOLDER_A,
    PLACEHOLDER_B,
    PLACEHOLDER_C,
    PLACEHOLDER_D,
    STKFUNC_REGULAR,
    STKFUNC_MASTER,
    STKFUNC_ADD_STICKER,
    STKFUNC_REMOVE_STICKER,
    STKFUNC_FACTORY_RESET,
    STKFUNC_BACKUP_STICKER,
    PLACEHOLDER_E,
    PLACEHOLDER_F,
    PLACEHOLDER_G,
    PLACEHOLDER_H,
    PLACEHOLDER_I,
    PLACEHOLDER_J,
    PLACEHOLDER_K,
    PLACEHOLDER_L,
    STKFUNC_OPEN_1_HOUR,
    STKFUNC_OPEN_2_HOURS,
    STKFUNC_OPEN_4_HOURS,
    STKFUNC_OPEN_8_HOURS,
    STKFUNC_OPEN_12_HOURS,
    STKFUNC_OPEN_18_HOURS,
    STKFUNC_OPEN_24_HOURS,
    STKFUNC_CLOSE_NOW,
    PLACEHOLDER_M,
    PLACEHOLDER_N,
    PLACEHOLDER_O,
    PLACEHOLDER_P,
    PLACEHOLDER_Q,
    PLACEHOLDER_R,
    PLACEHOLDER_S,
    PLACEHOLDER_T,
    PLACEHOLDER_U,
    PLACEHOLDER_V,
    PLACEHOLDER_W,
    PLACEHOLDER_X,
    PLACEHOLDER_Y,
    STKFUNC_GET_BATTERY_LIFE,
    STKFUNC_ONE_TIME_ACCESS,
    STKFUNC_CHANGE_TO_PARANOID_MODE_DEFAULT,
    STKFUNC_CHANGE_TO_ALWAYS_OPEN_MODE,
    STKFUNC_GET_ACCESS_LOGS,
    STKFUNC_GET_DEBUG_INFO,
    STKFUNC_VERIFY_BACKUP_STICKER,
    // Don't do this, we don't want production firmware to be able to write to
    // stickers!   STKFUNC_CREATE_BACKUP_STICKER
    STKFUNC_COPY_CONFIG,
    STKFUNC_PASTE_CONFIG,
    STKFUNC_DO_TESTING_FUNCTION,
    STKFUNC_SET_UNLOCK_TIME_10SEC,
    STKFUNC_SET_UNLOCK_TIME_20SEC,
    STKFUNC_CONFIG_PAYLOAD,
    STKFUNC_HWTUNE_MORE_CAP_SENSITIVITY, // Intended for factory use only
    STKFUNC_HWTUNE_LESS_CAP_SENSITIVITY, // Intended for factory use only
    STKFUNC_HWTUNE_GET_CAP_SENSITIVITY,  // Intended for factory use only
} stk_function;
// NOTE: Always add to the end!
//       Or edit a placeholder
ct_assert(sizeof(stk_function)==1);

typedef enum
{
    STK_LOCK_UNDEFINED = 0,
    STK_LOCK_OPEN,
    STK_LOCK_CLOSED,
    STK_LOCK_EXTENDED_OPEN,
} stk_lockState; // Operational Mode

typedef enum
{
    STK_BUZZER_SUCCESS = 0,
    STK_BUZZER_ALT_SUCCESS,
    STK_BUZZER_ALT2_SUCCESS,
    STK_BUZZER_FAILURE,
} stk_buzzer;

#define STK_DAT_ALWAYS_LEN  (4)
typedef struct __attribute__((__packed__))
{
    uint8_t  version;
    stk_function function;
    uint16_t crc; // To verify this is "our" data format

    // CRCs aren't strictly necessary for data integrity because ISO15693 has
    // 16bit CRCs built into the (RF) spec.  Meaning both writes and reads
    // won't return success unless the CRC is valid.  This is built into the
    // RFAL read/write functions.
    //
    // The CRC above is dual purpose:
    //     - Verify it is "our" data format (I've seen stickers with existing
    //       data).
    //     - Regular CRC function: verify the data for our variable-length data
    //       scheme
    //
    // It is in the first 32bit section because 99% of sticker reads only
    // read the first 32bits.  Why waste time reading more data if not
    // necessary?  Optimize the 99% use case.

} stk_dat_always; // Because we always read this on every sticker read
ct_assert(sizeof(stk_dat_always)==STK_DAT_ALWAYS_LEN);

ct_assert(RFAL_NFCV_UID_LEN==8);

typedef struct __attribute__((__packed__))
{
    stk_dat_always always; // Because we always read this on every sticker read
    union {
        struct {
            uint8_t  UID[RFAL_NFCV_UID_LEN];
            uint8_t  meta1_truST25;
            uint8_t  meta_RFU2;
            uint8_t  meta_RFU3;
            uint8_t  meta_RFU4;
        } fmat; // Formatted, i.e. "structured" data
        uint8_t  data[28];  // Raw data
    } payload;
} stk_data;

#define STK_ONSTICK_DATA_LEN  (32)
ct_assert(sizeof(stk_data)==STK_ONSTICK_DATA_LEN);

// This is the size of data on backup stickers: 4 + 8 + 4 = 16
#define STK_BACKUP_LENV1  (STK_DAT_ALWAYS_LEN + RFAL_NFCV_UID_LEN)
#define STK_BACKUP_LENV2  (STK_DAT_ALWAYS_LEN + RFAL_NFCV_UID_LEN + NFCV_BLOCK_LEN)
ct_assert(STK_BACKUP_LENV2==16);

typedef struct __attribute__((__packed__))
{
    stk_function function;
    const char  *name;
    uint8_t      size; // A multiple of NFCV_BLOCK_LEN
} stk_data_size;

typedef enum
{
    PROGRAM_DEFAULT = 0,
    PROGRAM_WRITE,
    PROGRAM_READ_NORMAL,
    PROGRAM_READ_ALL,
    PROGRAM_VERIFY_BACKUP,
} stickerProgrammerMode;

typedef struct {
    stickerProgrammerMode mode;
    stk_function          func;
    uint64_t lastStickReadUID; // For programming backup stickers
    bool lastStickReadTruST25; // For programming backup stickers
} stk_prog;

typedef struct {
    bool devHandled;
    bool devSuccessful;
} stk_cur_dev;

typedef enum
{
    LED_UNDEFINED = 0,
    LED_BLINK_RED,
    PAUSE_LED_BLINK_RED,
    LED_BLINK_GREEN,
    LED_BLINK_BLUE,
    LED_ERROR_RED,
    LED_RGB_RGB_RGB,
    LED_GGGGGGG, // Battery life high
    LED_G_G_G_G, // Battery life good
    LED_RRRRRRR, // Battery life low
    LED_R_R_R_R, // Battery life *really* low
    LED_B_B_B_B,
} ledSequence;

//
// Notes on adding meta-data to each UID:
//     Current max entries:
//         3992/8 = 499
//         499 - 8 = 491 actual UID slots
//
//     Future max entries if we go from 64bit(8bytes) to 96bit(12bytes)
//     (i.e. from 2 32-bit emulated EEPROM slots to 3 32-bit slots):
//         4000 - 12 = 3988 bytes avail See EMULATED_EEPROM_NO_INDEX_ZERO
//
//         332 slots * 12 bytes = 3984 bytes
//
//         332 - 8 (reserved) = 324 actual UID slots
//
// ---------------------------------------------------------
// ---------------------------------------------------------
#define STK_ENTRY_META1_ISTRUST25        (0x01U)
#define STK_ENTRY_META1_ISMASTER         (0x02U)
#define STK_ENTRY_META1_RFU3             (0x04U) // Reserved for future use
#define STK_ENTRY_META1_RFU4             (0x08U) // Reserved for future use
#define STK_ENTRY_META1_RFU5             (0x10U) // Reserved for future use
#define STK_ENTRY_META1_RFU6             (0x20U) // Reserved for future use
#define STK_ENTRY_META1_RFU7             (0x40U) // Reserved for future use
#define STK_ENTRY_META1_RFU8             (0x80U) // Reserved for future use

#define STK_ENTRY_META1_ISTRUST25_shift  (0U)
#define STK_ENTRY_META1_ISMASTER_shift   (1U)
#define STK_ENTRY_META1_RFU3_shift       (2U)
#define STK_ENTRY_META1_RFU4_shift       (3U)
#define STK_ENTRY_META1_RFU5_shift       (4U)
#define STK_ENTRY_META1_RFU6_shift       (5U)
#define STK_ENTRY_META1_RFU7_shift       (6U)
#define STK_ENTRY_META1_RFU8_shift       (7U)

bool stk_ent_ISTRUST25(stk_dbEntry *ent) {
    if ( (ent->meta1_truST25_mast & STK_ENTRY_META1_ISTRUST25) == 0) {
        return false;
    } else {
        return true;
    }
}
bool stk_ent_ISMASTER(stk_dbEntry *ent) {
    if ( (ent->meta1_truST25_mast & STK_ENTRY_META1_ISMASTER) == 0) {
        return false;
    } else {
        return true;
    }
}
// ---------------------------------------------------------
// ---------------------------------------------------------

#define STK_OPMODE_MODE_OPEN_10SEC       (0x01U)
#define STK_OPMODE_MODE_OPEN_20SEC       (0x02U)
#define STK_OPMODE_MODE_RFU3             (0x04U) // Reserved for future use
#define STK_OPMODE_MODE_RFU4             (0x08U) // Reserved for future use
#define STK_OPMODE_MODE_RFU5             (0x10U) // Reserved for future use
#define STK_OPMODE_MODE_RFU6             (0x20U) // Reserved for future use
#define STK_OPMODE_MODE_WALL_POWER       (0x40U)
#define STK_OPMODE_MODE_RFU8             (0x80U) // Reserved for future use

#define STK_DB_ENTRY_SIZE  (12)
ct_assert(sizeof(stk_dbEntry)==STK_DB_ENTRY_SIZE);

typedef struct __attribute__((__packed__))
{
    uint8_t version;

    // This applies to the entire on-flash structure only
    // during Copy_Config, Paste_Config, and Config_Payload:
    uint16_t numBlks; // Number of valid STK_DB_ENTRY_SIZE blocks
    uint16_t crc;

    uint8_t mode;
    uint8_t numWatchdogs;
    uint8_t numRFfrozen; // The RF chip appears non-responsive (frozen)
    uint8_t hwtuneCap : 4;  // Hardware tune capacitive sensitivity
    uint8_t hwtuneRFU : 4;  // Hardware tune RFU
    uint8_t op06;
    uint8_t op07;
    uint8_t op08;
} stk_opMode;
ct_assert(sizeof(stk_opMode)==STK_DB_ENTRY_SIZE);

typedef struct __attribute__((__packed__))
{
    uint8_t res01;
    uint8_t res02;
    uint8_t res03;
    uint8_t res04;
    uint8_t res05;
    uint8_t res06;
    uint8_t res07;
    uint8_t res08;
    uint8_t res09;
    uint8_t res10;
    uint8_t res11;
    uint8_t res12;
} stk_RFU; // Reserved for Future Use
ct_assert(sizeof(stk_RFU)==STK_DB_ENTRY_SIZE);

#define STK_ONFLASH_ENTRIES  (324)
typedef struct __attribute__((__packed__))
{
    stk_opMode  op_mode;    // Operational mode
    stk_dbEntry master1;
    stk_RFU     reserved1;
    stk_RFU     reserved2;
    stk_RFU     reserved3;
    stk_RFU     reserved4;
    stk_RFU     reserved5;
    stk_RFU     reserved6;
    stk_dbEntry entries[STK_ONFLASH_ENTRIES];
} stk_onflash_data;
#define STK_ONFLASH_DATA_LEN  (3984) // See EMULATED_EEPROM_NO_INDEX_ZERO
ct_assert(sizeof(stk_onflash_data)==STK_ONFLASH_DATA_LEN);
#define STK_ONFLASH_NUM_MEMBERS    (STK_ONFLASH_DATA_LEN / STK_DB_ENTRY_SIZE) // Number of 96bit members
ct_assert(STK_ONFLASH_NUM_MEMBERS==332);

typedef enum
{
    IDX_op_mode    = 0,
    IDX_master1    = 1,
    IDX_reserved1  = 2,
    IDX_reserved2  = 3,
    IDX_reserved3  = 4,
    IDX_reserved4  = 5,
    IDX_reserved5  = 6,
    IDX_reserved6  = 7,
    IDX_uids       = 8,
    IDX_LAST_ENTRY = 9, // Keep last
} stk_onflash_idx;


// This represents all data on a STKFUNC_CONFIG_PAYLOAD sticker.
//     - Write and read the cp_ofd member.
//     - Only read the cp_dat member (to avoid the possibility of overwriting
//       function stickers!).
typedef struct __attribute__((__packed__))
{
                             // NOTE: cp_dat should only be written in the factory!
    stk_data cp_dat;         // Normal data on every function sticker (member name: config payload dat)
    stk_onflash_data cp_ofd; // Config payload onflash data
} stk_onstick_config_payload;
ct_assert(sizeof(stk_onstick_config_payload)==(STK_ONSTICK_DATA_LEN+STK_ONFLASH_DATA_LEN));
//------------------------------------------------
//           end TYPES AND ENUMS
//------------------------------------------------



//------------------------------------------------
//                   GLOBALS
//------------------------------------------------
const stk_data_size gDataSizeArray[] = {
    { STKFUNC_UNDEFINED,
     "STKFUNC_UNDEFINED",                        STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_A,
     "PLACEHOLDER_A",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_B,
     "PLACEHOLDER_B",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_C,
     "PLACEHOLDER_C",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_D,
     "PLACEHOLDER_D",                            STK_DAT_ALWAYS_LEN },
    { STKFUNC_REGULAR,
     "STKFUNC_REGULAR",                          STK_DAT_ALWAYS_LEN },
    { STKFUNC_MASTER,
     "STKFUNC_MASTER",                           STK_DAT_ALWAYS_LEN },
    { STKFUNC_ADD_STICKER,
     "STKFUNC_ADD_STICKER",                      STK_DAT_ALWAYS_LEN },
    { STKFUNC_REMOVE_STICKER,
     "STKFUNC_REMOVE_STICKER",                   STK_DAT_ALWAYS_LEN },
    { STKFUNC_FACTORY_RESET,
     "STKFUNC_FACTORY_RESET",                    STK_DAT_ALWAYS_LEN },
    { STKFUNC_BACKUP_STICKER,
     "STKFUNC_BACKUP_STICKER",                   STK_BACKUP_LENV2 },
    { PLACEHOLDER_E,
     "PLACEHOLDER_E",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_F,
     "PLACEHOLDER_F",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_G,
     "PLACEHOLDER_G",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_H,
     "PLACEHOLDER_H",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_I,
     "PLACEHOLDER_I",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_J,
     "PLACEHOLDER_J",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_K,
     "PLACEHOLDER_K",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_L,
     "PLACEHOLDER_L",                            STK_DAT_ALWAYS_LEN },
    { STKFUNC_OPEN_1_HOUR,
     "STKFUNC_OPEN_1_HOUR",                      STK_DAT_ALWAYS_LEN },
    { STKFUNC_OPEN_2_HOURS,
     "STKFUNC_OPEN_2_HOURS",                     STK_DAT_ALWAYS_LEN },
    { STKFUNC_OPEN_4_HOURS,
     "STKFUNC_OPEN_4_HOURS",                     STK_DAT_ALWAYS_LEN },
    { STKFUNC_OPEN_8_HOURS,
     "STKFUNC_OPEN_8_HOURS",                     STK_DAT_ALWAYS_LEN },
    { STKFUNC_OPEN_12_HOURS,
     "STKFUNC_OPEN_12_HOURS",                    STK_DAT_ALWAYS_LEN },
    { STKFUNC_OPEN_18_HOURS,
     "STKFUNC_OPEN_18_HOURS",                    STK_DAT_ALWAYS_LEN },
    { STKFUNC_OPEN_24_HOURS,
     "STKFUNC_OPEN_24_HOURS",                    STK_DAT_ALWAYS_LEN },
    { STKFUNC_CLOSE_NOW,
     "STKFUNC_CLOSE_NOW",                        STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_M,
     "PLACEHOLDER_M",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_N,
     "PLACEHOLDER_N",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_O,
     "PLACEHOLDER_O",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_P,
     "PLACEHOLDER_P",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_Q,
     "PLACEHOLDER_Q",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_R,
     "PLACEHOLDER_R",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_S,
     "PLACEHOLDER_S",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_T,
     "PLACEHOLDER_T",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_U,
     "PLACEHOLDER_U",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_V,
     "PLACEHOLDER_V",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_W,
     "PLACEHOLDER_W",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_X,
     "PLACEHOLDER_X",                            STK_DAT_ALWAYS_LEN },
    { PLACEHOLDER_Y,
     "PLACEHOLDER_Y",                            STK_DAT_ALWAYS_LEN },
    { STKFUNC_GET_BATTERY_LIFE,
     "STKFUNC_GET_BATTERY_LIFE",                 STK_DAT_ALWAYS_LEN },
    { STKFUNC_ONE_TIME_ACCESS,
     "STKFUNC_ONE_TIME_ACCESS",                  STK_DAT_ALWAYS_LEN },
    { STKFUNC_CHANGE_TO_PARANOID_MODE_DEFAULT,
     "STKFUNC_CHANGE_TO_PARANOID_MODE_DEFAULT",  STK_DAT_ALWAYS_LEN },
    { STKFUNC_CHANGE_TO_ALWAYS_OPEN_MODE,
     "STKFUNC_CHANGE_TO_ALWAYS_OPEN_MODE",       STK_DAT_ALWAYS_LEN },
    { STKFUNC_GET_ACCESS_LOGS,
     "STKFUNC_GET_ACCESS_LOGS",                  STK_DAT_ALWAYS_LEN },
    { STKFUNC_GET_DEBUG_INFO,
     "STKFUNC_GET_DEBUG_INFO",                   STK_DAT_ALWAYS_LEN },
    { STKFUNC_VERIFY_BACKUP_STICKER,
     "STKFUNC_VERIFY_BACKUP_STICKER",            STK_DAT_ALWAYS_LEN },
    { STKFUNC_COPY_CONFIG,
     "STKFUNC_COPY_CONFIG",                      STK_DAT_ALWAYS_LEN },
    { STKFUNC_PASTE_CONFIG,
     "STKFUNC_PASTE_CONFIG",                     STK_DAT_ALWAYS_LEN },
    { STKFUNC_DO_TESTING_FUNCTION,
     "STKFUNC_DO_TESTING_FUNCTION",              STK_DAT_ALWAYS_LEN },
    { STKFUNC_SET_UNLOCK_TIME_10SEC,
     "STKFUNC_SET_UNLOCK_TIME_10SEC",            STK_DAT_ALWAYS_LEN },
    { STKFUNC_SET_UNLOCK_TIME_20SEC,
     "STKFUNC_SET_UNLOCK_TIME_20SEC",            STK_DAT_ALWAYS_LEN },
    { STKFUNC_CONFIG_PAYLOAD,
     "STKFUNC_CONFIG_PAYLOAD",                   STK_DAT_ALWAYS_LEN },
    { STKFUNC_HWTUNE_MORE_CAP_SENSITIVITY,
     "STKFUNC_HWTUNE_MORE_CAP_SENSITIVITY",      STK_DAT_ALWAYS_LEN },
    { STKFUNC_HWTUNE_LESS_CAP_SENSITIVITY,
     "STKFUNC_HWTUNE_LESS_CAP_SENSITIVITY",      STK_DAT_ALWAYS_LEN },
    { STKFUNC_HWTUNE_GET_CAP_SENSITIVITY,
     "STKFUNC_HWTUNE_GET_CAP_SENSITIVITY",       STK_DAT_ALWAYS_LEN },
};
#define DAT_ARRAY_NUM_ELEMS  (56)
ct_assert(sizeof(gDataSizeArray)/sizeof(stk_data_size)==DAT_ARRAY_NUM_ELEMS);

// start - These use a *lot* of our RAM!
stk_onflash_data gStkRamFlash;
stk_onstick_config_payload gStkConfigPayload;
// end   - These use a *lot* of our RAM!

stk_dbEntry *gSRF = (stk_dbEntry *)&gStkRamFlash;
//------------------------------------------------
//               end GLOBALS
//------------------------------------------------



//------------------------------------------------
//            FORWARD DECLARATIONS
//------------------------------------------------
static bool stk_isInDB(       rfalNfcDevice *nfcDev, bool truST25);
static bool stk_bkupIsInDB   (rfalNfcDevice *nfcDev, stk_data *dat);
static bool stk_addSticker(   rfalNfcDevice *nfcDev, bool truST25, stk_data *dat);
static bool stk_removeSticker(rfalNfcDevice *nfcDev, stk_data *dat);
//------------------------------------------------
//        end FORWARD DECLARATIONS
//------------------------------------------------


//
// This securely checks if a sticker is in the DB accounting
// for the TruST25 validation rules.
//
static bool stk_isInDB(rfalNfcDevice *nfcDev, bool truST25)
{
    int i = 0;

    for (i = 0; i < STK_ONFLASH_ENTRIES; i++) {
        if ( (gStkRamFlash.entries[i].uid != 0) &&
              isTheSame(nfcDev, truST25, IDX_uids, i)
           )
        {
            platformLog("Found in slot [%d]\n", i);
            return true;
        }
    }

    return false;
}


//
// Backup Sticker is in DB
//
// This is not secure, it does not account for TruST25.  Only
// use this if master has already been presented (i.e. in the
// master state).  This only checks if a UID is in the DB.
//
static bool stk_bkupIsInDB(rfalNfcDevice *nfcDev, stk_data *dat)
{
    int i = 0;

    uint64_t luid = stk_nfcDev_or_backupStk(nfcDev, dat);
    if (luid == 0) {
        return false;
    }

    for (i = 0; i < STK_ONFLASH_ENTRIES; i++) {
        if ( (gStkRamFlash.entries[i].uid != 0) &&
             (gStkRamFlash.entries[i].uid == luid)
           )
        {
            platformLog("Found in slot [%d]\n", i);
            return true;
        }
    }

    return false;
}


static bool stk_addSticker(rfalNfcDevice *nfcDev, bool truST25, stk_data *dat)
{
    assert_param(nfcDev != NULL);
    assert_param(dat    != NULL);

    int i = 0;
    bool lisAMaster = false; // Local, is a master

    uint64_t luid = stk_nfcDev_or_backupStk(nfcDev, dat);
    if (luid == 0) {
        return false;
    }

    // See if its already in a slot
    for (i = 0; i < STK_ONFLASH_ENTRIES; i++) {
        if (gStkRamFlash.entries[i].uid == luid) {
            platformLog("Already in slot [%d]\n", i);
            return true;
        }
    }

    // Already verified in stk_isValidSticker()
    if (dat->always.function == STKFUNC_MASTER) {
        lisAMaster = true;
    }

    // If its a backup sticker and the meta data says the associated user
    // sticker should have TruST25, then mark it as such.
    if ( (dat->always.function == STKFUNC_BACKUP_STICKER) &&
         (dat->always.version > STK_ONSTICK_DATA_VERSION1) &&
         (dat->payload.fmat.meta1_truST25 == 1) )
    {
        truST25 = true;
    }

    // Find a blank spot
    for (i = 0; i < STK_ONFLASH_ENTRIES; i++) {
        if (gStkRamFlash.entries[i].uid == 0) {
            stk_writeToRamFlash_uid(luid, truST25, lisAMaster, IDX_uids, i);
            platformLog("Added to slot [%d]\n", i);
            return true;
        }
    }

    return false;
}


static bool stk_removeSticker(rfalNfcDevice *nfcDev, stk_data *dat)
{
    assert_param(nfcDev != NULL);
    assert_param(dat    != NULL);

    int i = 0;

    // AVOID_CONFUSION_DO_NOTHING
    //
    // We should be able to return an error from this function
    // to indicate that we were not able to remove the sticker.
    // Instead of flashing red (which is what we do when a remove
    // is successful), lets instead not blink anything.
    //
    // This is to avoid confusion.  For example, if you blink some
    // sort of red code to indicate error, that is confusing because
    // the user is expecting to see red anyways since they are removing
    // a sticker.

    uint64_t luid = stk_nfcDev_or_backupStk(nfcDev, dat);
    if (luid == 0) {
        return false;
    }

    stk_dbEntry lZeroEnt;
    memset((uint8_t *)&lZeroEnt, 0, sizeof(stk_dbEntry));

    for (i = 0; i < STK_ONFLASH_ENTRIES; i++) {
        if ( (gStkRamFlash.entries[i].uid != 0) &&
             (gStkRamFlash.entries[i].uid == luid)
           )
        {
            // Write all 0's to the slot
            stk_writeToRamFlash_ent(&lZeroEnt, IDX_uids, i);
            platformLog("Removed from slot [%d]\n", i);
        }
    }

    return true;
}
