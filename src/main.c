/**
 * @file    main.c
 * @brief   逐梦贰号 — multi-peripheral smart car, first iteration (August 2025).
 *
 * Platform : LPC1114 (ARM Cortex-M0, 50 MHz, 32 KB Flash / 8 KB RAM)
 * Board    : 逐梦贰号 self-designed PCB, 70 x 100 mm, JRLCEDA free prototype
 *
 * This is the "everything works at once" car.  Eight independent subsystems
 * run off one 7.4 V battery pack:
 *
 *   1. Four-wheel drive, independent PWM per wheel          (TMR32B0 MAT2)
 *   2. One-touch start key on an external interrupt         (P2_10 / EINT2)
 *   3. Passive buzzer playing a melody                      (TMR32B0 PWM)
 *   4. Battery voltage sensing with a low-voltage warning   (ADC7 / P1_11)
 *   5. Two head lights plus four turn indicators            (GPIO)
 *   6. Ultrasonic obstacle avoidance                        (HC-SR04)
 *   7. Five-channel infrared line following                 (ITR9909)
 *   8. UART console for debug and future wireless expansion (115200 8N1)
 *
 * The design goal was coverage, not depth: each peripheral demonstrates one
 * microcontroller concept — output, input, timer, ADC, interrupt, PWM,
 * external sensor.  The successor project (relay car, December 2025) takes
 * one of these — line following — and drives it much further with an RTOS
 * and a closed-loop controller.
 */

#include "LPC11xx.h"
#include "system.h"

/* ====================================================================== */
/*  Global state                                                          */
/* ====================================================================== */

/* Key press cycles through three modes.  0 = running, 1 = playing music,
 * 2 = music muted.  The mode is advanced in the interrupt handler and
 * consumed in the main loop.                                            */
volatile uint8_t key = 0;

/* Turn-indicator flag: 0 = off, 1 = left, 2 = right.  Set by the steering
 * helpers, consumed by the 100 ms SysTick handler.                      */
volatile uint8_t L = 0;

/* Ultrasonic round-trip time in microseconds, written by the capture
 * routine and read by the distance converter.                           */
volatile uint32_t pulseWidth = 0;

/* Battery voltage as measured through the 1/3 divider, in volts. */
volatile float battery_voltage = 0.0f;

/* Five line-sensor channels, 0 = white, 1 = black. */
uint8_t value[5] = {0, 0, 0, 0, 0};

/* ====================================================================== */
/*  Melody tables                                                         */
/* ====================================================================== */

/* Note periods as timer counts, index 0 being a rest.  Intermediate and
 * low octave.                                                           */
const uint32_t FREQ[] = {
    0,      /* rest                                                        */
    1908,  2381,  2672,  3174,  3571,  4000,  4762,   /* C5..B5          */
    952,   1190,  1336,  1587,  1786,  2000,  2381,   /* C6..B6          */
};

/* Beat durations in milliseconds. */
const uint16_t beat[] = { 125, 250, 375, 500, 750, 1000 };

/* The melody itself: pairs of (note index, beat index).  Terminated by a
 * zero note.                                                            */
const uint8_t song1[] = {
    8, 1,  8, 1,  10, 1, 10, 1,  12, 2,
    12, 1, 10, 1, 8, 1,  10, 2,
    8, 1,  7, 1,  5, 1,  5, 1,   7, 2,
    0, 0
};

/* ====================================================================== */
/*  Entry point                                                           */
/* ====================================================================== */

int main(void)
{
    float distance;

    /* ---- bring-up, ordered so that each step can be observed --------- */
    Led_Init();             /* lights first — confirms power              */
    SysTick_Config(SystemCoreClock / 10);  /* 100 ms tick for indicators */

    Buzzer_Init();          /* TMR32B0 in PWM mode on P0_1                */
    Buzzer_StartupChime();  /* short two-note chime: hardware alive       */

    Oled_Init();            /* 128x64 SSD1306 over I2C                    */
    Serial_Init();          /* UART0 115200 8N1                           */
    Track_Init();           /* five ITR9909 channels on P2_7..P2_11       */
    Key_Init();             /* one-touch start, P2_10 external interrupt  */
    Adc_Init();             /* battery sense on ADC7 / P1_11              */

    /* Obstacle avoidance is initialised last because it shares the
     * 16-bit timer block with the line-sensor excitation signal — see
     * hardware/README.md for how the two were made to coexist.         */
    Hcsr04_Init();          /* TRIG P2_5, ECHO P2_4                       */

    Serial_Printf("dream-chaser-ii: boot ok\r\n");

    /* ---- main loop ---------------------------------------------------
     * The loop body is deliberately a straight-line sequence rather than
     * a scheduler: at this stage of the project there is exactly one
     * control context, and every peripheral is polled.  The successor
     * project replaced this with three RT-Thread threads once the
     * blocking delays below started to interfere with each other.     */
    while (1)
    {
        /* (1) battery supervision -----------------------------------
         * A low pack shows up as a head-light breathing pattern, which
         * is the visual stand-in for a fuel gauge.  Reading the ADC
         * takes ~11 ADC clocks and does not block.                   */
        battery_voltage = Adc_ReadBattery();
        if (battery_voltage < 6.6f)
        {
            Led_Breathe(1);         /* low battery — slow fade          */
        }
        else
        {
            Led_On();
        }

        /* (2) obstacle check -----------------------------------------
         * Sample first, then decide.  The 40 cm threshold gives roughly
         * 1.2 s of braking distance at the cruise speed used here.   */
        HC_SR04_Trigger();
        distance = HC_SR04_GetDistance();

        Oled_ShowString(1, 1, "d:");
        Oled_ShowNum(1, 4, (uint32_t)distance, 4);
        Oled_ShowString(1, 9, "cm");

        if (distance > 0.0f && distance <= 40.0f)
        {
            /* Stop, hold, then pivot left and continue.  The hold is
             * what stops the car from twitching on a marginal reading
             * that flickers across the threshold.                    */
            Car_Brake();
            Delay_ms(300);
            Car_PivotLeft();
        }
        else
        {
            /* (3) line following --------------------------------------
             * The five channels are read as a single port access so that
             * all five values describe the same instant.             */
            Track_Excite();         /* keep the emitters modulated     */
            Track_Read(value);
            Oled_ShowTrack(value);

            switch (Track_Decode(value))
            {
            case TRACK_STRAIGHT:  Motor_SetSpeed(20, 20); break;
            case TRACK_LEFT_SOFT: Motor_SetSpeed(10, 20); break;
            case TRACK_LEFT_HARD: Motor_SetSpeed( 5, 20); break;
            case TRACK_RIGHT_SOFT:Motor_SetSpeed(20, 10); break;
            case TRACK_RIGHT_HARD:Motor_SetSpeed(20,  5); break;
            default:              Motor_SetSpeed(20, 20); break;
            }
        }

        /* (4) musical accompaniment, only while the mode allows it ---- */
        if (key == 1)
        {
            Buzzer_PlayMelody(song1, FREQ, beat);
        }
        else
        {
            Buzzer_Silence();
        }
    }
}
