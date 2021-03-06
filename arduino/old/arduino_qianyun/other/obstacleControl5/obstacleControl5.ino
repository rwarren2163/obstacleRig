// OBSTACLE CONTROL
#include <RunningMedian.h>


// pin assignments
#define stepPin         4
#define stepDirPin      5
#define encoderPinA     20   // don't change encoderPins, because direct port manipulation is used that assumes 20 and 21 are used for encoder pins
#define encoderPinB     21
#define waterPin        7
#define obstaclePin     13   // signals whether the obstacle is engaged... this is sent to an arduino that controls the obstacle servo
#define motorOffPin     8    // turns on stepper motor driver
#define startLimitPin   9    // signal is LOW when engaged
#define stopLimitPin    10   // signal is LOW when engaged
#define obsLightPin     2    // controls when the light for the obstacle turns on
#define obsLightPin2    6    // controls when the light for the obstacle turns on
#define obsOnPin        3    // when the obstacle is engaged in a trial, i.e. tracking the mouse's position


// user settings
volatile int state = 1; // 1: no platform movement, no obstacles, 2: platform movement, no obstacles, 3: platform movement and obstacles
volatile float obsGain = 1.0;
const int servoSwingTime = 200; // ms, approximate amount of time it takes for the osbtacle to pop out // this is used as a delay bewteen the obstacle reaching the end of the track and it coming back, to avoid it whacking the guy in the butt!
volatile float rewardRotations = 9.01;
const int startPositionMm = 5;
const int endPositionMm = 20;
const int waterDuration = 80; // milliseconds
const double maxStepperSpeed = 1.5; // (m/s)
const float acceleration = 8.0; //(m/s^2)
volatile float callibrationSpeed = .6; // speed with motor moves plastform during callibration (m/s)
const float obstacleLocations[] = {1.5, 4.5, 7.5, rewardRotations*20}; // expressed in wheel ticks // the last element is a hack... the index goes up and the wheel position will never reach the last value, which is the desired behavior
const int velocitySamples = 10; // each sample last about 500 microseconds
const int obsPosJitter[] = {-100, 100}; // jitter range for the onset position of obstacles (mm)
const int startPosJitter = 20; // (mm)
const float obsLightProbability = 0.5;
const long delayLookupLength = 20000;


// rig characteristics
const int microStepping = 16; // only (1/microStepping) steps per pulse // this should correspond to the setting on the stepper motor driver, which is set by 3 digital inputs
const int motorSteps = 200;
const int encoderSteps = 2880; // 720cpr * 4
const double timingPulleyRad = 15.2789; // (mm)
const double wheelRad = 95.25; // (m)


// initializations
const float mmPerMotorTic = ((2*PI*timingPulleyRad) / (motorSteps*microStepping));
const int startPositionBuffer = startPositionMm * ((motorSteps*microStepping) / (2*PI*timingPulleyRad));
const int endPositionBuffer = endPositionMm  * ((motorSteps*microStepping) / (2*PI*timingPulleyRad));; // motor stops endPositionBuffer steps before the beginning and end of the track
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
volatile int startingWheelTics = 0;
volatile int startingStepperTics = 0;
volatile int obstacleLocationSteps[sizeof(obstacleLocations)];
volatile int obsPos; // position of obstacle, in wheel tics, on a given trial
volatile float stepperSpeed;
RunningMedian deltaMicroSmps = RunningMedian(5);



void setup() {
  
  // prepare ins and outs
  pinMode(stepPin, OUTPUT);
  pinMode(stepDirPin, OUTPUT);
  pinMode(waterPin, OUTPUT);
  pinMode(motorOffPin, OUTPUT);
  pinMode(obstaclePin, OUTPUT);
  pinMode(startLimitPin, INPUT_PULLUP);
  pinMode(stopLimitPin, INPUT_PULLUP);
  pinMode(obsLightPin, OUTPUT);
  pinMode(obsLightPin2, OUTPUT);
  pinMode(obsOnPin, OUTPUT);
  
  digitalWrite(stepPin, LOW);
  digitalWrite(stepDirPin, stepDir);
  digitalWrite(waterPin, LOW);
  digitalWrite(motorOffPin, LOW);
  digitalWrite(obstaclePin, LOW);
  digitalWrite(obsLightPin, LOW);
  digitalWrite(obsLightPin2, LOW);
  digitalWrite(obsOnPin, LOW);


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
  if (maxStepperDelayInd==0){
    Serial.println("WARNING: Too many values in stepper delay lookup!");
    Serial.println("Try increasing acceleration or decreasing max stepper velocity...");
  }

  
  // initialize locations of obstacles
  initializeObsLocations();
  
  // initialize track limits and move to starting position
  initializeLimits();
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
       


  // give water if reward location reached
  if (wheelTicksTemp > rewardPosition){
    giveReward();
  }
  

  
  // compute and move to target stepper position
  if (obstacleEngaged){
    
    targetStepperTicks = ((wheelTicksTemp - startingWheelTics) * conversionFactor) / obsGain + startingStepperTics;
    targetStepperTicks = constrain(targetStepperTicks, startPositionBuffer, stepperStopPosition);

    stepsToTake = targetStepperTicks - stepperTicks;
    
    if (stepsToTake!=0){
      takeStep(stepsToTake);
    }
  }
}




// move stepper one step in stepDirection
void startTracking(){

  // set motor direction to forward
  digitalWrite(stepDirPin, HIGH);

  targetStepDelay = getMotorDelayFromWheelDelay(deltaMicroSmps.getAverage()) / obsGain;
  stepperDelayInd = 0;
  stepDelay = stepperDelays[0];

  while (stepDelay >= targetStepDelay){
    
    // increment motor speed
    stepperDelayInd = min(stepperDelayInd+1, maxStepperDelayInd);
    stepDelay = max(stepperDelays[stepperDelayInd], targetStepDelay);

    // take step
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(stepDelay);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(stepDelay);
    stepperTicks++;

    // update target delay based on current wheel speed
    targetStepDelay = getMotorDelayFromWheelDelay(deltaMicroSmps.getAverage());
  }

  // record wheel and motor positions at the start of positional tracking  
  noInterrupts();
  wheelTicksTemp = wheelTicks;
  interrupts();
  startingWheelTics = wheelTicksTemp;
  startingStepperTics = stepperTicks;

  // turn on obstacle light
  if (state==3){
    if(random(0,100) < obsLightProbability*100.0){
      digitalWrite(obsLightPin, HIGH);
      digitalWrite(obsLightPin2, HIGH);
    }
  }
}




// move stepper one step in stepDirection
void takeStep(int stepsToTake){

  // set motor direction
  digitalWrite(stepDirPin, (stepsToTake>0));
  delayMicroseconds(1); // TCM2100 stepper motor driver requires 20 nanoseconds between stepDirectionPin change and stepPin input // the driver was messing up occasionally, so hopefully this will fix the problem

  for (int i = 0; i < abs(stepsToTake); i++){

    // take step
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(maxSpeedDelay);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(maxSpeedDelay);
  }

  stepperTicks += stepsToTake;
}



// move stepper one step in stepDirection
void takeAcceleratingStep(int stepsToTake, int accelDirection, int delayLimit){

  // set motor direction
  digitalWrite(stepDirPin, (stepsToTake>0));
  delayMicroseconds(1); // TCM2100 stepper motor driver requires 20 nanoseconds between stepDirectionPin change and stepPin input // the driver was messing up occasionally, so hopefully this will fix the problem

  for (int i = 0; i < abs(stepsToTake); i++){

    switch (accelDirection){

      case -1:
        if (stepperDelays[stepperDelayInd]<delayLimit){
          stepperDelayInd--;
          stepDelay = min(stepperDelays[stepperDelayInd], delayLimit);
        }
        break;
        
      case 1:
        if (stepperDelays[stepperDelayInd]>delayLimit){
          stepperDelayInd++;
          stepDelay = max(stepperDelays[stepperDelayInd], delayLimit);
        }
        break;
    }

    // take step
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(stepDelay);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(stepDelay);
  }

  stepperTicks += stepsToTake;
}






// read rotary encoder
void encoder_isr() {
    
    currentMicros = micros();
    deltaMicroSmps.add(currentMicros - lastMicros);
    lastMicros = currentMicros;
    
    static int8_t lookup_table[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
    static uint8_t enc_val = 0;
    
    enc_val = enc_val << 2;
    enc_val = enc_val | ((REG_PIOB_PDSR & (0b11 << 12)) >> 12); // read all pins from port B, and bitmask to get just inputs 12 and 13 from port B, which correspond to Due pins 20 and 21
 
    wheelTicks = wheelTicks - lookup_table[enc_val & 0b1111];

}



void getStartLimit(){

  stepperDelayInd = 0; // start at lowest velocity
  
  // find start limit
  while (digitalRead(startLimitPin)){
    takeAcceleratingStep(-1, 1, callibrationDelay);
  }
  stepperTicks = 0;
  
}



void getEndLimit(){

  stepperDelayInd = 0; // start at lowest velocity

  // find stop limit
  while (digitalRead(stopLimitPin)){
    takeAcceleratingStep(1, 1, callibrationDelay);
  }
  stepperStopPosition = stepperTicks - endPositionBuffer;

}



void initializeLimits(){

  getStartLimit();
  getEndLimit();
  getStartLimit();
  stepperDelayInd = 0; // start at lowest velocity
  takeAcceleratingStep(startPositionBuffer + getStartJitter(startPosJitter), 1, callibrationDelay); // move back to startPositionBuffer
  digitalWrite(motorOffPin, HIGH);
  
}



void recalibrateLimits(){

  // reset stepper motor driver
  digitalWrite(motorOffPin, HIGH);
  delay(50);
  digitalWrite(motorOffPin, LOW);

  // return to beginning of track
  delay(servoSwingTime); // delay before returning the platform to avoid whacking the mouse in the butt
  getStartLimit();
  if (state==3){
    digitalWrite(obstaclePin, HIGH);
  }

  // return to starting position
  stepperDelayInd = 0; // start at lowest velocity
  takeAcceleratingStep(startPositionBuffer + getStartJitter(startPosJitter), 1, callibrationDelay); // move back to startPositionBuffer
  digitalWrite(motorOffPin, HIGH); // disengage stepper motor driver
  
}




void printMenuAndSettings(){

  // print settings
  Serial.println("CURRENT SETTINGS:");
  Serial.print("condition: ");
  Serial.println(conditionNames[state-1]);
  Serial.print("reward rotations: ");
  Serial.println(rewardRotations);
  Serial.print("osbtacle gain: ");
  Serial.println(obsGain);
  Serial.println("");
  delay(500);

  // print menu
  Serial.println("MENU:");
  Serial.println("1: set condition to rewards only");
  Serial.println("2: set condition to platform, no obstacles");
  Serial.println("3: set condition to platform, with obstacles");
  Serial.println("4: set reward rotation number");
  Serial.println("5: set obstacle gain");
  Serial.println("");
  delay(500);
  
}




void getUserInput(){

  // check for user input
  if (Serial.available()){
    
    userInput = Serial.parseInt();
    
    switch (userInput){

      // set condition to rewards only
      case 1:
        state = userInput;
        printMenuAndSettings();
        digitalWrite(obstaclePin, LOW);

        // reset wheel ticks
        noInterrupts();
        wheelTicks = 0;
        interrupts();
        break;
        
      // set condition to platform movement, no obstacles
      case 2:
        state = userInput;
        printMenuAndSettings();
        digitalWrite(obstaclePin, LOW);
        break;
        
      // set condition to platform movement with obstacles
      case 3:
        state = userInput;
        printMenuAndSettings();
        digitalWrite(obstaclePin, HIGH);

        // reset wheel ticks
        noInterrupts();
        wheelTicks = 0;
        interrupts();

        // prepare first obstacle position
        obstacleInd = 0;
        obsPos = setObsPos(obstacleInd);
        break;
        
      // enter reward rotation number
      case 4:
        Serial.print("enter reward rotations...\n\n");
        while (Serial.available() == 0)  {}
        rewardRotations = Serial.parseFloat();
        printMenuAndSettings();
        rewardPosition = (rewardRotations * encoderSteps) / obsGain;
        break;
        
      // set gain
      case 5:
        Serial.print("enter obstacle gain...\n\n");
        while (Serial.available() == 0)  {}
        obsGain = Serial.parseFloat();
        initializeObsLocations();
        printMenuAndSettings();
        break;
    }
  }
}




void giveReward(){
  
    wheelTicksTemp = 0;
    
    noInterrupts();
    wheelTicks = 0;
    interrupts();

    obstacleInd = 0;
    obsPos = setObsPos(obstacleInd);
    digitalWrite(waterPin, HIGH);
    delay(waterDuration);
    digitalWrite(waterPin, LOW);

}




long getMotorDelayFromSpeed(float motorSpeed){

    long ticsPerSecond = motorSpeed * (motorSteps*microStepping* 1000) / (2*PI*timingPulleyRad);
    int microSecondsPerTic = (1 / (ticsPerSecond / pow(10,6))) / 2;
    return microSecondsPerTic;

}



int getMotorDelayFromWheelDelay(int wheelDelay){

  return (wheelDelay * (mmPerMotorTic / ((2*PI*wheelRad) / encoderSteps))) / 2;
  
}



int setObsPos(int index){

  static double ticsPerMm = (encoderSteps / (2*PI*wheelRad));
  return obstacleLocationSteps[index] + random(obsPosJitter[0]*obsGain*ticsPerMm, obsPosJitter[1]*ticsPerMm); // initialize first position)

}



int getStartJitter(int jitterMax){

  return random(0, jitterMax/mmPerMotorTic);
  
}



void initializeObsLocations(){

  for (int i=0; i<sizeof(obstacleLocations); i++){
    obstacleLocationSteps[i] = (obstacleLocations[i] * encoderSteps) / obsGain;
  }
  obsPos = setObsPos(0); // set first obstacle position
  
}

