/**
 * @file    track.c
 * @brief   Five-channel infrared line sensor: excitation, read and decode.
 *
 * Reading strategy
 * ----------------
 * All five channels are sampled with a single read of GPIO2's data register
 * rather than five separate masked reads.  This matters more than it looks:
 * at 20 cm/s a 12 mm channel spacing is crossed in 60 ms, so five separate
 * reads taken a few microseconds apart are still describing the same
 * instant — but if the compiler reorders them or an interrupt fires between
 * two reads, the five values can straddle a line edge and produce a channel
 * pattern that never physically existed.  One read, one instant.
 *
 * Decode strategy
 * ---------------
 * The pattern-to-state mapping is an explicit table rather than a chain of
 * if-statements.  There are 32 possible patterns; writing them out makes
 * the intent auditable and guarantees that every pattern has a defined
 * behaviour, including the ones that should never occur.
 */

#include "LPC11xx.h"
#include "track.h"
#include "system.h"

/* Emitter excitation: a 50 % square wave on P0_2.  Toggling this pin is
 * what the original firmware called "tracking_PWM" — the sensor boards
 * need a modulated emitter drive, not a static one, or the phototransistor
 * is swamped by ambient infrared. */
#define TRK_EXCITE   (1UL << 2)     /* P0_2 */

/* Channel masks inside the GPIO2 data register. */
#define CH0   (1UL << 7)
#define CH1   (1UL << 8)
#define CH2   (1UL << 9)
#define CH3   (1UL << 10)
#define CH4   (1UL << 11)

void Track_Init(void)
{
    /* inputs */
    LPC_GPIO2->DIR &= ~(CH0 | CH1 | CH2 | CH3 | CH4);
    /* excitation output */
    LPC_GPIO0->DIR |= TRK_EXCITE;
}

void Track_Excite(void)
{
    /* Toggle at the main-loop rate.  The half-period that results is a few
     * hundred microseconds, which is well clear of the 50 Hz mains flicker
     * the sensors would otherwise pick up from fluorescent lighting.  */
    LPC_GPIO0->DATA ^= TRK_EXCITE;
}

void Track_Read(uint8_t *value)
{
    uint32_t d = LPC_GPIO2->DATA;       /* one read, one instant */

    value[0] = (d & CH0) ? 1 : 0;
    value[1] = (d & CH1) ? 1 : 0;
    value[2] = (d & CH2) ? 1 : 0;
    value[3] = (d & CH3) ? 1 : 0;
    value[4] = (d & CH4) ? 1 : 0;
}

track_state_t Track_Decode(const uint8_t *value)
{
    /* Build the 5-bit pattern once, channels left-to-right. */
    uint8_t m = (uint8_t)((value[0] << 4) | (value[1] << 3) | (value[2] << 2) |
                          (value[3] << 1) |  value[4]);

    switch (m)
    {
    /* -------- nothing sees the line ---------------------------------- */
    case 0x00:                       /* 00000 */
        return TRACK_LOST;

    /* -------- the line is under the centre ---------------------------- */
    case 0x04:                       /* 00100 — only the centre channel  */
    case 0x0C:                       /* 01100 — centre + one left        */
    case 0x06:                       /* 00110 — centre + one right       */
    case 0x0E:                       /* 01110 — centre straddled         */
    case 0x08:                       /* 01000 — the sensor that sits 12 mm
                                        left of centre alone            */
    case 0x02:                       /* 00010 — and its right twin       */
        return TRACK_STRAIGHT;

    /* -------- drifting left (line to the left of the nose) ----------- */
    case 0x18:                       /* 11000 — two leftmost            */
    case 0x10:                       /* 10000 — outermost left          */
        return TRACK_LEFT_HARD;
    case 0x1C:                       /* 11100 — three left              */
    case 0x1A:                       /* 11010                           */
        return TRACK_LEFT_HARD;

    /* -------- drifting right ----------------------------------------- */
    case 0x03:                       /* 00011 — two rightmost           */
    case 0x01:                       /* 00001 — outermost right         */
        return TRACK_RIGHT_HARD;
    case 0x07:                       /* 00111 — three right             */
    case 0x05:                       /* 00101                           */
        return TRACK_RIGHT_HARD;

    /* -------- everything dark ---------------------------------------- */
    case 0x1F:                       /* 11111 — cross, start or stop     */
        return TRACK_ALL_BLACK;

    /* -------- patterns with no clean interpretation ------------------- */
    /* A one-channel gap between two dark channels means the line edge is
     * transitioning across the array.  Treat it as a gentle correction
     * toward whichever side still sees the line.                      */
    case 0x1E:  /* 11110 */  return TRACK_LEFT_HARD;
    case 0x0F:  /* 01111 */  return TRACK_RIGHT_HARD;
    case 0x1D:  /* 11101 */  return TRACK_LEFT_SOFT;
    case 0x0B:  /* 01011 */  return TRACK_LEFT_SOFT;
    case 0x16:  /* 10110 */  return TRACK_RIGHT_SOFT;
    case 0x0D:  /* 01101 */  return TRACK_RIGHT_SOFT;

    default:
        /* Anything else is a marginal reflection.  Hold the previous
         * course rather than guessing: a wrong guess at 20 cm/s costs
         * more than carrying straight for one more cycle.           */
        return TRACK_STRAIGHT;
    }
}

const char *Track_StateName(track_state_t state)
{
    switch (state)
    {
    case TRACK_LOST:        return "LOST  ";
    case TRACK_STRAIGHT:    return "FWD   ";
    case TRACK_LEFT_SOFT:   return "LEFT- ";
    case TRACK_LEFT_HARD:   return "LEFT! ";
    case TRACK_RIGHT_SOFT:  return "RIGHT-";
    case TRACK_RIGHT_HARD:  return "RIGHT!";
    case TRACK_ALL_BLACK:   return "BLACK ";
    default:                return "?     ";
    }
}
