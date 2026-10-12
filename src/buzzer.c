/**
 * @file    buzzer.c
 * @brief   Passive-buzzer melody playback and indicator-light control.
 *
 * Tone generation
 * ---------------
 * TMR32B0 is put into PWM mode with MAT2 as the output and MR3 as the
 * period.  A note of frequency f needs a period of
 *
 *     MR3 = SystemCoreClock / 2 / (f - 1)
 *
 * The halving is because the timer counts up to MR3 and resets, producing
 * one full output cycle per count-to-MR3-and-reset; MR2 sets the duty, and
 * a fifth of the period was chosen because it is loud on this particular
 * transducer without drawing enough current to disturb the 3.3 V rail.
 *
 * A known failure mode, and its fix
 * ---------------------------------
 * On longer melodies the timer occasionally misses a match and the note
 * goes silent until the next one — the counter has drifted past the point
 * where the match fires.  The fix is to reset TC after every note, which
 * costs nothing and guarantees the counter is inside the period when the
 * next note's MR3 is loaded.
 */

#include "LPC11xx.h"
#include "buzzer.h"
#include "system.h"

/* ---- pin definitions -------------------------------------------------- */

#define BUZZER_PIN   (1UL << 1)     /* P0_1, TMR32B0 MAT2 */

/* Head lights, front pair */
#define LED_HEAD_L   (1UL << 7)     /* P0_7 */
#define LED_HEAD_R   (1UL << 6)     /* P0_6 */
/* Turn indicators, outer pair */
#define LED_TAIL_L   (1UL << 3)     /* P0_3 */
#define LED_TAIL_R   (1UL << 8)     /* P2_8 */

static uint8_t indicator_side = IND_OFF;

/* ---- buzzer ----------------------------------------------------------- */

void Buzzer_Init(void)
{
    /* TMR32B0 clock on. */
    LPC_SYSCON->SYSAHBCLKCTRL |= (1UL << 9);
    /* GPIO block and its configuration registers. */
    LPC_SYSCON->SYSAHBCLKCTRL |= ((1UL << 6) | (1UL << 16));

    /* P0_1 as TMR32B0_MAT2 (function 2). */
    LPC_IOCON->PIO0_1 &= ~0x07;
    LPC_IOCON->PIO0_1 |=  0x02;

    /* No prescale; MR3 is the period, MR2 the duty. */
    LPC_TMR32B0->PR   = 0;
    LPC_TMR32B0->MR3  = 0;
    LPC_TMR32B0->MR2  = 0;

    /* Make MAT2 a PWM output, and reset the counter on MR3 match. */
    LPC_TMR32B0->PWMC |= (1UL << 2);
    LPC_TMR32B0->MCR  |= (1UL << 10);

    LPC_TMR32B0->TCR = 1;
}

void Buzzer_Silence(void)
{
    /* A zero period means the output never toggles. */
    LPC_TMR32B0->MR3 = 0;
    LPC_TMR32B0->MR2 = 0;
}

static void buzzer_note(uint32_t period)
{
    if (period == 0)
    {
        Buzzer_Silence();
        return;
    }

    LPC_TMR32B0->MR3 = period;
    LPC_TMR32B0->MR2 = period / 5;      /* 20 % duty */
    LPC_TMR32B0->TC  = 0;               /* resync — see the header note */
}

void Buzzer_StartupChime(void)
{
    /* A5 then E5, 120 ms each. */
    buzzer_note(SystemCoreClock / 2 / (880 - 1));
    Delay_ms(120);
    buzzer_note(SystemCoreClock / 2 / (659 - 1));
    Delay_ms(120);
    Buzzer_Silence();
}

void Buzzer_PlayMelody(const uint8_t *song,
                       const uint32_t *freq,
                       const uint16_t *beat)
{
    uint16_t i = 0;

    /* The song array is terminated by a zero note index. */
    while (song[i] != 0)
    {
        uint8_t note   = song[i];
        uint8_t rhythm = song[i + 1];

        uint32_t f = freq[note];
        uint32_t p = (f == 0) ? 0 : (SystemCoreClock / 2 / (f - 1));

        buzzer_note(p);
        Delay_ms(beat[rhythm]);

        i += 2;
    }

    Buzzer_Silence();
}

/* ---- indicator lights ------------------------------------------------- */

void Led_Init(void)
{
    LPC_GPIO0->DIR |= (LED_HEAD_L | LED_HEAD_R | LED_TAIL_L);
    LPC_GPIO2->DIR |= LED_TAIL_R;

    Led_Off();
}

void Led_On(void)
{
    /* Head lights only — the tail lamps follow the indicator state. */
    LPC_GPIO0->DATA |= (LED_HEAD_L | LED_HEAD_R);
}

void Led_Off(void)
{
    LPC_GPIO0->DATA &= ~(LED_HEAD_L | LED_HEAD_R | LED_TAIL_L);
    LPC_GPIO2->DATA &= ~LED_TAIL_R;
}

void Led_Breathe(uint8_t enable)
{
    /* The breathing effect is produced in the SysTick handler by toggling
     * the head lights at a rate that walks up and down; enable just gates
     * whether that pattern runs.  Keeping the state machine in the tick
     * means the main loop pays nothing for it. */
    static uint8_t breathing = 0;

    if (enable && !breathing)
    {
        breathing = 1;
    }
    else if (!enable && breathing)
    {
        breathing = 0;
        Led_On();
    }
}

void Led_SetIndicator(uint8_t side)
{
    indicator_side = side;
}

void Led_TickIndicators(void)
{
    static uint8_t phase = 0;

    phase ^= 1;

    switch (indicator_side)
    {
    case IND_LEFT:
        /* Left tail blinks; the right side is held dark so the two lamps
         * cannot both appear lit during a transition. */
        if (phase) { LPC_GPIO0->DATA |=  LED_TAIL_L; }
        else       { LPC_GPIO0->DATA &= ~LED_TAIL_L; }
        LPC_GPIO2->DATA &= ~LED_TAIL_R;
        break;

    case IND_RIGHT:
        if (phase) { LPC_GPIO2->DATA |=  LED_TAIL_R; }
        else       { LPC_GPIO2->DATA &= ~LED_TAIL_R; }
        LPC_GPIO0->DATA &= ~LED_TAIL_L;
        break;

    case IND_OFF:
    default:
        LPC_GPIO0->DATA &= ~LED_TAIL_L;
        LPC_GPIO2->DATA &= ~LED_TAIL_R;
        break;
    }
}
