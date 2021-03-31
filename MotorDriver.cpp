/*
 *  © 2020, Chris Harlow. All rights reserved.
 *  
 *  This file is part of Asbelos DCC API
 *
 *  This is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  It is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with CommandStation.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <Arduino.h>
#include "MotorDriver.h"
#include "DCCTimer.h"
#include "DIAG.h"

#define setHIGH(fastpin)  *fastpin.inout |= fastpin.maskHIGH
#define setLOW(fastpin)   *fastpin.inout &= fastpin.maskLOW
#define isHIGH(fastpin)   (*fastpin.inout & fastpin.maskHIGH)
#define isLOW(fastpin)    (!isHIGH(fastpin))

bool MotorDriver::usePWM=false;
bool MotorDriver::commonFaultPin=false;

// Booster constructor.. no signal pins, just power control. 
MotorDriver::MotorDriver(byte power_pin, byte current_pin, float sense_factor, unsigned int trip_milliamps) {
  nextDriver=NULL;
  powerPin=power_pin;
  getFastPin(F("POWER"),powerPin,fastPowerPin);
  pinMode(powerPin, OUTPUT);

  currentPin=current_pin;
  if (currentPin!=UNUSED_PIN) {
    pinMode(currentPin, INPUT);
    senseOffset=analogRead(currentPin); // value of sensor at zero current
  }

  senseFactor=sense_factor;
  tripMilliamps=trip_milliamps;
  rawCurrentTripValue=(int)(trip_milliamps / sense_factor);
  lastCurrent=0;
  
  signalPin=UNUSED_PIN;
  signalPin2=UNUSED_PIN;
  brakePin=UNUSED_PIN;
  faultPin=UNUSED_PIN;
  
  if (currentPin==UNUSED_PIN) 
    DIAG(F("MotorDriver ** WARNING ** No current or short detection"));  
  else  
    DIAG(F("MotorDriver currentPin=A%d, senseOffset=%d, rawCurentTripValue(relative to offset)=%d"),
    currentPin-A0, senseOffset,rawCurrentTripValue);
}

// prog or main track constructor
MotorDriver::MotorDriver(byte power_pin, byte signal_pin, byte signal_pin2, int8_t brake_pin,
                         byte current_pin, float sense_factor, unsigned int trip_milliamps, byte fault_pin)
                         : MotorDriver(power_pin,current_pin,sense_factor, trip_milliamps) {
  
  signalPin=signal_pin;
  getFastPin(F("SIG"),signalPin,fastSignalPin);
  pinMode(signalPin, OUTPUT);
  
  signalPin2=signal_pin2;
  if (signalPin2!=UNUSED_PIN) {
    dualSignal=true;
    getFastPin(F("SIG2"),signalPin2,fastSignalPin2);
    pinMode(signalPin2, OUTPUT);
  }
  else dualSignal=false; 
  
  brakePin=brake_pin;
  if (brake_pin!=UNUSED_PIN){
    invertBrake=brake_pin < 0;
    brakePin=invertBrake ? 0-brake_pin : brake_pin;
    getFastPin(F("BRAKE"),brakePin,fastBrakePin);
    pinMode(brakePin, OUTPUT);
    setBrake(false);
  }
  else brakePin=UNUSED_PIN;
  
  faultPin=fault_pin;
  if (faultPin != UNUSED_PIN) {
    getFastPin(F("FAULT"),faultPin, 1 /*input*/, fastFaultPin);
    pinMode(faultPin, INPUT);
  }
  progTripValue = TRIP_CURRENT_PROG/sense_factor; // NMRA prog track default 250mA limit
  boosterId=0;   
}

void MotorDriver::addBooster(byte id, MotorDriver * booster) {
  booster->boosterId=id;
  booster->nextDriver=nextDriver;
  nextDriver=booster;
}

bool MotorDriver::isPWMCapable() {
    return (!dualSignal) && DCCTimer::isPWMPin(signalPin); 
}


void MotorDriver::setPower(bool on) {
  if (on) {
    // toggle brake before turning power on - resets overcurrent error
    // on the Pololu board if brake is wired to ^D2.
    setBrake(true);
    setBrake(false);
    setHIGH(fastPowerPin);
  }
  else setLOW(fastPowerPin);
}

// setBrake applies brake if on == true. So to get
// voltage from the motor bride one needs to do a
// setBrake(false).
// If the brakePin is negative that means the sense
// of the brake pin on the motor bridge is inverted
// (HIGH == release brake) and setBrake does
// compensate for that.
//
void MotorDriver::setBrake(bool on) {
  if (brakePin == UNUSED_PIN) return;
  if (on ^ invertBrake) setHIGH(fastBrakePin);
  else setLOW(fastBrakePin);
}

void MotorDriver::setSignal( bool high) {
   if (usePWM) {
    DCCTimer::setPWM(signalPin,high);
   }
   else {
     if (high) {
        setHIGH(fastSignalPin);
        if (dualSignal) setLOW(fastSignalPin2);
     }
     else {
        setLOW(fastSignalPin);
        if (dualSignal) setHIGH(fastSignalPin2);
     }
   }
}

#if defined(ARDUINO_TEENSY32) || defined(ARDUINO_TEENSY35)|| defined(ARDUINO_TEENSY36)
volatile unsigned int overflow_count=0;
#endif

bool MotorDriver::canMeasureCurrent() {
  return currentPin!=UNUSED_PIN;
}
/*
 * Return the current reading as pin reading 0 to 1023. If the fault
 * pin is activated return a negative current to show active fault pin.
 * As there is no -0, create a little and return -1 in that case.
 * 
 * senseOffset handles the case where a shield returns values above or below 
 * a central value depending on direction.
 */
int MotorDriver::getCurrentRaw() {
  if (currentPin==UNUSED_PIN) return 0; 
  int current;
#if defined(ARDUINO_TEENSY40) || defined(ARDUINO_TEENSY41)
  bool irq = disableInterrupts();
  current = analogRead(currentPin)-senseOffset;
  enableInterrupts(irq);
#elif defined(ARDUINO_TEENSY32) || defined(ARDUINO_TEENSY35)|| defined(ARDUINO_TEENSY36)
  unsigned char sreg_backup;
  sreg_backup = SREG;   /* save interrupt enable/disable state */
  cli();
  current = analogRead(currentPin)-senseOffset;
  overflow_count = 0;
  SREG = sreg_backup;    /* restore interrupt state */
#else
  current = analogRead(currentPin)-senseOffset;
#endif
  if (current<0) current=0-current;
  if ((faultPin != UNUSED_PIN)  && isLOW(fastFaultPin) && isHIGH(fastPowerPin))
      return (current == 0 ? -1 : -current);
  return current;
  // IMPORTANT:  This function can be called in Interrupt() time within the 56uS timer
  //             The default analogRead takes ~100uS which is catastrphic
  //             so DCCTimer has set the sample time to be much faster.  
}

unsigned int MotorDriver::raw2mA( int raw) {
  return (unsigned int)(raw * senseFactor);
}
int MotorDriver::mA2raw( unsigned int mA) {
  return (int)(mA / senseFactor);
}

void  MotorDriver::getFastPin(const FSH* type,int pin, bool input, FASTPIN & result) {
    // DIAG(F("MotorDriver %S Pin=%d,"),type,pin);
    (void) type; // avoid compiler warning if diag not used above. 
    uint8_t port = digitalPinToPort(pin);
    if (input)
      result.inout = portInputRegister(port);
    else
      result.inout = portOutputRegister(port);
    result.maskHIGH = digitalPinToBitMask(pin);
    result.maskLOW = ~result.maskHIGH;
    // DIAG(F(" port=0x%x, inoutpin=0x%x, isinput=%d, mask=0x%x"),port, result.inout,input,result.maskHIGH);
}


void MotorDriver::setPowerMode(POWERMODE mode) {
  powerMode=mode;
  setPower(mode == POWERMODE::ON);
}

// useProgTripValue is true on prog track unless ack in progress or joined to main 
void MotorDriver::checkPowerOverload(bool useProgTripValue) {
  if (!canMeasureCurrent()) return;
  uint16_t mymillis= millis();
  if (mymillis - lastSampleTaken  < sampleDelay) return;
  
  lastSampleTaken = mymillis;
  int tripValue= useProgTripValue? progTripValue : rawCurrentTripValue;
  lastCurrent=getCurrentRaw();
   
  switch (powerMode) {
    case POWERMODE::OFF:
      sampleDelay = POWER_SAMPLE_OFF_WAIT;
      break;
    case POWERMODE::ON:
      if (lastCurrent < tripValue) {
        sampleDelay = POWER_SAMPLE_ON_WAIT;
        if(power_good_counter<100) power_good_counter++;
        else  if (power_sample_overload_wait>POWER_SAMPLE_OVERLOAD_WAIT) power_sample_overload_wait=POWER_SAMPLE_OVERLOAD_WAIT;
        break;
        } 
      // We have an overload... it may be new or it may be because we are retyring  
      setPowerMode(POWERMODE::OVERLOAD);
      power_good_counter=0;
      sampleDelay = power_sample_overload_wait;

      // put a diag on the serial
      {
        unsigned int mA=raw2mA(lastCurrent);
        unsigned int maxmA=raw2mA(tripValue);
        if (boosterId==255) DIAG(F("*** PROG TRACK OVERLOAD current=%d/%dmA max=%d/%dmA  offtime=%dmS ***"),
                                 lastCurrent,mA, tripValue,maxmA, sampleDelay);
        else DIAG(F("*** TRACK %d OVERLOAD current=%d/%dmA max=%d/%dmA  offtime=%dmS ***"),
                  boosterId, lastCurrent,mA, tripValue,maxmA, sampleDelay);
      }
      power_sample_overload_wait = (power_sample_overload_wait >= 10000) ? 10000: power_sample_overload_wait * 2;
      break;
   
    case POWERMODE::OVERLOAD:
      // Try setting it back on after the OVERLOAD_WAIT
      setPowerMode(POWERMODE::ON);
      sampleDelay = POWER_SAMPLE_ON_WAIT;
      break;
      
    default:
      sampleDelay = 999; // cant get here..meaningless statement to avoid compiler warning.
  }
}


 void MotorDriver::describeGauge(Print * stream) {
    if (boosterId==255) StringFormatter::send(stream,F("<G 0 PROG %f %d mA>"),senseFactor,rawCurrentTripValue);
    else               StringFormatter::send(stream,F("<G %d TRACK%d %f %d mA>"),boosterId+1,boosterId,senseFactor,rawCurrentTripValue);  
 }

void MotorDriver::printRawCurrent(Print * stream) {
  if (powerMode==POWERMODE::OVERLOAD) stream->print(F("X "));
  else {
    stream->print(lastCurrent);
    stream->print(' '); 
   }
 }
   
/*****      
 *       FAULT PIN STUFF.... temporaraily removed for clarity 
 *if (lastCurrent < 0) {
    // We have a fault pin condition to take care of
    lastCurrent = -lastCurrent;
    setPowerMode(POWERMODE::OVERLOAD); // Turn off, decide later how fast to turn on again
    if (MotorDriver::commonFaultPin) {
        if (lastCurrent <= tripValue) {
    setPowerMode(POWERMODE::ON); // maybe other track
        }
        // Write this after the fact as we want to turn on as fast as possible
        // because we don't know which output actually triggered the fault pin
        DIAG(F("*** COMMON FAULT PIN ACTIVE - TOGGLED POWER on %S ***"), isMainTrack ? F("MAIN") : F("PROG"));
    } else {
        DIAG(F("*** %S FAULT PIN ACTIVE - OVERLOAD ***"), isMainTrack ? F("MAIN") : F("PROG"));
        if (lastCurrent < tripValue) {
      lastCurrent = tripValue; // exaggerate
        }
    }
      }
*************/      
