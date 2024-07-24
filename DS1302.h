#ifndef DS1302_H
#define DS1302_H

#include <stdbool.h>
#include <stdint.h>

// NOTE for SPI: nRST is chip select (active high), SPI is 2-wire, IO data is on CLK rising edge
// Low bits in front, high bits in back.
// At power–up, RST must be a logic 0 until VCC > 2.0 volts (performed in DS1302_Init())
// Also SCLK must be at a logic 0 when RST is driven to a logic 1 state.
#define DS1302_SPI_FREQ_MAX (500 * 1000) // 500kHz for VCC=2.0V, 2MHz for VCC=5V
#define DS1302_SPI_LENGTH_MAX (32) // 1 address/control byte + 31 RAM burst registers

enum DS1302_12_24_Mode {
    DS1302_24 = 0,
    DS1302_12 = 1,
};

enum DS1302_AM_PM {
    DS1302_AM = 0,
    DS1302_PM = 1,
};

struct DS1302_Platform {
    int (*gpioGet)(int pin);
    void (*gpioSet)(int pin, int state);

    int (*spiSend)(uint8_t *data, int len);
    int (*spiSendRecv)(uint8_t *tdata, int tlen, uint8_t *rdata, int rlen);

    void (*delayMs)(int ms);
    void (*delayUs)(int us);
    void (*debugPrint)(const char *fmt, ...);

    int pinNrst;
};

void DS1302_Init(struct DS1302_Platform *platform);

bool DS1302_ReadReg(int addr, int rc, uint8_t *data, int len);
bool DS1302_WriteReg(int addr, int rc, const uint8_t *data, int len);

bool DS1302_SetWriteProtect(bool writeProtect); // power-on state is not defined, must be explicitly set
bool DS1302_SetClockHalt(bool clockHalt); // power-on state is not defined, must be explicitly set
bool DS1302_GetClockHalt(bool *clockHalt);

bool DS1302_GetClock(int *sec, int *mins, int *hour, int *date, int *month, int *year, int *wday);
bool DS1302_SetClock(int sec, int mins, int hour, int date, int month, int year, int wday);

bool DS1302_ReadRamReg(int reg, uint8_t *data);
bool DS1302_WriteRamReg(int reg, uint8_t data);

#endif // DS1302_H
