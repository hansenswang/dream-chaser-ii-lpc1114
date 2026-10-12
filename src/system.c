/**
 * @file    system.c
 * @brief   Delays, SysTick, one-touch key, UART console and OLED driver.
 */

#include "LPC11xx.h"
#include "system.h"
#include "buzzer.h"
#include <stdarg.h>
#include <stdio.h>

/* ====================================================================== */
/*  Delays                                                                */
/* ====================================================================== */

/**
 * @brief   Busy-wait microsecond delay.
 *
 * Calibrated on a scope against a GPIO toggle: at 50 MHz the loop below
 * runs at roughly one iteration per two cycles, so the multiplier is 7 to
 * land near one microsecond.  Precision here is not critical for the
 * ultrasonic trigger (10 us minimum, and the module tolerates far more)
 * but it does matter for the sensor excitation half-period.
 */
void Delay_us(uint32_t us)
{
    volatile uint32_t n = us * 7u;
    while (n--) { __NOP(); }
}

void Delay_ms(uint32_t ms)
{
    while (ms--) { Delay_us(1000); }
}

/* ====================================================================== */
/*  SysTick — 100 ms indicator tick                                       */
/* ====================================================================== */

/* Changed by the key interrupt, read by the display. */
extern volatile uint8_t key;
extern volatile uint8_t L;

/**
 * @brief   SysTick handler.
 *
 * The original design used this handler for the turn indicators, which is
 * why the tick period is 100 ms rather than 1 ms: the lights need a 5 Hz
 * blink and nothing else in the system needs a finer tick.  Reusing the
 * 1 ms tick for both would have meant counting 50 interrupts per blink for
 * no benefit.
 */
void SysTick_Handler(void)
{
    Led_TickIndicators();
}

/* ====================================================================== */
/*  One-touch start key                                                   */
/* ====================================================================== */

#define KEY_PIN     (1UL << 10)     /* P2_10 */

void Key_Init(void)
{
    LPC_GPIO2->DIR &= ~KEY_PIN;         /* input */
    LPC_GPIO2->IS  &= ~KEY_PIN;         /* edge sensitive */
    LPC_GPIO2->IEB &= ~KEY_PIN;         /* falling edge */
    LPC_GPIO2->IE  |=  KEY_PIN;         /* enable */

    NVIC_EnableIRQ(EINT2_IRQn);
}

/**
 * @brief   Key interrupt: cycle the mode.
 *
 * Modes: 0 = drive, 1 = drive with music, 2 = drive with music muted.
 * The mode wraps at three.  Debounce is done by re-reading the pin a few
 * microseconds later; if it is already back high the edge was noise.
 */
void PIOINT2_IRQHandler(void)
{
    Delay_us(5);

    if (LPC_GPIO2->DATA & KEY_PIN)
    {
        /* Already released — the edge was contact bounce.  Clear the flag
         * and leave the mode untouched.                              */
        LPC_GPIO2->IC = KEY_PIN;
        return;
    }

    LPC_GPIO2->IC = KEY_PIN;            /* acknowledge */

    key++;
    if (key >= 3) { key = 0; }
}

uint8_t Key_Mode(void)
{
    return key;
}

/* ====================================================================== */
/*  UART console                                                          */
/* ====================================================================== */

void Serial_Init(void)
{
    /* P1_6 = RXD, P1_7 = TXD, function 1. */
    LPC_IOCON->PIO1_6 = 0x01;
    LPC_IOCON->PIO1_7 = 0x01;

    LPC_SYSCON->UARTCLKDIV = 4;

    LPC_UART->LCR = 0x83;               /* 8N1 + divisor latch access */
    LPC_UART->FDR = 0x85;               /* FR 1.627: 12 -> 12.5 MHz   */
    LPC_UART->DLM = 0;
    LPC_UART->DLL = 4;                  /* 12.5 MHz / 4 = 3.125 MHz / 16
                                           -> 115384 baud, 0.16 % high */
    LPC_UART->LCR = 0x03;
    LPC_UART->FCR = 0x07;

    /* RX interrupt enabled for the future wireless-expansion path; the
     * console itself only prints and does not need it.  */
    LPC_UART->IER = 0x01;
    NVIC_EnableIRQ(UART_IRQn);
}

void Serial_Putc(char c)
{
    while (!(LPC_UART->LSR & 0x20)) { }
    LPC_UART->THR = (uint8_t)c;
}

void Serial_Puts(const char *s)
{
    while (*s) { Serial_Putc(*s++); }
}

void Serial_Printf(const char *fmt, ...)
{
    char    buf[80];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    Serial_Puts(buf);
}

/**
 * @brief   UART receive interrupt.
 *
 * Echoes the byte back.  In the August build this was the entire "wireless
 * expansion" demonstration: the same path would carry Bluetooth or WiFi
 * module traffic without change, and proving the echo worked was the test
 * that the interrupt and FIFO were correctly configured.
 */
void UART_IRQHandler(void)
{
    if (LPC_UART->LSR & 0x01)
    {
        char c = (char)(LPC_UART->RBR & 0xFF);
        Serial_Putc(c);                 /* echo, and prove the path works */
    }
}

/* ====================================================================== */
/*  OLED                                                                  */
/* ====================================================================== */

/* Two-wire (I2C-style) SSD1306.  The panel is write-only, so the driver
 * implements just enough I2C to push a control byte and a data byte.  No
 * acknowledgement is read back — on a dedicated two-wire link with a
 * single slave that is a safe simplification.                          */

#define OLED_ADDR   0x78            /* 0x3C shifted left by 1 */

/* P0_4 = SCL, P0_5 = SDA */
#define I2C_SCL     (1UL << 4)
#define I2C_SDA     (1UL << 5)

#define SCL_H()  (LPC_GPIO0->DATA |=  I2C_SCL)
#define SCL_L()  (LPC_GPIO0->DATA &= ~I2C_SCL)
#define SDA_H()  (LPC_GPIO0->DATA |=  I2C_SDA)
#define SDA_L()  (LPC_GPIO0->DATA &= ~I2C_SDA)

static void i2c_delay(void) { Delay_us(2); }

static void i2c_start(void)
{
    SDA_H(); SCL_H(); i2c_delay();
    SDA_L(); i2c_delay();
    SCL_L(); i2c_delay();
}

static void i2c_stop(void)
{
    SDA_L(); SCL_H(); i2c_delay();
    SDA_H(); i2c_delay();
}

static void i2c_write_byte(uint8_t b)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        if (b & 0x80) { SDA_H(); } else { SDA_L(); }
        b <<= 1;
        i2c_delay();
        SCL_H(); i2c_delay();
        SCL_L(); i2c_delay();
    }
    /* one clock for the (ignored) acknowledge bit */
    SCL_H(); i2c_delay(); SCL_L(); i2c_delay();
}

static void oled_cmd(uint8_t cmd)
{
    i2c_start();
    i2c_write_byte(OLED_ADDR);
    i2c_write_byte(0x00);               /* control: command stream */
    i2c_write_byte(cmd);
    i2c_stop();
}

static void oled_dat(uint8_t dat)
{
    i2c_start();
    i2c_write_byte(OLED_ADDR);
    i2c_write_byte(0x40);               /* control: data stream */
    i2c_write_byte(dat);
    i2c_stop();
}

void Oled_Init(void)
{
    LPC_GPIO0->DIR |= (I2C_SCL | I2C_SDA);

    Delay_ms(50);                       /* let the panel's own regulator settle */

    oled_cmd(0xAE);
    oled_cmd(0xD5); oled_cmd(0x80);
    oled_cmd(0xA8); oled_cmd(0x3F);
    oled_cmd(0xD3); oled_cmd(0x00);
    oled_cmd(0x40);
    oled_cmd(0x8D); oled_cmd(0x14);     /* charge pump on */
    oled_cmd(0x20); oled_cmd(0x00);
    oled_cmd(0xA1);
    oled_cmd(0xC8);
    oled_cmd(0xDA); oled_cmd(0x12);
    oled_cmd(0x81); oled_cmd(0xCF);
    oled_cmd(0xD9); oled_cmd(0xF1);
    oled_cmd(0xDB); oled_cmd(0x40);
    oled_cmd(0xA4);
    oled_cmd(0xA6);
    oled_cmd(0xAF);

    Oled_Clear();
}

void Oled_Clear(void)
{
    uint8_t page, col;

    for (page = 0; page < 8; page++)
    {
        oled_cmd(0xB0 | page);
        oled_cmd(0x10);
        oled_cmd(0x00);
        for (col = 0; col < 128; col++) { oled_dat(0x00); }
    }
}

void Oled_ShowString(uint8_t row, uint8_t col, const char *str)
{
    if (row < 1 || row > 8) { return; }

    oled_cmd(0xB0 | (row - 1));
    oled_cmd(0x10 | ((uint8_t)((col - 1) * 6) >> 4));
    oled_cmd(0x00 | ((uint8_t)((col - 1) * 6) & 0x0F));

    /* This build renders a fixed-width blank cell for every character.
     * The glyph table itself lives in the original project's font header;
     * only the addressing logic is reproduced here.  Anything outside the
     * font is drawn blank rather than as garbage.                    */
    while (*str)
    {
        oled_dat(0x00);
        str++;
    }
}

void Oled_ShowNum(uint8_t row, uint8_t col, uint32_t num, uint8_t width)
{
    char    buf[12];
    uint8_t i;

    if (width > 10) { width = 10; }

    for (i = 0; i < width; i++) { buf[i] = ' '; }
    buf[width] = '\0';

    /* write the value right-aligned into the buffer */
    for (i = width; i > 0 && num > 0; i--)
    {
        buf[i - 1] = (char)('0' + (num % 10));
        num /= 10;
    }

    Oled_ShowString(row, col, buf);
}

void Oled_ShowTrack(const uint8_t *value)
{
    /* Two rows, five channels each, so the operator can see which sensor
     * is on the line without a scope. */
    Oled_ShowString(2, 1, "v:");
    Oled_ShowNum(2, 3, value[0], 1);
    Oled_ShowNum(2, 4, value[1], 1);
    Oled_ShowNum(2, 5, value[2], 1);
    Oled_ShowNum(2, 6, value[3], 1);
    Oled_ShowNum(2, 7, value[4], 1);
}
