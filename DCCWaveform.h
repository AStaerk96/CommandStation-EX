/*
 *  © 2020, Chris Harlow. All rights reserved.
 *  © 2020, Harald Barth.
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
#ifndef DCCWaveform_h
#define DCCWaveform_h
#include "MotorDriver.h"


// Number of preamble bits.
const int   PREAMBLE_BITS_MAIN = 16;
const int   PREAMBLE_BITS_PROG = 22;
const byte   MAX_PACKET_SIZE = 5;  // NMRA standard extended packets, payload size WITHOUT checksum.

// The WAVE_STATE enum is deliberately numbered because a change of order would be catastrophic
// to the transform array.
enum  WAVE_STATE : byte {WAVE_START=0,WAVE_MID_1=1,WAVE_HIGH_0=2,WAVE_MID_0=3,WAVE_LOW_0=4,WAVE_PENDING=5};


// NOTE: static functions are used for the overall controller, then
// one instance is created for each track.

const byte idlePacket[] = {0xFF, 0x00, 0xFF};
const byte resetPacket[] = {0x00, 0x00, 0x00};

class DCCWaveform {
  public:
    DCCWaveform( byte preambleBits, bool isMain);
    static void begin(MotorDriver * mainDriver, MotorDriver * progDriver,
                        MotorDriver * booster1, MotorDriver * booster2, MotorDriver * booster3, MotorDriver * booster4);
    static void loop(bool ackManagerActive);
    static DCCWaveform   mainTrack;
    static DCCWaveform * progTrack;

    void beginTrack();
    void setPowerMode(POWERMODE mode);
    static void setBoosterPowerMode(byte boosterId,POWERMODE mode);
                 
    POWERMODE getPowerMode();
    static void describeGauges(Print * stream);
    static void listRawGauges(Print * stream);
    
    void checkPowerOverload(bool ackManagerActive);
    void schedulePacket(const byte buffer[], byte byteCount, byte repeats);
    volatile bool packetPending;
    volatile byte sentResetsSincePacket;
    volatile bool autoPowerOff=false;
    void setAckBaseline();  //prog track only
    void setAckPending();  //prog track only
    byte getAck();               //prog track only 0=NACK, 1=ACK 2=keep waiting
    static bool progTrackSyncMain;  // true when prog track is a siding switched to main
    static bool progTrackBoosted;   // true when prog track is not current limited
    static uint16_t lastGaugeTime;
    static uint16_t gaugeSampleTime; // millis between <g > responses 

    inline void doAutoPowerOff() {
	if (autoPowerOff) {
	    motorDriver->setPowerMode(POWERMODE::OFF);
	    autoPowerOff=false;
	}
    };
    inline bool canMeasureCurrent() {
      return motorDriver->canMeasureCurrent();
    };
    inline void setAckLimit(int mA) {
	ackLimitmA = mA;
    }
    inline void setMinAckPulseDuration(unsigned int i) {
	minAckPulseDuration = i;
    }
    inline void setMaxAckPulseDuration(unsigned int i) {
	maxAckPulseDuration = i;
    }

  private:
    
// For each state of the wave  nextState=stateTransform[currentState] 
   static const WAVE_STATE stateTransform[6];

// For each state of the wave, signal pin is HIGH or LOW   
   static const bool signalTransform[6];
  
    static void interruptHandler();
    static void interruptHandlerNoProgtrack();
    void interrupt2();
    void checkAck();
    
    bool isMainTrack;
    MotorDriver*  motorDriver;
    // Transmission controller
    byte transmitPacket[MAX_PACKET_SIZE+1]; // +1 for checksum
    byte transmitLength;
    byte transmitRepeats;      // remaining repeats of transmission
    byte remainingPreambles;
    byte requiredPreambles;
    byte bits_sent;           // 0-8 (yes 9 bits) sent for current byte
    byte bytes_sent;          // number of bytes sent from transmitPacket
    WAVE_STATE state;         // wave generator state machine
    byte pendingPacket[MAX_PACKET_SIZE+1]; // +1 for checksum
    byte pendingLength;
    byte pendingRepeats;
    
    // ACK management (Prog track only)  
    volatile bool ackPending;
    volatile bool ackDetected;
    int  ackThreshold; 
    int  ackLimitmA = 60;
    int ackMaxCurrent;
    unsigned long ackCheckStart; // millis
    unsigned int ackCheckDuration; // millis       
    
    unsigned int ackPulseDuration;  // micros
    unsigned long ackPulseStart; // micros

    unsigned int minAckPulseDuration = 2000; // micros
    unsigned int maxAckPulseDuration = 8500; // micros
           
};
#endif
