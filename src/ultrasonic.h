/**
 * @file    ultrasonic.h
 * @brief   HC-SR04 distance measurement and the obstacle-avoidance response.
 *
 * Wiring: TRIG on P2_5 (output), ECHO on P2_4 (input).
 *
 * Timing is measured with CT16B0 at a prescaler of 47, giving a 1 us tick
 * from a 48 MHz peripheral clock.  The counter runs only while ECHO is
 * high, so the final count *is* the round-trip time in microseconds.
 */

#ifndef __ULTRASONIC_H__
#define __ULTRASONIC_H__

#include <stdint.h>

/* Close-range threshold.  40 cm leaves about 1.2 s of braking distance at
 * the cruise speed this car uses. */
#define OBSTACLE_CM     40.0f

/* Sound speed assumed by the conversion, in cm/us, times two for the round
 * trip — folded into a single constant. */
#define CM_PER_US_HALF  0.017f

void     Hcsr04_Init(void);

/** Emit the 10 us trigger pulse. */
void     HC_SR04_Trigger(void);

/** Measure and return the distance in centimetres; 0 if no echo. */
float    HC_SR04_GetDistance(void);

/** Convenience: 1 if an obstacle is inside OBSTACLE_CM. */
uint8_t  HC_SR04_ObstacleAhead(void);

#endif /* __ULTRASONIC_H__ */
