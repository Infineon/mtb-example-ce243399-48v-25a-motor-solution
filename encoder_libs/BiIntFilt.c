/******************************************************************************
* File Name: BiIntFilt.h
*
* Description: Double integrator filter library used to filter 15bit encoder angle values.
*              Return filtered angle and angular velocity in [-pi ... pi].
*              by Rene Santeler & Maurizio Incurvati
*
*******************************************************************************
* Copyright 2025-2026, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/******************************************************************************
* File Name: BiIntFilt.h / BiIntFilt.c  (Optimized)
******************************************************************************/

#include <math.h>
#include "cybsp.h"
#include "BiIntFilt.h"

#include "ParamConfig.h"

/* M_PI is a POSIX extension not guaranteed by standard C (e.g. IAR omits it).
 * Provide a fallback so the build is portable across all toolchains.         */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define F_PI        ((float)M_PI)
#define TWO_F_PI    ((float)(2.0 * M_PI))

/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

/* Wraps any input to [0 ... 2pi].
 * Fast path: If the value is already in [0 ... 4pi] only one subtraction is needed.
 * For arbitrary input we fall back to fmodf.                                 */
static inline float wrap_2pi(float a)
{
    /* Fast path covers the common case (single-revolution increments).       */
    if (a >= 0.0f) {
        if (a < TWO_F_PI)  return a;
        if (a < 4.0f * F_PI) return a - TWO_F_PI;
    }
    else {
        if (a >= -TWO_F_PI) return a + TWO_F_PI;
    }
    /* Slow path – only reached on large jumps / first call                   */
    a = fmodf(a, TWO_F_PI);
    if (a < 0.0f) a += TWO_F_PI;
    return a;
}

/* Shortest signed difference (a - b) mapped to [-pi ... +pi].                */
static inline float angle_diff(float a, float b)
{
    float d = a - b;
    /* Bring d into (-pi, +pi] with at most two conditional adds.             */
    if      (d >  F_PI) d -= TWO_F_PI;
    else if (d < -F_PI) d += TWO_F_PI;
    /* A second correction handles the (unlikely) case |a-b| > 2pi           */
    if      (d >  F_PI) d -= TWO_F_PI;
    else if (d < -F_PI) d += TWO_F_PI;
    return d;
}

/* Minimal signed error (target - current) in [-pi, +pi].                    */
static inline float angle_error(float target, float current) {
    return angle_diff(target, current);
}

/* ---------------------------------------------------------------------------
 * Init
 * -------------------------------------------------------------------------*/
void BiIntFilt_Init(BiIntFilt_t *bif,
                    float        Ts,
                    float        k_L1,
                    float        k_L2,
                    float        pole_pairs,
                    float        omega_limit,
                    float        deadband,
                    uint32_t     fixed_offset_mech,
                    uint32_t     alignment_phase_time_ms)
{
    /* ---- Configuration ---- */
    bif->Ts                     = Ts;
    bif->k_L1                   = k_L1;
    bif->k_L2                   = k_L2;
    bif->pole_pairs             = pole_pairs;
    bif->deadband               = deadband;
    bif->omega_limit            = omega_limit;
    bif->fixed_offset_mech      = fixed_offset_mech;
    bif->alignment_phase_time_ms = alignment_phase_time_ms;

    /* ---- Precomputed constants (avoids repeated calculation in Update) ---- */
#if defined(ENC_TLx5012)
    bif->counts_to_rad          = TWO_F_PI / 32767.0f;
    bif->rad_to_counts          = 32768.0f / TWO_F_PI;   /* 1<<15 */
#elif defined(ENC_TLx49012)
    bif->counts_to_rad          = TWO_F_PI / 65535.0f;
    bif->rad_to_counts          = 65536.0f / TWO_F_PI;   /* 1<<16 */
#endif

    /* Combined mechanical output correction:
     *   + pi   → shift [0,2pi) to [-pi, +pi)
     *   + (pi/2) / pole_pairs → compensate pi/2 electrical alignment        */
    bif->mech_correction        = F_PI + (F_PI * 0.5f) / pole_pairs;

    /* ---- Internal states ---- */
    bif->was_align_mode_entered          = 0u;
    bif->was_theta_start_offset_estimated = 0u;
    bif->last_align_mode_entered_time    = 0u;
    bif->theta_start_offset              = 0u;

    bif->theta_meas_mech        = 0u;
    bif->theta_meas_mech_rad    = 0.0f;
    bif->err_mech_rad           = 0.0f;
    bif->omega_mech_flt2        = 0.0f;
    bif->theta_mech_flt2        = 0.0f;

    /* ---- Outputs ---- */
    bif->theta_mech_flt         = 0.0f;
    bif->theta_elec_flt         = 0.0f;
    bif->omega_mech_flt         = 0.0f;
    bif->omega_elec_flt         = 0.0f;
}

/* ---------------------------------------------------------------------------
 * Update  (called every Ts)
 * -------------------------------------------------------------------------*/
void BiIntFilt_Update(BiIntFilt_t *bif,
                      uint32_t     theta_raw,
                      uint8_t      isControlInAlignMode,
                      uint32_t     timeNowMs)
{
    /* ------------------------------------------------------------------
     * 1. Apply fixed + dynamic offset and extract encoder bits
     * ----------------------------------------------------------------*/
#if defined(ENC_TLx5012)
    uint32_t raw_sum     = (theta_raw & 0x7FFFu)
                         + bif->fixed_offset_mech
                         + bif->theta_start_offset;
    bif->theta_meas_mech = (uint16_t)(raw_sum & 0x7FFFu);
#elif defined(ENC_TLx49012)
    bif->theta_meas_mech = (uint16_t)(theta_raw
                         + bif->fixed_offset_mech
                         + bif->theta_start_offset);
#endif

    /* ------------------------------------------------------------------
     * 2. Alignment-phase offset calibration
     * ----------------------------------------------------------------*/
    if (isControlInAlignMode) {
        if (bif->was_align_mode_entered == 0u) {
            bif->last_align_mode_entered_time = timeNowMs;
            bif->was_align_mode_entered = 1u;
            bif->theta_start_offset     = 0u;
        }

        if (bif->was_theta_start_offset_estimated == 0u) {
            uint32_t elapsed   = (timeNowMs) - bif->last_align_mode_entered_time;
            uint32_t threshold = (uint32_t)(bif->alignment_phase_time_ms * 0.75f);

            if (elapsed > threshold) {
                /* Error between current electrical angle and 3pi/2 target    */
                float elec_angle = wrap_2pi(bif->theta_mech_flt2 * bif->pole_pairs);
                /* angle_error(3pi/2, elec_angle) == angle_diff(3pi/2, elec_angle) */
                float err_elec   = angle_diff(1.5f * F_PI, elec_angle);
                float err_mech   = err_elec / bif->pole_pairs;

                /* rad → counts using precomputed scale                        */
                float err_counts = err_mech * bif->rad_to_counts;
                bif->theta_start_offset = (uint32_t)(int32_t)lroundf(err_counts);

                /* Store for inspection if needed                              */
                bif->offset_err_elec         = err_elec;
                bif->offset_err_mech         = err_mech;
                bif->offset_err_mech_counts  = err_counts;

                bif->was_theta_start_offset_estimated = 1u;
            }
        }
    }
    else {
        bif->was_align_mode_entered           = 0u;
        bif->was_theta_start_offset_estimated = 0u;
    }

    /* ------------------------------------------------------------------
     * 3. Counts → radians  (precomputed scale)
     * ----------------------------------------------------------------*/
    float theta_meas_rad = (float)bif->theta_meas_mech * bif->counts_to_rad;
    bif->theta_meas_mech_rad = theta_meas_rad;      /* store for debug */

    /* ------------------------------------------------------------------
     * 4. Double-integrator update
     * ----------------------------------------------------------------*/
    float theta2  = bif->theta_mech_flt2;
    float omega2  = bif->omega_mech_flt2;

    float err     = angle_diff(theta_meas_rad, theta2);
    bif->err_mech_rad = err;                        /* store for debug */

    theta2 = theta2 + omega2 * bif->Ts + bif->k_L1 * err;
    omega2 = omega2 + bif->k_L2 * err;

    /* ------------------------------------------------------------------
     * 5. Wrap / limit
     * ----------------------------------------------------------------*/
    theta2 = wrap_2pi(theta2);

    /* Clamp omega – branchless friendly but kept readable                */
    if      (omega2 >  bif->omega_limit) omega2 =  bif->omega_limit;
    else if (omega2 < -bif->omega_limit) omega2 = -bif->omega_limit;

    bif->theta_mech_flt2 = theta2;
    bif->omega_mech_flt2 = omega2;

    /* ------------------------------------------------------------------
     * 6. Outputs
     * ----------------------------------------------------------------*/
    /* Electrical angle: wrap then shift to [-pi, +pi)                    */
    float theta_elec = wrap_2pi(theta2 * bif->pole_pairs) - F_PI;
    bif->theta_elec_flt = theta_elec;

    /* Mechanical angle: add precomputed correction, cheap single-step wrap */
    float theta_mech = theta2 + bif->mech_correction;
    if (theta_mech >= TWO_F_PI) theta_mech -= TWO_F_PI;
    theta_mech -= F_PI;                             /* → [-pi, +pi)       */
    bif->theta_mech_flt = theta_mech;

    /* Angular speeds                                                      */
    bif->omega_mech_flt = omega2;
    bif->omega_elec_flt = omega2 * bif->pole_pairs;
}