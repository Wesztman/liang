#ifndef _DEFINITIONS_H_
#define _DEFINITIONS_H_

#include "functional"
typedef std::function<void(int newMode)> ModeSelectEvent;


// BWF Code for inside and outside
#define INSIDE_BWF          86
#define OUTSIDE_BWF         5

#define LEFT_MOTOR_PWM_PIN 10
#define LEFT_MOTOR_DIRECTION_PIN 11
#define LEFT_MOTOR_PWM_CHANNEL 8

#define RIGHT_MOTOR_PWM_PIN 12
#define RIGHT_MOTOR_DIRECTION_PIN 13
#define RIGHT_MOTOR_PWM_CHANNEL 7

#define CUTTER_MOTOR_PWM_PIN 14
#define CUTTER_MOTOR_PWM_CHANNEL 6

#define MPU_INTERRUPT_PIN 33

#define LEFT_SENSOR_PIN 15
#define RIGHT_SENSOR_PIN 16

#define BATTERY_SENSOR_PIN 33
#define BATTERY_CHARGE_PIN 18

#define BUMPER_PIN 18


#define CUTTER_SPEED 100

#define TILT_ANGLE 55;

#endif