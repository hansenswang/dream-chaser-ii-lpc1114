/**
 * @file    system.h
 * @brief   Delays, SysTick indicators, one-touch key, UART console and OLED.
 */

#ifndef __SYSTEM_H__
#define __SYSTEM_H__

#include <stdint.h>

/* ---- delays ----------------------------------------------------------- */

void Delay_us(uint32_t us);
void Delay_ms(uint32_t ms);

/* ---- one-touch start key (P2_10, EINT2) ------------------------------- */

void    Key_Init(void);
uint8_t Key_Mode(void);

/* ---- UART console (UART0, 115200 8N1) --------------------------------- */

void Serial_Init(void);
void Serial_Putc(char c);
void Serial_Puts(const char *s);
void Serial_Printf(const char *fmt, ...);

/* ---- OLED (SSD1306 128x64 over I2C) ----------------------------------- */

void Oled_Init(void);
void Oled_Clear(void);
void Oled_ShowString(uint8_t row, uint8_t col, const char *str);
void Oled_ShowNum(uint8_t row, uint8_t col, uint32_t num, uint8_t width);
void Oled_ShowTrack(const uint8_t *value);

#endif /* __SYSTEM_H__ */
