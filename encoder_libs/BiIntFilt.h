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

#ifndef BiIntFilt_H
#define BiIntFilt_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
   Double integrator filter for angle to clean angle and angular speed filter used in encoder-based FOC position control
   ============================================================*/

typedef struct
{
    /* ---- Configuration ---- */
    float Ts;                    // sec. Cycle time 1/f
    float k_L1;                    // Double integrator control parameter l1
    float k_L2;                 // Double integrator control parameter l2
    float pole_pairs;            // Number of pole pairs for mech->elec conversion
    float omega_limit;          // rad/s. Maximum speed that can be tracked
    float deadband;                // rad. Deadband (unused at the moment)
    uint32_t fixed_offset_mech;    // counts. Manually estimated mechanical alignment offset between encoder magnet and motor. 
                                // To estimated this, set to 0, keep motor in align mode (MOTOR_CTRL_ALIGN_TIME 200) 
                                // and note down what needs to be added to rotate motor to 0 (unused at the moment)

    /* ---- Internal values ---- */
    // Internal states 
    uint32_t theta_start_offset;                // counts. Automatically estimated mechanical offset to start electrical theta at pi/2
    uint8_t was_align_mode_entered;                // Marker to initialize theta offset calculation only once
    uint32_t last_align_mode_entered_time;      // Timestamp set when align mode is entered
    uint8_t was_theta_start_offset_estimated;    // Marker to check if theta start offset was already calculated
    
    // States needed for theta start offset calibration to pi/2
    uint32_t alignment_phase_time_ms; //ms. The time the system will stay in alignment phase. Half the time will be waited before the theta start offset is calculated and applied. The rest is used to wait for the filter to reach steady state.
    float offset_err_mech;
    float offset_err_elec;
    float offset_err_mech_counts;
    
    // Measured values / input
    uint16_t theta_meas_mech ; // counts
    float theta_meas_mech_rad; // rad. converted from theta_meas_mech

    // Control error
    float err_mech_rad       ; // rad. error between theta_meas_mech_rad and theta_mech_flt2
    
    // Filter internal value
    float omega_mech_flt2    ; // rad/s. controller intermediate value of angular speed
    float theta_mech_flt2    ; // rad. controller intermediate value of angle

    // Precomputed values
    float counts_to_rad;   // precomputed: TWO_F_PI / max_counts
    float rad_to_counts;   // precomputed: max_counts / TWO_F_PI
    float mech_correction; // precomputed: pi + (pi/2)/pole_pairs

    /* ---- Public values ---- */
    float theta_mech_flt     ; // rad. output mechanical angle
    float theta_elec_flt     ; // rad. output electrical angle
    float omega_mech_flt     ; // rad/s. output mechanical angular speed
    float omega_elec_flt     ; // rad/s. output electrical angular speed
} BiIntFilt_t;

void BiIntFilt_Init(BiIntFilt_t *bif,
                    float Ts,
                    float k_L1,
                    float k_L2,
                    float pole_pairs,
                    float omega_limit,
                    float deadband,
                    uint32_t fixed_offset_mech,
                    uint32_t alignment_phase_time_ms);

void BiIntFilt_Update(BiIntFilt_t *bif,
                      uint32_t     theta_raw,
                      uint8_t      isControlInAlignMode,
                      uint32_t     timeNowMs);

#ifdef __cplusplus
}
#endif

#endif