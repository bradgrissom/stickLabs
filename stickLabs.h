//------------------------------------------------
// Copyright (c) 2022 stickLabs.io  All rights reserved.
//------------------------------------------------

#ifndef __STK_UTILS_H__
#define __STK_UTILS_H__

#include "rfal_nfc.h"  // rfalNfcDevice


typedef struct __attribute__((__packed__))
{
    uint8_t meta1_truST25_mast;
    uint8_t metaRFU2;
    uint8_t metaRFU3;
    uint8_t metaRFU4;
    uint64_t uid;
} stk_dbEntry;

typedef enum {
    ST_NOT_INITIALIZED = 0,
    ST_SETUP_ASSIGN_MASTER,
    ST_NORMAL,
    ST_MASTER,
    ST_MASTER_CONFIG_ADD,
    ST_MASTER_CONFIG_REMOVE,
    ST_MASTER_COPY_CONFIG,
    ST_MASTER_PASTE_CONFIG,
    ST_VERIFY_BACKUP_W_BACK,  // Wait for backup sticker
    ST_VERIFY_BACKUP_W_STICK, // Wait for actual sticker
    ST_TRY_AGAIN,
} setup_state;

#endif // __STK_UTILS_H__
