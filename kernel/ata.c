#include "ata.h"
#include "ports.h"

// ATA PIO Ports (Primary Bus)
#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECT_COUNT 0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7

// Status bits
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

// Commands
#define ATA_CMD_READ  0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_FLUSH 0xE7
#define ATA_CMD_IDENT 0xEC

// With no drive attached the bus floats and the status port reads 0xFF
// (BSY set forever), so every wait must have a timeout and a 0xFF check.
#define ATA_TIMEOUT 100000

static int ata_wait_bsy() {
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = port_byte_in(ATA_STATUS);
        if (status == 0xFF) return -1; // Floating bus: no drive
        if (!(status & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq() {
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = port_byte_in(ATA_STATUS);
        if (status == 0xFF || (status & ATA_SR_ERR)) return -1;
        if (status & ATA_SR_DRQ) return 0;
    }
    return -1;
}

int ata_write_sector(uint32_t lba, const uint8_t* buffer) {
    if (ata_wait_bsy() != 0) return -1;

    port_byte_out(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    port_byte_out(ATA_SECT_COUNT, 1);
    port_byte_out(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    port_byte_out(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    port_byte_out(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    port_byte_out(ATA_COMMAND, ATA_CMD_WRITE);

    if (ata_wait_drq() != 0) return -1;

    const uint16_t* buf16 = (const uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        port_word_out(ATA_DATA, buf16[i]);
    }

    if (ata_wait_bsy() != 0) return -1;
    if (port_byte_in(ATA_STATUS) & ATA_SR_ERR) return -1;

    // Flush the drive's write cache so data actually hits the medium
    port_byte_out(ATA_COMMAND, ATA_CMD_FLUSH);
    if (ata_wait_bsy() != 0) return -1;

    return 0;
}

int ata_identify() {
    // Select master drive
    port_byte_out(ATA_DRIVE_HEAD, 0xA0);
    port_byte_out(ATA_SECT_COUNT, 0);
    port_byte_out(ATA_LBA_LO, 0);
    port_byte_out(ATA_LBA_MID, 0);
    port_byte_out(ATA_LBA_HI, 0);
    port_byte_out(ATA_COMMAND, ATA_CMD_IDENT);

    uint8_t status = port_byte_in(ATA_STATUS);
    if (status == 0 || status == 0xFF) return 0; // No disk

    if (ata_wait_bsy() != 0) return 0;

    // Check if LBA_MID or LBA_HI are non-zero (not ATA)
    if (port_byte_in(ATA_LBA_MID) != 0 || port_byte_in(ATA_LBA_HI) != 0) {
        return 0; // Not ATA
    }

    if (ata_wait_drq() != 0) return 0;

    // Read and discard 256 words of identify data
    for (int i = 0; i < 256; i++) {
        port_word_in(ATA_DATA);
    }

    return 1; // Disk found
}

int ata_read_sector(uint32_t lba, uint8_t* buffer) {
    if (ata_wait_bsy() != 0) return -1;

    // Select drive + LBA mode + top 4 bits of LBA
    port_byte_out(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    port_byte_out(ATA_SECT_COUNT, 1); // Read 1 sector
    port_byte_out(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    port_byte_out(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    port_byte_out(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    port_byte_out(ATA_COMMAND, ATA_CMD_READ);

    if (ata_wait_bsy() != 0) return -1;

    uint8_t status = port_byte_in(ATA_STATUS);
    if (status & ATA_SR_ERR) return -1;

    if (ata_wait_drq() != 0) return -1;

    // Read 256 words (512 bytes) from data port
    uint16_t* buf16 = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        buf16[i] = port_word_in(ATA_DATA);
    }

    return 0;
}
