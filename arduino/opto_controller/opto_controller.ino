#include "config.h"
#include "Adafruit_MCP4725.h"



// initializations
const uint8_t sinLookup[] = {0,0,0,0,0,0,0,0,1,1,1,1,1,2,2,2,3,3,3,4,4,4,5,5,6,6,7,7,8,8,9,10,10,11,12,12,13,14,14,15,16,17,17,18,19,20,21,22,23,24,24,25,26,27,28,29,30,31,33,34,35,36,37,38,39,40,42,43,44,45,46,48,49,50,51,53,54,55,57,58,59,61,62,64,65,66,68,69,71,72,73,75,76,78,79,81,82,84,85,87,88,90,91,93,95,96,98,99,101,102,104,106,107,109,110,112,113,115,117,118,120,121,123,125,126,128,130,131,133,134,136,138,139,141,142,144,146,147,149,150,152,153,155,157,158,160,161,163,164,166,167,169,170,172,173,175,176,178,179,181,182,184,185,187,188,189,191,192,194,195,196,198,199,200,202,203,204,205,207,208,209,210,212,213,214,215,216,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,236,237,238,239,240,240,241,242,242,243,244,244,245,246,246,247,247,248,248,249,249,250,250,251,251,252,252,252,253,253,253,253,254,254,254,254,254,255,255,255,255,255,255,255,255,255,255,255,255,255,255,254,254,254,254,254,253,253,253,253,252,252,252,251,251,250,250,249,249,248,248,247,247,246,246,245,244,244,243,242,242,241,240,240,239,238,237,236,236,235,234,233,232,231,230,229,228,227,226,225,224,223,222,221,220,219,218,216,215,214,213,212,210,209,208,207,205,204,203,202,200,199,198,196,195,194,192,191,189,188,187,185,184,182,181,179,178,176,175,173,172,170,169,167,166,164,163,161,160,158,157,155,153,152,150,149,147,146,144,142,141,139,138,136,134,133,131,130,128,126,125,123,121,120,118,117,115,113,112,110,109,107,106,104,102,101,99,98,96,95,93,91,90,88,87,85,84,82,81,79,78,76,75,73,72,71,69,68,66,65,64,62,61,59,58,57,55,54,53,51,50,49,48,46,45,44,43,42,40,39,38,37,36,35,34,33,31,30,29,28,27,26,25,24,24,23,22,21,20,19,18,17,17,16,15,14,14,13,12,12,11,10,10,9,8,8,7,7,6,6,5,5,4,4,4,3,3,3,2,2,2,1,1,1,1,1,0,0,0,0,0,0,0,0};
const int fs = 1000; // sampling frequency (don't change this dummy!)
const int sinSmps = sizeof(sinLookup);
volatile float deltaIndex; // how far to advance in the lookup table for every clock tick
volatile float interPulseInterval;
volatile float sinIndex = 0;
volatile float bitConversion = 4095 / 255;
volatile bool updated = false; // keeps track of whether the desired value for the ADC has been updated
volatile uint16_t oldValue = 0;
volatile uint16_t newValue = 0;
volatile long signalTimer = signalDuration;  // timer for the duration of the signal // initialize the timer at a high value - setting it to 0 causes the stimulus to be delivered
volatile long pulseTimer = 0; // keeps track of interstimulus interval in the pulse condition
volatile bool isSignalOn = false;
volatile int userInput;
Adafruit_MCP4725 dac;





void setup() {

  pinMode(triggerPin, INPUT);
  pinMode(referencePin, OUTPUT);
  analogWrite(referencePin, 0);
  
  // initialize timer interrupt
  cli();//disable interrupts
  TCCR0A = 0;// set entire TCCR0A register to 0
  TCCR0B = 0;// same for TCCR0B
  TCNT0  = 0;//initialize counter value to 0
  OCR0A = 249; // set compare match register for 2khz increments
  TCCR0A |= (1 << WGM01); // turn on CTC mode
  TCCR0B |= (1 << CS01) | (1 << CS00); // Set CS01 and CS00 bits for 64 prescaler
  TIMSK0 |= (1 << OCIE0A); // enable timer compare interrupt
  sei();//enable interrupts

  TWBR = 12; // speed up i2c communication
  dac.begin(0x62); // begin communication with DAC
  dac.setVoltage(0, false);
  randomSeed(analogRead(0)); // initialize random seed
  attachInterrupt(digitalPinToInterrupt(triggerPin), stimulusOnOff  , CHANGE);
  Serial.begin(115200);
  setFrequency();
  showMenu();
  
}



void loop(){
  
  // update LED output if value has changed
  if (updated){
    dac.setVoltage(constrain(round(newValue*bitConversion*signalPower), 0, 4095), false);
    analogWrite(referencePin, newValue);
    updated = false;
  }

  getUserInput();
}
