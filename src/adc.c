/**
 * @file    adc.c
 * @brief   Battery voltage supervision, software-triggered conversion.
 *
 * Configuration notes
 * -------------------
 * The LPC1114 ADC is clocked from the system clock through CLKDIV in the
 * CR register.  A software-triggered conversion takes eleven ADC clocks,
 * so the divider must not be so large that a conversion overruns the loop
 * it is called from.  A CLKDIV value of 23 gives 24 system clocks per ADC
 * clock — about 5.3 us per conversion at 48 MHz, comfortably inside one
 * main-loop pass.
 *
 * Averaging
 * ---------
 * A single conversion is noisy enough (±2 LSB) to make the low-battery
 * threshold chatter.  Eight samples are summed and shifted, which costs
 * 40 us and removes the chatter entirely — cheaper than adding hysteresis
 * to the threshold logic, and it also filters the droop that appears when
 * the motors start.
 */

#include "LPC11xx.h"
#include "adc.h"

#define ADC_CHANNEL     7           /* ADC7 is on P1_11 */
#define ADC_CLKDIV      23
#define ADC_AVG_SHIFT   3           /* divide the 8-sample sum by 8 */

/* Reference is the 3.3 V rail.  Note that this is the *same* rail that
 * powers the core, so the measurement inherits its error: if the rail sags
 * under motor load, the reported battery voltage sags with it.  The
 * averaging below mitigates but does not eliminate this — a real fix would
 * use the internal band-gap as reference.                                */
#define VREF_MILLIVOLTS 3300u

/* The divider presents one third of the pack; multiply back by three. */
#define DIVIDER_NUM     3u

void Adc_Init(void)
{
    /* P1_11 as AD7: function 1 in IOCON. */
    LPC_IOCON->PIO1_11 &= ~0x07;
    LPC_IOCON->PIO1_11 |=  0x01;

    /* Power up the ADC block and its clock. */
    LPC_SYSCON->PDRUNCFG     &= ~(1UL << 4);
    LPC_SYSCON->SYSAHBCLKCTRL |= (1UL << 13);
}

static uint16_t adc_convert_once(void)
{
    /* BURST = 0, software trigger, channel 7. */
    LPC_ADC->CR &= ~(1UL << 16);
    LPC_ADC->CR &= ~0xFF;
    LPC_ADC->CR |=  (1UL << ADC_CHANNEL);
    LPC_ADC->CR &= ~(0xFFUL << 8);
    LPC_ADC->CR |=  ((uint32_t)ADC_CLKDIV << 8);

    /* Start the conversion. */
    LPC_ADC->CR |= (1UL << 24);

    /* Wait for DONE before reading, otherwise DR[7] still holds the
     * previous result and the caller silently reads stale data. */
    while ((LPC_ADC->DR[7] & 0x80000000UL) == 0) { }

    /* The result sits in bits 15..6; the low six bits are always zero. */
    return (uint16_t)((LPC_ADC->DR[7] >> 6) & 0x3FF);
}

uint16_t Adc_ReadRaw(void)
{
    uint32_t sum = 0;
    uint8_t  i;

    for (i = 0; i < (1u << ADC_AVG_SHIFT); i++)
    {
        sum += adc_convert_once();
    }

    return (uint16_t)(sum >> ADC_AVG_SHIFT);
}

float Adc_ReadBattery(void)
{
    uint32_t raw = Adc_ReadRaw();

    /* Millivolts at the pin, then undo the divider.
     * Multiply before dividing so the fractional part survives — with
     * integers the other order loses everything below a volt.        */
    uint32_t mv_pin  = (raw * VREF_MILLIVOLTS) / 1024u;
    uint32_t mv_pack = mv_pin * DIVIDER_NUM;

    return (float)mv_pack / 1000.0f;
}
