


// pin assignments
const int stepperInputPin = 2; // when this is high the stepper is engaged; disengage when it goes low
const int rewardInputPin = 3;
const int endStopPin = 7;
const int stepperDirectionPin = 6;
const int stepperStepPin = 5;
const int stepperDisablePin = 4;
const int vidTtlPin = 13;
const int obsHeightPin = 11; // don't change - i hack into timer0, timer1 is used by stepper library, and pins 3,11 use timer2 on arduino uno


// user settings
const bool randomizeHeights = true;
const float randObsHeightMin = 0.0;
const float randObsHeightMax = 8.0;
volatile float obsHeight = 5.0;  // (mm), height of bottom surface of obs
volatile float tallShortProbability = 0.5; // probability that the obstacle will be high or low
const float obsOnSteps = 180;
const float obsOffSteps = 20;
const int vidTtlPulseDuration = 1;   // ms
const int vidTtlInterval = 4; // ms
const int rewardTtlOverhang = 500; // ms to continue generating ttls for after reward reached // THIS SHOULD NOT BE DIVISIBLE BY 1000, SO THE TIMESTAMP INTERVAL BETWEEN FRAMES AT ENDAND BEGINNING OF NEXT TRIAL WILL BE DIFFERENT THAN 1/FS
const int rewardTtlGap = 500; // ms pause in ttls after rewardTtlOverhang has passed
const float obsHeightTravel = 15.0; // mm
const int stepTtlDuration = 1; // ms
const int stepTtlInterval = 2; // ms


// initializations
volatile bool isStepperEngaged = false;
volatile bool isTtlOn = false;
volatile bool overHanging = false;
volatile int vidTtlTimer = 0;
volatile int stepperTtlTimer = 0;
volatile int overhangTimer = 0;
volatile int serialInput = 0;
volatile int stepsToTake = 0;
volatile int stepsTaken = 0;




void setup() {

  // initialize pins
  pinMode(stepperInputPin, INPUT);
  pinMode(rewardInputPin, INPUT);
  pinMode(vidTtlPin, OUTPUT);
  pinMode(stepperDisablePin, OUTPUT);
  pinMode(stepperDirectionPin, OUTPUT);
  pinMode(stepperStepPin, OUTPUT);
  pinMode(obsHeightPin, OUTPUT);
  pinMode(endStopPin, INPUT_PULLUP);
  
  digitalWrite(vidTtlPin, LOW);
  digitalWrite(stepperDisablePin, LOW);
  digitalWrite(stepperStepPin, LOW);
  digitalWrite(stepperDirectionPin, HIGH);
  setObsHeight(obsHeight);


  // initialiez random seed
  randomSeed(analogRead(0));

  // initialize hardware interrupts
  attachInterrupt(digitalPinToInterrupt(stepperInputPin), controlstepper, CHANGE);
  attachInterrupt(digitalPinToInterrupt(rewardInputPin), rewardReached, RISING);



  // initialize 1kHz timer0 interrupt
  noInterrupts();
  TCCR0A = 0;// set entire TCCR0A register to 0
  TCCR0B = 0;// same for TCCR0B
  TCNT0  = 0;//initialize counter value to 0
  // set compare match register for 2khz increments
  OCR0A = 249;// = (16*10^6) / (1000*64) - 1 (must be <256)
  // turn on CTC mode
  TCCR0A |= (1 << WGM01);
  // Set CS01 and CS00 bits for 64 prescaler
  TCCR0B |= (1 << CS01) | (1 << CS00);
  // enable timer compare interrupt
  TIMSK0 |= (1 << OCIE0A);
  interrupts();


  // begin serial communication
  Serial.begin(115200);
  Serial.println("1: start video ttls");
  Serial.println("2: stop video ttls");
  Serial.println("3: set obstacle height");
}



void loop() {

  if (Serial.available()) {

    serialInput = Serial.read() - 48; // parseInt was not working here for some reason

    switch (serialInput) {
      case 1:
        isTtlOn = true;
        overHanging = false;
        break;
      case 2:
        isTtlOn = false;
        overHanging = false;
        break;
      case 3:
        Serial.print("enter obstacle height...\n\n");
        while (Serial.available() == 0)  {}
        obsHeight = Serial.parseFloat();
        setObsHeight(obsHeight);
        break;
    }
  }

}


// stepper engage/disengage interrupts
void controlstepper() {
  
  // turn on motor
//  digitalWrite(stepperDisablePin, LOW);
  
  // find the end stop
  while (digitalRead(endStopPin)){
    if (stepsTaken>=stepsToTake){
      takeStep(-1);
    }
  }

  // read desired obstacle status
  isStepperEngaged = digitalRead(stepperInputPin);

  // set obstacle to desired position
  if (isStepperEngaged) {

    // set obstacle height
    if (randomizeHeights){
      obsHeight = random(randObsHeightMin*10, randObsHeightMax*10) / 10;
      setObsHeight(obsHeight);
    }

    // engage obstacle
    takeStep(obsOnSteps);
    
  } else {
    // disengage obstacle
    takeStep(obsOffSteps);
  }

  // turn off driver
//  digitalWrite(stepperDisablePin, HIGH);
}



// reward reached interrupt
void rewardReached() {
  if (isTtlOn) {
    overHanging = true;
    overhangTimer = 0;
  }
}



// set obstacle height
void setObsHeight(float obsHeight) {
  int obsHeight8 = round(obsHeight * (255.0 / obsHeightTravel));
  analogWrite(obsHeightPin, obsHeight8);
  Serial.print("obstacle height set to: ");
  Serial.println(obsHeight);
}


// timer0 interrupts controls vidTtl generation
ISR(TIMER0_COMPA_vect) {

  // control vidTtl generation
  vidTtlTimer++;

  switch (vidTtlTimer) {
    case 1:
      if (isTtlOn) {
        digitalWrite(vidTtlPin, HIGH);
      }
      break;
    case vidTtlPulseDuration+1:
      digitalWrite(vidTtlPin, LOW);
      break;
    case vidTtlInterval:
      vidTtlTimer = 0;
      break;
  }


  // control vidTtl overhang period
  if (overHanging) {

    overhangTimer++;

    switch (overhangTimer) {
      case rewardTtlOverhang:
        isTtlOn = false;
        break;
      case rewardTtlOverhang + rewardTtlGap:
        isTtlOn = true;
        overHanging = false;
        break;
    }
  }


  // control stepper ttl timer
  if (stepsTaken<stepsToTake){
//    Serial.print(stepsTaken);
//    Serial.println(stepsToTake);
    
    stepperTtlTimer++;
  
    switch (stepperTtlTimer) {
      case 1:
        digitalWrite(stepperStepPin, HIGH);
        break;
      case (1+stepTtlDuration):
        digitalWrite(stepperStepPin, LOW);
        break;
      case (1+stepTtlDuration+stepTtlInterval):
        stepperTtlTimer = 0;
        stepsTaken++;
        break;
    }
  }
}



void takeStep(volatile int steps){
  
  // set direction
  digitalWrite(stepperDirectionPin, steps>0);

//  for (int i=0; i<abs(steps); i++){
//    digitalWrite(stepperStepPin, HIGH);
//    delay(1);
//    digitalWrite(stepperStepPin, LOW);
//    delay(1);
//  }
  

//   reset step counter parameters
  stepsToTake = abs(steps);
  stepsTaken = 0;
  stepperTtlTimer = 0;
}



