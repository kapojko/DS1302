#include <string.h>
#include "DS1302.h"

#define REG_SEC_ADDR 0x0
#define REF_MIN_ADDR 0x1
#define REG_HR_ADDR 0x2
#define REG_DATE_ADDR 0x3
#define REG_MONTH_ADDR 0x4
#define REG_DAY_ADDR 0x5
#define REG_YEAR_ADDR 0x6
#define REG_CONTROL_ADDR 0x7
#define REG_TRICKLE_CHARGER_ADDR 0x8

#define REG_RAM_0_ADDR 0x0 // RAM 0-30

#define REG_BURST 0x1F

#define AC_READ 1 // Bit 0
#define AC_WRITE 0 // Bit 0

#define RC_RAM 1 // Bit 6
#define RC_CLOCK 0 // Bit 6

// #define DEBUG_PRINT_READ_WRITE

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static struct DS1302_Platform platform;

static uint8_t spiBuf[DS1302_SPI_LENGTH_MAX];

void DS1302_Init(struct DS1302_Platform *platformPtr) {
    platform = *platformPtr;
}

bool DS1302_ReadReg(int addr, int rc, uint8_t *data, int len) {
    // Prepare address/control byte
    // NOTE: The MSB (Bit 7) must be a logic 1
    uint8_t ac = (1 << 7) | (rc << 6) | (addr << 1) | AC_READ;
    
    // Set nRST to start transfer
    platform.gpioSet(platform.pinNrst, 1);

    // Delay not less than 1-4 us (tCC)
    platform.delayUs(4);

    // Perform send/receive
    int ret = platform.spiSendRecv(&ac, 1, data, len);
    if (ret < 0) {
        platform.debugPrint("spiSendRecv for reg %x len %d failed: %d\r\n", addr, len, -ret);
    }

    // Reset nRST to stop transfer
    platform.gpioSet(platform.pinNrst, 0);

    // Delay not less than 1-4 us (tCWH)
    platform.delayUs(4);

#ifdef DEBUG_PRINT_READ_WRITE
    if (len == 1) {
        platform.debugPrint("Read REG %x (rc=%d): %02x\r\n", addr, rc, data[0]);
    } else {
        platform.debugPrint("Read REG %x (rc=%d): ", addr, rc);
        for (int i = 0; i < len; i++) {
            platform.debugPrint("%02x ", data[i]);
        }
        platform.debugPrint("\r\n");
    }
#endif

    return (ret >= 0);
}

bool DS1302_WriteReg(int addr, int rc, const uint8_t *data, int len) {
    // Prepare address/control byte
    // NOTE: The MSB (Bit 7) must be a logic 0
    uint8_t ac = (1 << 7) | (rc << 6) | (addr << 1) | AC_WRITE;

    // Prepare buffer
    spiBuf[0] = ac;
    memcpy(&spiBuf[1], data, MIN(DS1302_SPI_LENGTH_MAX - 1, len));
    
    // Set nRST to start transfer
    platform.gpioSet(platform.pinNrst, 1);

    // Delay not less than 1-4 us (tCC)
    platform.delayUs(4);

    // Perform send
    int ret = platform.spiSend(spiBuf, len + 1);
    if (ret < 0) {
        platform.debugPrint("spiSend for reg %x len %d failed: %d\r\n", addr, len, -ret);
    }

    // Reset nRST to stop transfer
    platform.gpioSet(platform.pinNrst, 0);

    // Delay not less than 1-4 us (tCWH)
    platform.delayUs(4);

#ifdef DEBUG_PRINT_READ_WRITE
    if (len == 1) {
        platform.debugPrint("Write REG %x (rc=%d): %02x\r\n", addr, rc, data[0]);
    } else {
        platform.debugPrint("Write REG %x (rc=%d): ", addr, rc);
        for (int i = 0; i < len; i++) {
            platform.debugPrint("%02x ", data[i]);
        }
        platform.debugPrint("\r\n");
    }
#endif

    return (ret >= 0);
}

bool DS1302_SetWriteProtect(bool writeProtect) {
    // Prepare control register
    uint8_t control = 0;
    if (writeProtect) {
        control |= 0x80;
    }

    // Write control register
    return DS1302_WriteReg(REG_CONTROL_ADDR, RC_CLOCK, &control, 1);
}

bool DS1302_SetClockHalt(bool clockHalt) {
    bool ok = true;

    // Bit 7 of the seconds register is defined as the clock halt flag
    uint8_t sec = 0;
    ok &= DS1302_ReadReg(REG_SEC_ADDR, RC_CLOCK, &sec, 1);

    // Reset write protect
    ok &= DS1302_SetWriteProtect(false);

    // Update clock halt flag
    sec &= 0x7F;
    if (clockHalt) {
        sec |= 0x80;
    }

    // Write seconds register
    ok &= DS1302_WriteReg(REG_SEC_ADDR, RC_CLOCK, &sec, 1);

    // Set write protect
    ok &= DS1302_SetWriteProtect(true);

    return ok;
}

bool DS1302_GetClockHalt(bool *clockHalt) {
    // Read seconds register
    uint8_t sec = 0;
    if (!DS1302_ReadReg(REG_SEC_ADDR, RC_CLOCK, &sec, 1)) {
        return false;
    }

    // Parse clock halt flag
    *clockHalt = sec & 0x80;
    
    return true;
}

bool DS1302_GetClock(int *sec, int *mins, int *hour, int *date, int *month, int *year, int *wday) {
    // Perform burst read of 8 clock registers
    uint8_t data[8];
    if (!DS1302_ReadReg(REG_BURST, RC_CLOCK, data, 8)) {
        return false;
    }

    // Parse sec
    int ch = data[0] & 0x80; // Clock halt flag
    if (ch) {
        platform.debugPrint("Clock halted\r\n");
        return false;
    }

    int secBcd = data[0] & 0x7F;
    *sec = (secBcd >> 4) * 10 + (secBcd & 0x0F);

    // Parse min
    int minBcd = data[1] & 0x7F;
    *mins = (minBcd >> 4) * 10 + (minBcd & 0x0F);

    // Parse hour
    int hr12_24 = (data[2] >> 7) & 0x01;
    if (hr12_24 == DS1302_12) {
        int amPm = (data[2] >> 5) & 0x01;
        int hourBcd = data[2] & 0x1F;
        if (amPm == DS1302_AM) {
            *hour = (hourBcd >> 4) * 10 + (hourBcd & 0x0F);
        } else {
            *hour = (hourBcd >> 4) * 10 + (hourBcd & 0x0F) + 12;
        }
    } else {
        int hourBcd = data[2] & 0x3F;
        *hour = (hourBcd >> 4) * 10 + (hourBcd & 0x0F);
    }

    // Parse date
    int dateBcd = data[3] & 0x3F;
    *date = (dateBcd >> 4) * 10 + (dateBcd & 0x0F);

    // Parse month
    int monthBcd = data[4] & 0x1F;
    *month = (monthBcd >> 4) * 10 + (monthBcd & 0x0F);

    // Parse day
    int wdayBcd = data[5] & 0x07;
    *wday = (wdayBcd >> 4) * 10 + (wdayBcd & 0x0F);

    // Parse year
    int yearBcd = data[6] & 0x7F;
    *year = (yearBcd >> 4) * 10 + (yearBcd & 0x0F) + 2000;

    return true;
}

bool DS1302_SetClock(int sec, int mins, int hour, int date, int month, int year, int wday) {
    // Reset write protect
    if (!DS1302_SetWriteProtect(false)) {
        return false;
    }

    // Prepare buffer for burst of 8 clock registers
    uint8_t data[8];
    data[0] = (sec / 10) << 4 | (sec % 10); // CH bit = 0, clock is on
    data[1] = (mins / 10) << 4 | (mins % 10);
    data[2] = (hour / 10) << 4 | (hour % 10) | ((int)DS1302_24 << 7); // 24h mode
    data[3] = (date / 10) << 4 | (date % 10);
    data[4] = (month / 10) << 4 | (month % 10);
    data[5] = (wday / 10) << 4 | (wday % 10);
    data[6] = ((year - 2000) / 10) << 4 | ((year - 2000) % 10);
    data[7] = 0; // WP bit = 0, no write protect

    // Write clock registers (burst)
    if (!DS1302_WriteReg(REG_BURST, RC_CLOCK, data, 8)) {
        return false;
    }

    // Set write protect
    if (!DS1302_SetWriteProtect(true)) {
        return false;
    }

    return true;
}

bool DS1302_ReadRamReg(int reg, uint8_t *data) {
    // Read RAM register
    return DS1302_ReadReg(REG_RAM_0_ADDR + reg, RC_RAM, data, 1);
}

bool DS1302_WriteRamReg(int reg, uint8_t data) {
    // Reset write protect
    if (!DS1302_SetWriteProtect(false)) {
        return false;
    }

    // Write RAM register
    if (!DS1302_WriteReg(REG_RAM_0_ADDR + reg, RC_RAM, &data, 1)) {
        return false;
    }

    // Set write protect
    if (!DS1302_SetWriteProtect(true)) {
        return false;
    }

    return true;
}