/*
 * os_eeprom.c — controller-pak / EEPROM save shim.
 * Fidelity tier: MINIMAL — backed by a 2 KB in-memory buffer. ExtData
 *                persistence deferred to the save-data milestone.
 */

#include <stdint.h>
#include <string.h>

#define EEPROM_TYPE_4K     1
#define EEPROM_TYPE_16K    2
#define EEPROM_MAXBLOCKS   128
#define EEPROM_BLOCK_SIZE  8

static uint8_t g_eeprom[2048] = {0};

int  osEepromProbe(void *mq) { (void)mq; return EEPROM_TYPE_16K; }

int osEepromLongRead(void *mq, uint8_t address, uint8_t *buf, int nbytes) {
    (void)mq;
    int byte_off = address * EEPROM_BLOCK_SIZE;
    if (byte_off + nbytes > (int)sizeof(g_eeprom)) return -1;
    memcpy(buf, g_eeprom + byte_off, (size_t)nbytes);
    return 0;
}

int osEepromLongWrite(void *mq, uint8_t address, uint8_t *buf, int nbytes) {
    (void)mq;
    int byte_off = address * EEPROM_BLOCK_SIZE;
    if (byte_off + nbytes > (int)sizeof(g_eeprom)) return -1;
    memcpy(g_eeprom + byte_off, buf, (size_t)nbytes);
    return 0;
}

int  osEepromRead (void *mq, uint8_t a, uint8_t *b) { return osEepromLongRead (mq, a, b, 8); }
int  osEepromWrite(void *mq, uint8_t a, uint8_t *b) { return osEepromLongWrite(mq, a, b, 8); }
