#include "sensor.h"
#include "Arduino.h"
#include "definitions.h"

#define INSIDE    1
#define NOSIGNAL  0
#define OUTSIDE   -1

#define SIGNAL_VALIDITY_TIME_MS 120000
#define PULSE_UNIT_LENGTH 100
#define BWF_FILTER_TIME_MS 300


int SENSOR::outside_code[] = {OUTSIDE_BWF};
int SENSOR::inside_code[] = {INSIDE_BWF};

SENSOR::SENSOR(String name_, int pin_, bool missingSignalIsOut_, int signalValidityTimeMs_, LOGGER *logger_){
    name = name_;
    pin = pin_;
    missingSignalIsOut = missingSignalIsOut_;
    pulseHistoryPos = 0;
    pulseCount = 0;
    signalValidityTimeMs = signalValidityTimeMs_;
    logger = logger_;
    for (size_t i = 0; i < PULSE_HISTORY_COUNT; i++)
    {
      pulsehistory[i] = 0;
    }
    lastResultWasOutOfBounds = false;
}

void SENSOR::setup() {
  pinMode(pin, INPUT);
//  logger->log("Sensor setup for pin: " + String(pin));
  lastSignalTime = millis();
  isIn = !missingSignalIsOut;
}

void SENSOR::handleInterrupt() {
  unsigned long now = micros();
  pulseCount++;

  // Calculate the time since last pulse
  int time_since_pulse = int(now - last_pulse);
  last_pulse = now;

  // Convert to pulse units (rounding up)
  int pulse_length = (time_since_pulse+(PULSE_UNIT_LENGTH/2)) / PULSE_UNIT_LENGTH;

   pulseHistoryPos++;
   pulseHistoryPos = pulseHistoryPos % PULSE_HISTORY_COUNT;
   pulsehistory[pulseHistoryPos] = pulse_length;

  // Check if the latest pulse fits the code for inside
  if (abs(pulse_length-inside_code[pulse_count_inside]) < 2) {
    pulse_count_inside++;

    // Check if the entire pulse train has been batched
    if (pulse_count_inside >= sizeof(inside_code)/sizeof(inside_code[0])) {
      lastSignalTime = millis();
      isIn = true;
      pulse_count_outside=0;
    }
  } else {
    pulse_count_inside=0;
  }

  // Check if the latest pulse fits the code for outside
  if (abs(pulse_length-outside_code[pulse_count_outside]) < 2) {
    pulse_count_outside++;
    if (pulse_count_outside >= sizeof(outside_code)/sizeof(outside_code[0])) {
      lastSignalTime = millis();
      isIn = false;
      pulse_count_inside=0;
    }
  } else {
    pulse_count_outside=0;
  }



  // Store the received code for debug output
  // arr[arr_count++] = pulse_length;
  // if (arr_count>arr_length) arr_count=0;
}

// bool SENSOR::IsIn() {
//     return isIn && !IsSignalMissing();
// }

bool SENSOR::IsIn() {
    static unsigned long startTime = 0; // Static variable to store the start time
    unsigned long currentTime = millis(); // Get the current time in milliseconds

    if (isIn && !IsSignalMissing()) { // Check if the conditions are met
        if (startTime == 0) { // If this is the first time the conditions are met, set the start time
            startTime = currentTime;
        }
        if (currentTime - startTime >= BWF_FILTER_TIME_MS) { // If the filter time has passed, return true
            return true;
        }
    } else { // If the conditions are not met, reset the start time
        startTime = 0;
    }

    return false; // If the filter time has not passed, return false
}

bool SENSOR::IsOut() {
    return !isIn;
}

bool SENSOR::IsOutOfBounds() {
    bool isSigMiss = IsSignalMissing();
    bool result = isSigMiss ? missingSignalIsOut : IsOut();
    if (lastResultWasOutOfBounds != result) {
      lastResultWasOutOfBounds = result;
      if (logSensorChanges){
        String s = isSigMiss ? " Missing signal. Last ok: " + String(lastSignalTime)  : "";
        //logger->log("a");
        logger->log(name +  (result ? " Out" : " In ") + s);
        //logger->log("b");
      }
    }
    return result;
}

bool SENSOR::IsSignalMissing() {
  if (signalValidityTimeMs <= 0) return false;
  return hasTimeout(lastSignalTime, signalValidityTimeMs);
}

void SENSOR::SetLogSensorChanges(bool logChanges) {
  logSensorChanges = logChanges;
}

String SENSOR::GetPulseHistoryS() {
  String result = "";
  try
  {

  for (size_t i = 0; i < PULSE_HISTORY_COUNT; i++)
    {
      result = result + " " + String(pulsehistory[i]);
    }
    result = result + " Last ok: " + String(lastSignalTime);

  }
  catch(const std::exception& e)
  {
    result = e.what();
  }

   return result;
}