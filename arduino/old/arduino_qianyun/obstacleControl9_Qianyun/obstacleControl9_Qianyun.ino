// OBSTACLE CONTROL
#include "config.h"
#include <RunningMedian.h>




// initializations
const float mmPerMotorTic = ((2*PI*timingPulleyRad) / (motorSteps*microStepping));
const int startPositionBuffer = startPositionMm * ((motorSteps*microStepping) / (2*PI*timingPulleyRad));
const int endPositionBuffer = endPositionMm  * ((motorSteps*microStepping) / (2*PI*timingPulleyRad)); // motor stops endPositionBuffer steps before the beginning and end of the track
volatile int stepperDelays[delayLookupLength];
char* conditionNames[] = {"rewards only", "platform, no obstacles", "platform, with obstacles"};
volatile int userInput;
volatile int wheelTicks = 0;
volatile int wheelTicksTemp = 0; // this variable temporarily copies wheelTicks in the main code to avoid having to access it multiple times, potentially colliding with its access in the interrupt
volatile int obstacleInd = 0; // keeps track of which obstacle is being delivered for each reward trial
volatile int stepperTicks = 0;
volatile int stepperStopPosition; // value to be determined by call to initializeLimits in setup()
volatile int targetStepperTicks = startPositionBuffer;
volatile int stepsToTake = 0; // when driving the motor, stepsToTake is how many motor ticks required to get to target position
volatile int rewardPosition = (rewardRotations * encoderSteps) / obsGain; // expressed in wheelTicks
const double conversionFactor = (wheelRad / timingPulleyRad) * (float(motorSteps) / encoderSteps) * microStepping; // this converts from wheel encoder tics to desired number of steps in stepper driver
volatile bool touchSensorOn = false;
volatile bool stepDir = HIGH;
volatile bool obstacleEngaged = false;
volatile long stepperDelayInd = 0;
volatile long maxStepperDelayInd;
volatile int stepDelay;
volatile long currentMicros = 0;
volatile long lastMicros = 0;
volatile int deltaMicros = 0;
volatile int targetStepDelay = 0;
volatile int callibrationDelay; // tbd in setup
volatile int maxSpeedDelay; // tbd in setup
volatile int maxCallibrationDelay; // tbd in setup
volatile int startingWheelTics = 0;
volatile int startingStepperTics = 0;
volatile int obstacleLocationSteps[sizeof(obstacleLocations)];
volatile int obsPos; // position of obstacle, in wheel tics, on a given trial
volatile float stepperSpeed;
const float touchSensorOnSteps = touchSensorOnLimits[0] * ((microStepping*motorSteps) / (2*PI*timingPulleyRad));
const float touchSensorOffSteps = touchSensorOnLimits[1] * ((microStepping*motorSteps) / (2*PI*timingPulleyRad));
volatile int ticsBeforeStartLimitToSlowDown; // tbd in setup
RunningMedian deltaMicroSmps = RunningMedian(5);
volatile bool giveLaserPulseAlready = false;
const int laserStartRotation = 1.5; //wheelticks
const float laserEndRotation = 8.0; //wheelticks
volatile int LaserPulsePosition = 11520; //wheelticks
volatile int laserStartTicks;
volatile int laserEndTicks;

void setup() {
  
  // prepare ins and outs
  pinMode(stepPin, OUTPUT);
  pinMode(stepDirPin, OUTPUT);
  pinMode(waterPin, OUTPUT);
  pinMode(motorOffPin, OUTPUT);
  pinMode(obstaclePin, OUTPUT);
  pinMode(laserPin, OUTPUT);
  pinMode(startLimitPin, INPUT_PULLUP);
  pinMode(stopLimitPin, INPUT_PULLUP);
  pinMode(obsLightPin, OUTPUT);
  pinMode(obsLightPin2, OUTPUT);
  pinMode(obsOnPin, OUTPUT);
  pinMode(touchSensorOnPin, OUTPUT);
  
  digitalWrite(stepPin, LOW);
  digitalWrite(stepDirPin, stepDir);
  digitalWrite(waterPin, LOW);
  digitalWrite(motorOffPin, LOW);
  digitalWrite(obstaclePin, LOW);
  digitalWrite(laserPin, LOW);
  digitalWrite(obsLightPin, LOW);
  digitalWrite(obsLightPin2, LOW);
  digitalWrite(obsOnPin, LOW);
  digitalWrite(touchSensorOnPin, LOW);


  // initialiez random seed
  randomSeed(analogRead(0));
  

  // initialize encoder hardware interrupt
  attachInterrupt(digitalPinToInterrupt(encoderPinA), encoder_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encoderPinB), encoder_isr, CHANGE);

  
  // begin serial communication
  Serial.begin(115200);
  printMenuAndSettings();
  
  
  // initialize lookup table for stepper speeds
  stepperSpeed = 0;
  for (int i=0; i<delayLookupLength; i++){
    stepperSpeed = 0.5 * (stepperSpeed + sqrt(4*acceleration*mmPerMotorTic/1000 + pow(stepperSpeed,2)));
    stepperDelays[i] = getMotorDelayFromSpeed(stepperSpeed);
    if (stepperSpeed>maxStepperSpeed){
      maxStepperDelayInd = i;
      break;
    }
  }

  callibrationDelay = getMotorDelayFromSpeed(callibrationSpeed);
  maxSpeedDelay = getMotorDelayFromSpeed(maxStepperSpeed);
  maxCallibrationDelay = getMotorDelayFromSpeed(minCallibrationSpeed);
  ticsBeforeStartLimitToSlowDown = getDecellerationDistance(minCallibrationSpeed, callibrationSpeed, acceleration) / mmPerMotorTic;
  
  if (maxStepperDelayInd==0){
    Serial.println("WARNING: Too many values in stepper delay lookup!");
    Serial.println("Try increasing acceleration or decreasing max stepper velocity...");
  }

  
  // initialize locations of obstacles
  initializeObsLocations();
  
  // initialize track limits and move to starting position
  initializeLimits(.25);
}




void loop(){

  // check for user input
  getUserInput();
  
  
  // store current wheelTicks value with interrupts disabled
  noInterrupts();
  wheelTicksTemp = wheelTicks;
  interrupts();

  
  // check if obstacle should be engaged or disengaged  
  
  // engage obstacle:
  if (!obstacleEngaged & (wheelTicksTemp >= obsPos)){

    switch (state){
      
      // platform, no obstacles
      case 2:
        obstacleEngaged = true;
        digitalWrite(motorOffPin, LOW); // engages stepper motor driver
        startTracking();
        break;

      // platform, with obstacles
      case 3:
        obstacleEngaged = true;
        digitalWrite(motorOffPin, LOW); // engages stepper motor driver
        digitalWrite(obsOnPin, HIGH);
        startTracking();
        break;
      }
  }
   
  // turn on touch sensor
  else if (obstacleEngaged & !touchSensorOn & (stepperTicks >= touchSensorOnSteps) & (stepperTicks < touchSensorOffSteps)){
    touchSensorOn = true;
    digitalWrite(touchSensorOnPin, HIGH);
  }

  // turn off touch sensor
  else if (obstacleEngaged & touchSensorOn & ((stepperTicks >= touchSensorOffSteps) | (stepperTicks < touchSensorOnSteps))){
    touchSensorOn = false;
    digitalWrite(touchSensorOnPin, LOW);
  }
  
  // disengage:
  else if (obstacleEngaged & (stepperTicks >= stepperStopPosition)){
    
    obstacleEngaged = false;
    digitalWrite(obstaclePin, LOW);
    digitalWrite(obsLightPin, LOW);
    digitalWrite(obsLightPin2, LOW);
    digitalWrite(obsOnPin, LOW);
    obstacleInd++;
    obsPos = setObsPos(obstacleInd);
    recalibrateLimits();
    
  }
  
  //give laser at random position while mice are doing basic locomotion
  if (!giveLaserPulseAlready & wheelTicksTemp > LaserPulsePosition) {
    digitalWrite(laserPin, HIGH);
    giveLaserPulseAlready = true;
  }



  // give water if reward location reached
  if (wheelTicksTemp > rewardPosition){
    giveReward();
    resetForLaserExp();
  }


  
  // compute and move to target stepper position
  if (obstacleEngaged){
    
    targetStepperTicks = ((wheelTicksTemp - startingWheelTics) * conversionFactor) * obsGain + startingStepperTics;
    targetStepperTicks = constrain(targetStepperTicks, startPositionBuffer, stepperStopPosition);

    stepsToTake = targetStepperTicks - stepperTicks;
    
    if (stepsToTake!=0){
      takeStep(stepsToTake);
    }
  }
}
