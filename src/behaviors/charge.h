#ifndef _BEHAVIOUR_CHARGE_H_
#define _BEHAVIOUR_CHARGE_H_

#include "behavior.h"
#include "Controller.h"
#include "Logger.h"
#include "battery.h"
#include "operational_modes/operationalmode.h"
#include "definitions.h"


class Charge : public BEHAVIOR { 
    public:
        Charge(Controller *controller_, LOGGER *logger_, BATTERY *battery_, OPERATIONALMODE *currentMode_);
        void start();
        int loop();
        int id();
        String desc();
    private:
        Controller *controller;
        LOGGER *logger;
        BATTERY *battery;
        OPERATIONALMODE *currentMode;
};

#endif