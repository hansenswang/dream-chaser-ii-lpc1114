/**
 * @file    adc.h
 * @brief   Battery voltage supervision on ADC7 (P1_11).
 *
 * Two 14500 cells in series give 8.4 V fully charged, well above the
 * LPC1114's 3.6 V absolute maximum on any pin.  Three 10 kOhm resistors in
 * series (R14/R15/R16) form a divider; the tap between R15 and R16 presents
 * one third of the pack voltage, so 8.4 V becomes 2.8 V at the pin.  The
 * firmware multiplies by three to recover the true pack voltage.
 */

#ifndef __ADC_H__
#define __ADC_H__

#include <stdint.h>

void  Adc_Init(void);

/** Read the pack voltage in volts, compensated for the 1/3 divider. */
float Adc_ReadBattery(void);

/** Read the raw 10-bit conversion result, for calibration. */
uint16_t Adc_ReadRaw(void);

#endif /* __ADC_H__ */
