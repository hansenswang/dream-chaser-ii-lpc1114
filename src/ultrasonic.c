/**
 * @file    ultrasonic.c
 * @brief   HC-SR04 time-of-flight measurement.
 *
 * Principle
 * ---------
 * A 10 us pulse on TRIG makes the module emit eight cycles at 40 kHz.  The
 * module then raises ECHO for exactly as long as it takes the burst to
 * travel out, reflect, and return.  At 20 degrees Celsius sound travels at
 * 343 m/s = 0.0343 cm/us, and the distance is half the round trip:
 *
 *     distance_cm = pulse_us * 0.0343 / 2 = pulse_us * 0.01715
 *
 * The firmware uses 0.017, which corresponds to a sound speed of about
 * 340 m/s and is the calibration that matched a tape measure in the lab.
 *
 * Why the measurement is bounded
 * -----------------------------
 * If no echo returns (the surface is angled away, or nothing is within
 * range) ECHO never goes low and a naive `while (ECHO high)` would hang
 * forever.  A watchdog counter bounds the wait at about 25 ms, which is
 * the module's own maximum range — past that the reading is meaningless
 * anyway.
 */

#include "LPC11xx.h"
#include "ultrasonic.h"
#include "system.h"

#define TRIG_PIN    (1UL << 5)      /* P2_5 */
#define ECHO_PIN    (1UL << 4)      /* P2_4 */

/* 25 ms at 1 us per tick. */
#define ECHO_TIMEOUT_US   25000UL

void Hcsr04_Init(void)
{
    /* CT16B0 clock on (bit 7 of SYSAHBCLKCTRL). */
    LPC_SYSCON->SYSAHBCLKCTRL |= (1UL << 7);

    /* Prescaler 47 gives a 1 us tick from the 48 MHz peripheral clock. */
    LPC_TMR16B0->PR  = 48 - 1;
    LPC_TMR16B0->TCR = 0;           /* stopped until a measurement starts */

    /* TRIG is an output, ECHO an input. */
    LPC_GPIO2->DIR |=  TRIG_PIN;
    LPC_GPIO2->DIR &= ~ECHO_PIN;
    LPC_GPIO2->DATA &= ~TRIG_PIN;
}

void HC_SR04_Trigger(void)
{
    /* Pull TRIG high for 10 us, then release.  Ten microseconds is the
     * datasheet minimum; the original code used the same value and it
     * proved reliable across the whole range. */
    LPC_GPIO2->DATA |=  TRIG_PIN;
    Delay_us(10);
    LPC_GPIO2->DATA &= ~TRIG_PIN;
}

float HC_SR04_GetDistance(void)
{
    uint32_t timeout;
    uint32_t pulse_us;

    HC_SR04_Trigger();

    /* Wait for ECHO to rise.  Bound the wait: if the previous measurement
     * has not finished, the module may still be holding ECHO high, and
     * this loop would otherwise never be entered. */
    timeout = ECHO_TIMEOUT_US;
    while (!(LPC_GPIO2->DATA & ECHO_PIN))
    {
        if (--timeout == 0) { return 0.0f; }
    }

    /* Time the high phase. */
    LPC_TMR16B0->TC  = 0;
    LPC_TMR16B0->TCR = 0x01;

    timeout = ECHO_TIMEOUT_US;
    while (LPC_GPIO2->DATA & ECHO_PIN)
    {
        if (--timeout == 0) { break; }
    }

    LPC_TMR16B0->TCR = 0x00;

    /* TC is 16 bits; the prescaler makes one count one microsecond. */
    pulse_us = LPC_TMR16B0->TC;

    if (pulse_us == 0 || timeout == 0)
    {
        return 0.0f;                /* no usable echo */
    }

    return (float)pulse_us * CM_PER_US_HALF;
}

uint8_t HC_SR04_ObstacleAhead(void)
{
    float d = HC_SR04_GetDistance();

    /* A zero reading means no echo, which is *not* an obstacle — it is
     * clear space.  Treating zero as "very close" is the classic bug here
     * and makes the car brake at nothing. */
    if (d <= 0.0f) { return 0; }

    return (d <= OBSTACLE_CM) ? 1 : 0;
}
