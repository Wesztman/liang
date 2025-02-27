#include <math.h>
#include "ArduinoOTA.h"
#include "updatehandler.h"
#include "WiFi.h"
#include "Wire.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include "MPU6050_light.h"

#include "operational_modes/operationalmode.h"
#include "operational_modes/OpModeCharge.h"
#include "operational_modes/OpModeMow.h"
#include "operational_modes/OpModeMowOnce.h"
#include "operational_modes/OpModeIdle.h"
#include "operational_modes/OpModeError.h"
#include "operational_modes/OpModeUpgrade.h"

#include "behaviors/behavior.h"
#include "behaviors/charge.h"
#include "behaviors/followbwf.h"
#include "behaviors/goaround.h"
#include "behaviors/idle.h"
#include "behaviors/error.h"
#include "behaviors/launch.h"
#include "behaviors/lookforbwf.h"
#include "behaviors/mow.h"
#include "behaviors/sensordebug.h"
#include "behaviors/motordebug.h"

#include "interaction/presenter.h"
#include "logevent.h"

#include "interaction/display.h"
#include "interaction/webui.h"
#include "interaction/serialui.h"
#include "interaction/hardwarebutton.h"

#include "Controller.h"
#include "definitions.h"
#include "mowermodel.h"
#include "sensor.h"
#include "battery.h"
#include "gyro.h"

#include "utils.h"
#include "settings.h"


//#define INTERRUPT_ATTR DRAM_ATTR
#define BUFFER_LENGTH 32
#define __arm__
#define __PGMSPACE_H_


unsigned long lastPrint= 0;
int expectedMode = 0;
int expectedBehavior = 0;
int manualMode = -1;
bool rebootIsNeeded = false;
unsigned long rebootIsNeededSince;

volatile bool updateInProgress = false;
volatile bool pollTaskInitDone = false;
bool fullModelResendRequired = true;

TaskHandle_t pollTask;

MowerModel mowerModel;


void updateEvent(int percentDone_) {
  Serial.println("Update started");

detachInterrupt(LEFT_SENSOR_PIN);
detachInterrupt(RIGHT_SENSOR_PIN);
  updateInProgress = true;
}

void setManualMode(int manualMode_);

void setRebootNeeded() {
  rebootIsNeeded = true;
  rebootIsNeededSince = millis();
  manualMode = OP_MODE_UPGRADE;
}

SETTINGS settings;
MPU6050 mpu(Wire);
AsyncWebServer webServer(80);
AsyncWebSocket webSocket("/ws");
MOWERDISPLAY display;
SERIALUI serialUi(*setManualMode);
WEBUI webUi(&webServer, &webSocket, *setManualMode, *setRebootNeeded, &settings);



std::vector<PRESENTER*> presenters = {
  &serialUi,
  &display,
  &webUi,
};


LOGGER logger(presenters);
GYRO gyro(&logger, &mpu);
SENSOR leftSensor("Left", LEFT_SENSOR_PIN, MISSING_SIGNAL_IS_OUT, PULSE_SIGNAL_VALID_MS, &logger);
SENSOR rightSensor("Right", RIGHT_SENSOR_PIN, MISSING_SIGNAL_IS_OUT, PULSE_SIGNAL_VALID_MS, &logger);
BATTERY battery(BATTERY_SENSOR_PIN, BATTERY_CHARGE_PIN);
BUMPER bumper(BUMPER_PIN);
HardwareButton bootButton(SWITCH_BOOT_PIN, &logger);
HardwareButton sw3Button(SWITCH_3_PIN, &logger);

void setManualMode(int manualMode_) {
  manualMode = manualMode_;
}


MOTOR leftMotor(LEFT_MOTOR_SENSE_PIN, LEFT_MOTOR_FORWARD_PWM_PIN, LEFT_MOTOR_BACKWARDS_PWM_PIN, LEFT_MOTOR_PWM_CHANNEL_FORWARD, LEFT_MOTOR_PWM_CHANNEL_BACKWARDS, LOAD_START_IGNORE_TIME, &logger, "Left");
MOTOR rightMotor(RIGHT_MOTOR_SENSE_PIN, RIGHT_MOTOR_FORWARD_PWM_PIN, RIGHT_MOTOR_BACKWARDS_PWM_PIN, RIGHT_MOTOR_PWM_CHANNEL_FORWARD, RIGHT_MOTOR_PWM_CHANNEL_BACKWARDS, LOAD_START_IGNORE_TIME, &logger, "Right");
MOTOR cutterMotor(CUTTER_MOTOR_SENSE_PIN, CUTTER_MOTOR_FORWARD_PWM_PIN, CUTTER_MOTOR_BACKWARDS_PWM_PIN, CUTTER_MOTOR_PWM_CHANNEL_FORWARD, CUTTER_MOTOR_PWM_CHANNEL_BACKWARDS, LOAD_START_IGNORE_TIME, &logger, "Cutter");

Controller controller(&leftMotor, &rightMotor, &cutterMotor, &gyro, &bumper, &leftSensor, &rightSensor, &logger);

UPDATEHANDLER uh(&logger, &controller, *updateEvent);

OpModeCharge opModeCharge(&controller, &logger, &battery);
OpModeMow opModeMow(&controller, &logger, &battery);
OpModeMowOnce opModeMowOnce(&controller, &logger, &battery);
OpModeIdle opModeIdle(&controller, &logger);
OpModeError opModeError(&controller, &logger);
OpModeUpgrade opModeUpgrade(&controller, &logger);

OPERATIONALMODE* availableOpModes[] = {
  &opModeIdle,
  &opModeError,
  &opModeMow,
  &opModeMowOnce,
  &opModeCharge,
  &opModeUpgrade,
};
OPERATIONALMODE* currentMode = availableOpModes[0];


Charge charge(&controller, &logger, &battery, &mowerModel);
FollowBWF followBWF(&controller, &logger, &battery, &leftMotor, &rightMotor);
GoAround goAround(&controller, &logger, &battery, &leftSensor, &rightSensor);
Idle idle(&controller, &logger, &battery, &sw3Button);
Error error(&controller, &logger, &battery, &mowerModel);
SensorDebug sensorDebug(&controller, &logger, &battery, &leftSensor, &rightSensor, &sw3Button);
MotorDebug motorDebug(&controller, &logger, &battery, &leftSensor, &rightSensor, &sw3Button);
Launch launch(&controller, &logger, &battery);
LookForBWF lookForBwf(&controller, &logger, &battery);
Mow mow(&controller, &logger, &battery, &mowerModel, *setManualMode);


BEHAVIOR* currentBehavior;
BEHAVIOR* availableBehaviors[] = {
  &charge,
  &followBWF,
  &goAround,
  &idle,
  &launch,
  &lookForBwf,
  &mow,
  &sensorDebug,
  &motorDebug,
  &error,
};


void handleInterruptLeft() {
  leftSensor.handleInterrupt();
}

void handleInterruptRight() {
  rightSensor.handleInterrupt();
}


void pollPollables(void * parameter) {
  //Wire must be begin():ed on the same core it's going to be used
  Wire.begin();

  display.setup();
  logger.log("BOOTING");
  gyro.setup();
  battery.resetVoltage();
  pollTaskInitDone = true;
  while (!updateInProgress)
  {
    if (hasTimeout(lastPrint, 200)) {
      bool useFullResend = fullModelResendRequired;
      fullModelResendRequired = false;
      lastPrint = millis();
      for (size_t i = 0; i < presenters.size(); i++)
      {
        presenters[i]->PresentMowerModel(&mowerModel, useFullResend);
      }
    }
    // bootButton.check();
    sw3Button.check();


    if (bootButton.GetConsumablePress()) {
        if (VERBOSE_LOGGING) {
            logger.log("Boot button pressed");
        }
        int baseMode = manualMode >= 0 ? manualMode : currentMode->id();
        int newMode = baseMode + 1;
        if (newMode > MAX_MANUAL_OPMODE) {
          newMode = 0;
        }
        setManualMode(newMode);
        logger.log("New mode: " + String(newMode));

    }

    bumper.doLoop();
    if (bumper.IsStuck()) {
      controller.SetError(ERROR_BUMPER_STUCK);
    }

    leftMotor.doLoop();
    mowerModel.LeftMotorLoad = leftMotor.getLoad();
    mowerModel.LeftMotorSpeed = leftMotor.getSpeed();

    rightMotor.doLoop();
    mowerModel.RightMotorLoad = rightMotor.getLoad();
    mowerModel.RightMotorSpeed = rightMotor.getSpeed();

    cutterMotor.doLoop();
    mowerModel.CutterMotorLoad = cutterMotor.getLoad();
    mowerModel.CutterMotorSpeed = cutterMotor.getSpeed();

    gyro.loop();
    mowerModel.Tilt = gyro.getAngleYFiltered();
    mowerModel.Heading = gyro.getHeading();
    mowerModel.Acceleration = gyro.getAcceleration();
    mowerModel.speed = gyro.getSpeed();
    mowerModel.distanceTravelled = gyro.getTravelledDistance();

    mowerModel.BatteryVoltage = battery.updateVoltage();
    mowerModel.IsDocked = battery.isBeingCharged();

    mowerModel.LeftSensorIsOutOfBounds = leftSensor.IsOutOfBounds();
    mowerModel.RightSensorIsOutOfBounds = rightSensor.IsOutOfBounds();

    yield();
  }


  while (true) //Just chill and don't return while waiting for update
  {
    if (hasTimeout(lastPrint, 200)) {

      lastPrint = millis();
      for (size_t i = 0; i < presenters.size(); i++)
      {
        presenters[i]->PresentMowerModel(&mowerModel, fullModelResendRequired);
      }
    }
    delay(10);
  }
}

void setupWifi() {
  mowerModel.Behavior = "WIFI";

  String ssid =  settings.readWifiSid();
  String pwd = settings.readWifiPwd();

  logger.log("SSID: " + ssid);

  bool isWifiConfigured = false;
  if (ssid.length() > 0) {
    isWifiConfigured = true;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pwd.c_str());
  }

  if (!isWifiConfigured || WiFi.waitForConnectResult() != WL_CONNECTED) {
    WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      IPAddress IP(10, 0, 2, 1);
      IPAddress gateway(10, 0, 2, 1);
      IPAddress NMask(255, 255, 255, 0);
      WiFi.softAPConfig(IP, gateway, NMask);
      WiFi.softAP(String("Liang. "+ WiFi.softAPIP().toString() + "/settings.html").c_str());
      logger.log("Connect to WiFi Liang and use http://" + WiFi.softAPIP().toString() + "/settings.html to edit wifi settings");
      delay(5000);
      logger.log("IP: " + WiFi.softAPIP().toString());
  } else {
    logger.log("IP: " + WiFi.localIP().toString());
  }
  delay(1000);
}

void setup() {
  Serial.begin(115200);
  settings.setup(&logger);

  analogReadResolution(ANALOG_RESOLUTION);
  analogSetAttenuation(ADC_11db);


  webUi.SetLogger(&logger);


  mowerModel.OpMode = "Booting";
  mowerModel.Behavior = "Polltask";

  xTaskCreatePinnedToCore(pollPollables, "pollTask", 8192, NULL, 5, &pollTask, 1);

  while(!pollTaskInitDone) {
    delay(1);
  }
  mowerModel.Behavior = "SPIFFS";
  if(!SPIFFS.begin()){
    logger.log("SPIFFS failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  if (!EEPROM.begin(64)) {
    logger.log("Failed to initialise EEPROM, restarting...");
    delay(5000);
    ESP.restart();
  }

  // EEPROM.writeString(EEPROM_ADR_WIFI_SSID, WIFI_SSID);
  // EEPROM.writeString(EEPROM_ADR_WIFI_PWD, WIFI_PASS);
  // EEPROM.commit();

  setupWifi();

  mowerModel.Behavior = "Setup";

  uh.setup();
  webUi.setup();

  battery.setup();
  bumper.setup();

  leftSensor.setup();
  attachInterrupt(LEFT_SENSOR_PIN, handleInterruptLeft, RISING);

  rightSensor.setup();
  attachInterrupt(RIGHT_SENSOR_PIN, handleInterruptRight, RISING);

  leftMotor.setup(settings.readWheelMotorLoadLimit());
  rightMotor.setup(settings.readWheelMotorLoadLimit());
  cutterMotor.setup(settings.readCutterLoadLimit());

  mowerModel.Behavior = "Starting";

  delay(1000);

  expectedMode = EEPROM.readInt(EEPROM_ADR_INIT_MODE);
  expectedBehavior = currentMode->start();
  currentBehavior = availableBehaviors[0]; //Just something
  mowerModel.OpMode = currentMode->desc();
  mowerModel.CurrentOpModeId = currentMode->id();
  mowerModel.Behavior = currentBehavior->desc();

  pinMode(LED_PIN, OUTPUT);
  pinMode(SWITCH_3_PIN, INPUT_PULLUP);
  pinMode(SWITCH_BOOT_PIN, INPUT_PULLUP);
}



void loop() {
  //digitalWrite(LED_PIN, (millis() % 2000) < 300);
  //digitalWrite(LED_PIN, bumper.IsBumped());
  //digitalWrite(LED_PIN, (digitalRead(SWITCH_3_PIN) == LOW));
  //digitalWrite(LED_PIN, battery.isBeingCharged());

  if (rebootIsNeeded && hasTimeout(rebootIsNeededSince, 2000)) {
    ESP.restart();
  }

  digitalWrite(LED_PIN,
    bootButton.IsPressed()
    || sw3Button.IsPressed()
    || bumper.IsBumped());


  //Handle
  uh.doLoop();
  webUi.doLoop();




  if (controller.IsFlipped()) {
    controller.SetError(ERROR_FLIPPED);
  }

  if (manualMode >= 0) {
    expectedMode = manualMode;
    manualMode = -1;
  } else if(controller.GetError() != ERROR_NOERROR) {
    expectedMode = OP_MODE_ERROR;
  }


  if (expectedMode != currentMode->id()) {
    int c = sizeof(availableOpModes) / sizeof(availableOpModes[0]);
    bool foundIt = false;
    for (int i = 0; i < c; i++)
    {
      if (availableOpModes[i]->id() == expectedMode) {
        foundIt = true;
        currentMode = availableOpModes[i];
        logger.log("Enter mode: " + currentMode->desc());
        mowerModel.OpMode = currentMode->desc();
        mowerModel.CurrentOpModeId = currentMode->id();
        expectedBehavior = currentMode->start();
        controller.FreezeTargetHeading();
        controller.ResetOutOfBoundsTimout();
        controller.ResetFailedTurnsCount();
        break;
      }
    }

    if (!foundIt) {
      logger.log("Could not locate OpMode with id:" + String(expectedMode));
      controller.SetError(ERROR_INVALID_OP_MODE);
    }
  }


  if (expectedBehavior != currentBehavior->id()) {
    int c = sizeof(availableBehaviors) / sizeof(availableBehaviors[0]);
    bool foundIt = false;
    for (int i = 0; i < c; i++)
    {
      if (availableBehaviors[i]->id() == expectedBehavior) {
        foundIt = true;
        currentBehavior = availableBehaviors[i];
        logger.log("Enter behavior: " + currentBehavior->desc());
        mowerModel.Behavior = currentBehavior->desc();
        currentBehavior->start();
        leftSensor.SetLogSensorChanges(currentBehavior->logSensorChange());
        rightSensor.SetLogSensorChanges(currentBehavior->logSensorChange());
        break;
      }
    }

    if (!foundIt) {
      logger.log("Could not locate behavior with id:" + String(expectedBehavior));
      controller.SetError(ERROR_INVALID_BAHAVIOR);
    }
  }

  expectedMode = currentMode->loop();
  expectedBehavior = currentBehavior->loop();
  delay(0);
}