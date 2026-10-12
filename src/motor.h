/**
 * @file    motor.h
 * @brief   Four-wheel independent drive through four RZ7899 half-bridges.
 *
 * Each wheel has its own pair of direction pins and its own PWM input, so
 * all four can be commanded independently.  In practice the car drives the
 * two left wheels together and the two right wheels together, which is what
 * the differential-steering API below exposes.
 */

#ifndef __MOTOR_H__
#define __MOTOR_H__

#include <stdint.h>

/* ---- individual wheel pins ------------------------------------------- */
/* Front-left */
#define LF_FWD    (1UL << 2)    /* P1_2  forward                       */
#define LF_REV    (1UL << 4)    /* P1_4  reverse                       */
/* Rear-left */
#define LR_FWD    (1UL << 11)   /* P0_11 forward                       */
#define LR_REV    (1UL << 1)    /* P1_1  reverse                       */
/* Front-right */
#define RF_FWD    (1UL << 8)    /* P0_8  forward                       */
#define RF_REV    (1UL << 9)    /* P1_9  reverse                       */
/* Rear-right */
#define RR_FWD    (1UL << 9)    /* P0_9  forward                       */
#define RR_REV    (1UL << 10)   /* P1_10 reverse                       */

/* Convenience aliases for the two sides. */
#define LEFT_FWD   LF_FWD
#define LEFT_REV   LF_REV
#define RIGHT_FWD  RF_FWD
#define RIGHT_REV  RF_REV

/* ---- API ------------------------------------------------------------- */

void Motor_Init(void);

/** Drive both sides; each argument is 0..100. */
void Motor_SetSpeed(uint8_t left, uint8_t right);

/** Drive the four wheels individually, 0..100 each. */
void Motor_Wheels(uint8_t lf, uint8_t lr, uint8_t rf, uint8_t rr);

/** Drive a single side, 0..100. */
void Motor_LeftForward(uint8_t duty);
void Motor_LeftReverse(uint8_t duty);
void Motor_RightForward(uint8_t duty);
void Motor_RightReverse(uint8_t duty);

void Motor_Stop(void);

/** Short all four windings — dynamic braking. */
void Car_Brake(void);

/** Left-front + left-rear forward, right side idle: a gentle left pivot. */
void Car_PivotLeft(void);
void Car_PivotRight(void);

#endif /* __MOTOR_H__ */
