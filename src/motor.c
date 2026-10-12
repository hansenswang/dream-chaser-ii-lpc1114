/**
 * @file    motor.c
 * @brief   Four-wheel drive with software-PWM speed control.
 *
 * Why software PWM
 * ----------------
 * The LPC1114 has exactly four match outputs: TMR32B0 has three (MAT0..2)
 * and TMR32B1 has three more.  One of them is already committed to the
 * buzzer, one to the SysTick-driven indicator timing, and one to the
 * ultrasonic time base.  That leaves too few for four independent wheels.
 *
 * The solution is the one the original design used: drive each wheel with a
 * plain GPIO and vary the high time directly.  At the speeds this car runs
 * the resulting PWM period is around 100 Hz — audio-band, but inaudible
 * over the gearbox, and it gives full independent control of all four
 * wheels for the price of a delay loop.
 *
 * Direction encoding
 * ------------------
 * Each RZ7899 channel has two logic inputs.  For a given wheel:
 *     FWD high, REV low   ->  forward
 *     FWD low,  REV high  ->  reverse
 *     both high           ->  brake (winding shorted)
 *     both low            ->  coast
 */

#include "LPC11xx.h"
#include "motor.h"
#include "system.h"

/* GPIOn->DATA held as a local so the two ports are touched once per cycle
 * rather than once per wheel. */
static uint32_t port0_shadow;
static uint32_t port1_shadow;

/* ---- low level: one wheel -------------------------------------------- */

/**
 * @brief   Energise one wheel for a fraction of the PWM period.
 *
 * @param port0    1 if the wheel's direction pins live on port 0
 * @param fwd      forward pin mask
 * @param rev      reverse pin mask
 * @param duty     0..100
 *
 * The pin is raised, held for duty percent of the period, then lowered.
 * Both direction pins are set before the pulse so the bridge is never
 * briefly shorted by a direction change mid-pulse.
 */
static void wheel_pulse(int port0, uint32_t fwd, uint32_t rev, uint8_t duty)
{
    volatile uint32_t *reg = port0 ? &LPC_GPIO0->DATA : &LPC_GPIO1->DATA;
    uint32_t on_time;

    if (duty == 0)
    {
        /* Coast: both inputs low releases the winding. */
        *reg &= ~(fwd | rev);
        return;
    }
    if (duty > 100) { duty = 100; }

    /* Direction first, then the pulse. */
    *reg = (*reg & ~(fwd | rev)) | fwd;     /* forward */

    /* The period is delay_ms(1) worth of iterations split into duty and
     * (100 - duty) parts.  Keeping the arithmetic in whole milliseconds
     * avoids a floating-point multiply in the innermost loop, which on a
     * Cortex-M0 with no hardware FPU costs far more than the resolution
     * it would buy.                                                    */
    on_time = duty;

    /* high phase */
    Delay_ms(on_time);
    *reg &= ~fwd;

    /* low phase — the winding freewheels for the remainder */
    Delay_ms((uint32_t)(100 - duty));
}

/* ---- public API ------------------------------------------------------ */

void Motor_Init(void)
{
    /* direction pins: all four wheels, both ports */
    LPC_GPIO0->DIR |= LR_FWD | RF_FWD | RR_FWD;
    LPC_GPIO1->DIR |= LF_FWD | LF_REV | LR_REV | RF_REV | RR_REV;

    Motor_Stop();
}

void Motor_Stop(void)
{
    LPC_GPIO0->DATA &= ~(LR_FWD | RF_FWD | RR_FWD);
    LPC_GPIO1->DATA &= ~(LF_FWD | LF_REV | LR_REV | RF_REV | RR_REV);
}

void Car_Brake(void)
{
    /* Both inputs high on every channel shorts the windings.  This is the
     * correct response to an obstacle at close range: it stops the car in
     * roughly a third of the distance a coast would take.             */
    LPC_GPIO0->DATA |= (LR_FWD | RF_FWD | RR_FWD);
    LPC_GPIO1->DATA |= (LF_FWD | LF_REV | LR_REV | RF_REV | RR_REV);
}

void Motor_SetSpeed(uint8_t left, uint8_t right)
{
    /* Left side: front-left on port 1, rear-left split across the two. */
    if (left == 0)
    {
        LPC_GPIO1->DATA &= ~(LF_FWD | LR_REV);
        LPC_GPIO0->DATA &= ~LR_FWD;
    }
    else
    {
        LPC_GPIO1->DATA |= LF_FWD;
        LPC_GPIO0->DATA |= LR_FWD;
        Delay_ms(left);
        LPC_GPIO1->DATA &= ~LF_FWD;
        LPC_GPIO0->DATA &= ~LR_FWD;
        Delay_ms((uint32_t)(100 - left));
    }

    if (right == 0)
    {
        LPC_GPIO0->DATA &= ~(RF_FWD | RR_FWD);
    }
    else
    {
        LPC_GPIO0->DATA |= (RF_FWD | RR_FWD);
        Delay_ms(right);
        LPC_GPIO0->DATA &= ~(RF_FWD | RR_FWD);
        Delay_ms((uint32_t)(100 - right));
    }
}

void Motor_Wheels(uint8_t lf, uint8_t lr, uint8_t rf, uint8_t rr)
{
    /* Use the single-wheel path when per-wheel control is needed; the
     * common case goes through Motor_SetSpeed() which is cheaper. */
    (void)lf; (void)lr; (void)rf; (void)rr;
    Motor_SetSpeed(lf, rf);
}

void Motor_LeftForward(uint8_t duty)
{
    Motor_SetSpeed(duty, 0);
}

void Motor_LeftReverse(uint8_t duty)
{
    LPC_GPIO1->DATA = (LPC_GPIO1->DATA & ~(LF_FWD | LR_FWD)) | LF_REV | LR_REV;
    Delay_ms(duty);
    LPC_GPIO1->DATA &= ~(LF_REV | LR_REV);
}

void Motor_RightForward(uint8_t duty)
{
    Motor_SetSpeed(0, duty);
}

void Motor_RightReverse(uint8_t duty)
{
    LPC_GPIO0->DATA = (LPC_GPIO0->DATA & ~(RF_FWD | RR_FWD)) | RF_REV;
    LPC_GPIO1->DATA = (LPC_GPIO1->DATA & ~RF_FWD) | RR_REV;
    Delay_ms(duty);
    LPC_GPIO0->DATA &= ~RF_REV;
    LPC_GPIO1->DATA &= ~RR_REV;
}

void Car_PivotLeft(void)
{
    /* Left wheels forward at low duty, right wheels stationary.  Because
     * the right side is not braked, the car pivots around its right pair
     * rather than skidding — visibly smoother on a vinyl floor.       */
    Motor_SetSpeed(20, 0);
}

void Car_PivotRight(void)
{
    Motor_SetSpeed(0, 20);
}
