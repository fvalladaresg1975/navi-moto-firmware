/*
    Copyright (c) 2024 Felix Biego. All rights reserved.
    This work is licensed under the terms of the MIT license.  
    For a copy, see <https://opensource.org/licenses/MIT>.
*/


#ifndef _COMMON_API_H
#define _COMMON_API_H

#ifdef __cplusplus
extern "C"
{
#endif


typedef struct imu_data {
    bool success;
    float ax;
    float ay;
    float az;
    float gx;
    float gy;
    float gz;
    float temp;
} imu_data_t;


void imu_init();
imu_data_t get_imu_data();
void imu_close();

/* Asks the phone to place a call to the contact at [index] in the list the phone itself
   synced to the watch (so the index is enough - no need to send the number back).
   Implemented in the HAL since it needs the BLE `watch` object. */
void dial_contact(int index);

/* Asks the phone to end the current Google Maps navigation. The watch never ends it
   locally: navigation lives in Maps, so the phone fires the "Salir de la navegación"
   action on Maps' own notification and the watch then leaves the screen through the
   normal navigation-stop path. Implemented in the HAL since it needs the BLE `watch`. */
void stop_navigation(void);

/* Statistics for the most recently finished navigation, persisted across reboots.
   Both are zero when no trip has been recorded yet. See the implementation in the HAL
   for how the distance is derived and why it is an estimate. */
void get_last_trip(long *meters, unsigned long *seconds);


#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif