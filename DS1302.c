#include <string.h>
#include "DS1302.h"

#define REG_SEC_ADDR 0x40
#define REF_MIN_ADDR 0x41
#define REG_HR_ADDR 0x42
#define REG_DATE_ADDR 0x43
#define REG_MONTH_ADDR 0x44
#define REG_DAY_ADDR 0x45
#define REG_YEAR_ADDR 0x46
#define REG_CONTROL_ADDR 0x47
#define REG_TRICKLE_CHARGER_ADDR 0x48
#define REG_CLOCK_BURST_ADDR 0x5F

#define REG_RAM_0_ADDR 0x60 // RAM 0-30 (0x60-0x7E)
#define REG_RAM_BURST_ADDR 0x7F

#define AC_READ 1 // Bit 0
#define AC_WRITE 0 // Bit 0

#define RC_RAM 1 // Bit 6
#define RC_CLOCK 0 // Bit 6

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static struct DS1302_Platform platform;

static uint8_t spiBuf[DS1302_SPI_LENGTH_MAX];

static bool readReg(int addr, int rc, uint8_t *data, int len) {
    // Prepare address/control byte
    // NOTE: The MSB (Bit 7) must be a logic 1
    uint8_t ac = (1 << 7) | (rc << 6) | (addr << 1) | AC_READ;
    
    // Set nRST to start transfer
    platform.gpioSet(platform.pinNrst, 1);

    // Perform send/receive
    int ret = platform.spiSendRecv(&ac, 1, data, len);
    if (ret < 0) {
        platform.debugPrint("spiSendRecv for reg %x len %d failed: %d\r\n", addr, len, -ret);
        platform.gpioSet(platform.pinNrst, 0);
        return false;
    }

    // Reset nRST to stop transfer
    platform.gpioSet(platform.pinNrst, 0);

    return true;
}

static bool writeReg(int addr, int rc, const uint8_t *data, int len) {
    // Prepare address/control byte
    // NOTE: The MSB (Bit 7) must be a logic 0
    uint8_t ac = (1 << 7) | (rc << 6) | (addr << 1) | AC_WRITE;

    // Prepare buffer
    spiBuf[0] = ac;
    memcpy(&spiBuf[1], data, MIN(DS1302_SPI_LENGTH_MAX - 1, len));
    
    // Set nRST to start transfer
    platform.gpioSet(platform.pinNrst, 1);

    // Perform send
    int ret = platform.spiSend(spiBuf, len + 1);
    if (ret < 0) {
        platform.debugPrint("spiSend for reg %x len %d failed: %d\r\n", addr, len, -ret);
        platform.gpioSet(platform.pinNrst, 0);
        return false;
    }

    // Reset nRST to stop transfer
    platform.gpioSet(platform.pinNrst, 0);

    return true;
}

static bool setWriteProtect(bool writeProtect) {
    // Prepare control register
    uint8_t control = 0;
    if (writeProtect) {
        control |= 0x80;
    }

    // Write control register
    return writeReg(REG_CONTROL_ADDR, RC_CLOCK, &control, 1);
}

void DS1302_Init(struct DS1302_Platform *platformPtr) {
    platform = *platformPtr;
}

bool DS1302_SetClockHalt(bool clockHalt) {
    bool ok = true;

    // Bit 7 of the seconds register is defined as the clock halt flag
    uint8_t sec = 0;
    ok &= readReg(REG_SEC_ADDR, RC_CLOCK, &sec, 1);

    // Reset write protect
    ok &= setWriteProtect(false);

    // Update clock halt flag
    sec &= 0x7F;
    if (clockHalt) {
        sec |= 0x80;
    }

    // Write seconds register
    ok &= writeReg(REG_SEC_ADDR, RC_CLOCK, &sec, 1);

    // Set write protect
    ok &= setWriteProtect(true);

    return ok;
}

bool DS1302_GetClock(int *sec, int *min, int *hour, int *date, int *month, int *year, int *wday) {
    // Perform burst read of 8 clock registers
    uint8_t data[8];
    if (!readReg(REG_CLOCK_BURST_ADDR, RC_CLOCK, data, 8)) {
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
    *min = (minBcd >> 4) * 10 + (minBcd & 0x0F);

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

bool DS1302_SetClock(int sec, int min, int hour, int date, int month, int year, int wday) {
    // Reset write protect
    if (!setWriteProtect(false)) {
        return false;
    }

    // Prepare buffer for burst of 8 clock registers
    uint8_t data[8];
    data[0] = (sec / 10) << 4 | (sec % 10); // CH bit = 0, clock is on
    data[1] = (min / 10) << 4 | (min % 10);
    data[2] = (hour / 10) << 7 | (hour % 10) | ((int)DS1302_24 << 7); // 24h mode
    data[3] = (date / 10) << 4 | (date % 10);
    data[4] = (month / 10) << 4 | (month % 10);
    data[5] = (wday / 10) << 4 | (wday % 10);
    data[6] = ((year - 2000) / 10) << 4 | ((year - 2000) % 10);
    data[7] = 0; // WP bit = 0, no write protect

    // Write clock registers (burst)
    if (!writeReg(REG_CLOCK_BURST_ADDR, RC_CLOCK, data, 8)) {
        return false;
    }

    // Set write protect
    if (!setWriteProtect(true)) {
        return false;
    }

    return true;
}

bool DS1302_ReadRamReg(int reg, uint8_t *data) {
    // Read RAM register
    return readReg(REG_RAM_0_ADDR + reg, RC_RAM, data, 1);
}

bool DS1302_WriteRamReg(int reg, uint8_t data) {
    // Reset write protect
    if (!setWriteProtect(false)) {
        return false;
    }

    // Write RAM register
    if (!writeReg(REG_RAM_0_ADDR + reg, RC_RAM, &data, 1)) {
        return false;
    }

    // Set write protect
    if (!setWriteProtect(true)) {
        return false;
    }

    return true;
}