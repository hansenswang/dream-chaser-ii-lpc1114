/**
 * @file    buzzer.h
 * @brief   Passive buzzer melody playback and head/turn indicator lights.
 *
 * The buzzer is passive: it has no internal oscillator, so the firmware
 * must supply the tone.  TMR32B0 produces it directly on MAT2 (P0_1) by
 * setting the PWM period to the note's wavelength — no software toggling,
 * which is why the melody can play while the main loop is busy.
 */

#ifndef __BUZZER_H__
#define __BUZZER_H__

#include <stdint.h>

/* Indicator flag values shared with the SysTick handler. */
#define IND_OFF     0
#define IND_LEFT    1
#define IND_RIGHT   2

void Buzzer_Init(void);

/** Emit a short two-note chime used to signal a successful boot. */
void Buzzer_StartupChime(void);

/**
 * Play a melody.
 *
 * @param song   pairs of (note index, beat index), terminated by note 0
 * @param freq   note index -> timer period
 * @param beat   beat index -> duration in milliseconds
 *
 * Returns after the terminating rest, so with a non-terminating song this
 * blocks — which is why it is only called when the key has explicitly
 * selected music mode.
 */
void Buzzer_PlayMelody(const uint8_t *song,
                       const uint32_t *freq,
                       const uint16_t *beat);

/** Stop any tone currently being produced. */
void Buzzer_Silence(void);

/* ---- indicator lights ------------------------------------------------ */

void Led_Init(void);
void Led_On(void);
void Led_Off(void);
void Led_Breathe(uint8_t enable);

/** Called from the 100 ms SysTick handler to advance the turn indicators. */
void Led_TickIndicators(void);

/** Select which side blinks; IND_OFF stops both. */
void Led_SetIndicator(uint8_t side);

#endif /* __BUZZER_H__ */
