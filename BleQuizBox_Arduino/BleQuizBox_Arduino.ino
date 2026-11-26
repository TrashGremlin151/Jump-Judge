/*
  LED

  This example creates a Bluetooth® Low Energy peripheral with service that contains a
  characteristic to control an LED.

  The circuit:
  - Arduino MKR WiFi 1010, Arduino Uno WiFi Rev2 board, Arduino Nano 33 IoT,
    Arduino Nano 33 BLE, or Arduino Nano 33 BLE Sense board.

  You can use a generic Bluetooth® Low Energy central app, like LightBlue (iOS and Android) or
  nRF Connect (Android), to interact with the services and characteristics
  created in this sketch.

  This example code is in the public domain.
*/

#include <ArduinoBLE.h>

BLEService ledService("19B10000-E8F2-537E-4F6C-D104768A1214"); // Bluetooth® Low Energy LED Service
BLEService QuizBoxService("00001101-0000-10000-8000-00805F9B34FB"); // QuizBox Service

// Bluetooth® Low Energy LED Switch Characteristic - custom 128-bit UUID, read and writable by central
//BLEByteCharacteristic switchCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);
BLECharacteristic switchCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite,8);
BLEByteCharacteristic styleCharacteristic("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);
BLEByteCharacteristic maskCharacteristic("19B10003-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);
BLEByteCharacteristic latchCharacteristic("19B10004-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);
BLEByteCharacteristic stateCharacteristic("19B10005-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);
BLECharacteristic orderCharacteristic("19B10006-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite,8);
BLECharacteristic versionCharacteristic("19B10007-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite,5);
BLEByteCharacteristic localnameCharacteristic("19B10008-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);

const int ledPin = LED_BUILTIN; // pin to use for the LED
const int ChairPin[8] = {9,8,7,6,5,4,3,2};    //DI's used for chairs
byte message[8] = {77,76,75,74,73,72,71,70};


  //********************
  //Write state and order to JumpJudge
  //********************
  
void writeOrder(byte State0)
{
  byte BLEmsg[8];
  int Idx;

    stateCharacteristic.writeValue(State0);  //tell JumpJudge about seat state

    for (int Idx = 0; Idx <= 7; Idx++)
    {
      BLEmsg[Idx] = ChairOrder[Idx];      //seat jump order
    }
    orderCharacteristic.writeValue(BLEmsg,8);  //tell JumpJudge about a seat order
}//end writeOrder

void setup() {
  Serial.begin(9600);
  while (!Serial);

// initialize chairs pins as digital input with internal pullup resistors
    for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
  {
    pinMode(ChairPin[ChairIdx], INPUT_PULLUP);      // initialize chairs pins as digital input
  }

// set LED pin to output mode
  pinMode(ledPin, OUTPUT);

  // begin initialization
  if (!BLE.begin()) {
    Serial.println("starting Bluetooth® Low Energy module failed!");

    while (1);
  }

  // set advertised local name and service UUID:
//  BLE.setLocalName("LED");
  BLE.setLocalName("QuizBox5");
  BLE.setAdvertisedService(ledService);
//  BLE.setAdvertisedService(QuizBoxService);

  // add the characteristic to the service
  ledService.addCharacteristic(switchCharacteristic);
//  QuizBoxService.addCharacteristic(switchCharacteristic);

  // add service
  BLE.addService(ledService);
//  BLE.addService(QuizBoxService);

  // set the initial value for the characeristic:
//  switchCharacteristic.writeValue(0);
//  switchCharacteristic.writeValue(74);
  switchCharacteristic.writeValue(message,8);

  // start advertising
  BLE.advertise();

  Serial.println("BLE LED Peripheral");
}

void loop() {
  // listen for Bluetooth® Low Energy peripherals to connect:
  BLEDevice central = BLE.central();

  // if a central is connected to peripheral:
  if (central) {
    Serial.print("Connected to central: ");
    // print the central's MAC address:
    Serial.println(central.address());

    // while the central is still connected to peripheral:
    while (central.connected()) {
      // if the remote device wrote to the characteristic,
      // use the value to control the LED:
      if (switchCharacteristic.written()) {
        if (switchCharacteristic.value()) {   // any value other than 0
          Serial.println("LED on");
          digitalWrite(ledPin, HIGH);         // will turn the LED on
        } else {                              // a 0 value
          Serial.println(F("LED off"));
          digitalWrite(ledPin, LOW);          // will turn the LED off
        }
      }
      if (styleCharacteristic.written()) {
          BitVal = 1;
          for (int Idx = 0; Idx < 8; Idx++)
          {
            if (USBmsg[1] & BitVal) ChairStyle[Idx] = 1;
            else ChairStyle[Idx] = 0;
            BitVal *= 2;  //Next bit
          }
          jumpjudge = true;
      }
      if (maskCharacteristic.written()) {
          BitVal = 1;
          for (int Idx = 0; Idx < 8; Idx++)
          {
            if (USBmsg[1] & BitVal) ChairMask[Idx] = 1;
            else ChairMask[Idx] = 0;
            BitVal *= 2;  //Next bit
          }
      }
      if (latchCharacteristic.written()) {
          Latch = USBmsg[1];
      }
    }

    // when the central disconnects, print it out:
    Serial.print(F("Disconnected from central: "));
    Serial.println(central.address());
  }
}


// the loop routine runs over and over again forever:
void loop() {
  byte USBmsg[2];
  int RandomIdx;
  int ChairIdx;
  int SampleArray[8];
  int AllDown;
  byte State0;
  byte State1;
  int BitVal;
  unsigned long BuzzerTime;
  //********************
  //Read Chairs
  //********************
  
  USBmsg[0] = JJ_SETSTATE;      //BitBang tag
  State1 = State0;
//populate seat state bitmap
  State0 = 0;
  BitVal = 1;
  for (int Idx = 0; Idx < 8; Idx++)
  {
    State0 += (digitalRead(ChairPin[Idx]) * BitVal);   // accumulate bits
    BitVal *= 2;  //Next bit
  }

//is this needed?
  if (jumpjudge && (State0 != State1))
  {
    USBmsg[1] = State0;      //current state
    writeSLIP(USBmsg, 2);  //tell JumpJudge about a state change
  }
//-?

  for (int Idx = 0; Idx < MaskIdx; Idx++)
  {
    SampleArray[Idx] = ChairMask[Idx];  //Load the selection array
  }
  AllDown = 1;    //Assume all seats down (find out if we should reset jump order?)
  for (int Idx = MaskIdx-1; Idx >= 0; --Idx)        //Count backwards
  {
    RandomIdx = random(Idx);
    ChairIdx = SampleArray[RandomIdx];         //Select a random seat to sample
    SampleArray[RandomIdx] = SampleArray[Idx];  //backfill the selection array
    if (digitalRead(ChairPin[ChairIdx]) == ChairStyle[ChairIdx])   // chair is up!
    {
      AllDown = 0;    //NOT all down!
      if (ChairOrder[ChairIdx] == 0)       // Just triggered?
      {
        if (JumpIdx == 0)
        {
          //sound buzzer with jumper sound;
          digitalWrite(led, HIGH);   // turn the LED on (HIGH is the voltage level)
          BuzzerTime = millis();
          ReferenceTime = millis();          //start timer
        }
        if (JumpIdx < MaxJumpers)            //Artificially limit jumpers
        {
          JumpIdx++;
          ChairOrder[ChairIdx] = JumpIdx;
          text[ChairIdx] = JumpIdx;        //save jump order in chair position
          ChairTimer[ChairIdx] = millis() - ReferenceTime;
        }
      }//endif chair changed
      ChairState[ChairIdx] = HIGH;        // Mark seat up
      text[ChairIdx] = text[ChairIdx] | B10000000;        //save current status
      if (!(SeatBits & ChairBit[ChairIdx]))
      {
        SeatBits |= ChairBit[ChairIdx];
        if (jumpjudge && (State0 != State1))
        {
          writeUSB(State0);    //tell JumpJudge about a state change
        }
      }
    }//endif chair up
    else //chair is down
    {
      if (SeatBits & ChairBit[ChairIdx])
      {
        SeatBits &= !(ChairBit[ChairIdx]);
        if (jumpjudge && (State0 != State1))
        {
          writeUSB(State0);    //tell JumpJudge about a state change
        }
      }
      ChairState[ChairIdx] = LOW;        // Mark seat down
      text[ChairIdx] = text[ChairIdx] & B01111111;        //save current status
    }//endif chair down
    displayLED(ChairIdx+1, text[ChairIdx]);      //display jump order
  }//endfor check all chairs
  if ((AllDown == 1) && (Latch == 0))
  {
    JumpIdx = 0;
    for (int Idx = 0; Idx < MaskIdx; Idx++)
    {
      ChairIdx = ChairMask[Idx];  //index through the selection array
      ChairState[ChairIdx] = LOW;
      ChairOrder[ChairIdx] = 0;
      ChairTimer[ChairIdx] = 0;
      text[ChairIdx] = 0x0a;                //set digit to "-"
      displayLED(ChairIdx+1, text[ChairIdx]);
    }
  }
  if (millis() - BuzzerTime > 100)  //Shut off buzzer
  {
    digitalWrite(led, LOW);    // turn the LED off by making the voltage LOW
  }

  if (millis() > ReferenceTime + 250) //heartbeat update every quarter second
  {
    ReferenceTime = millis();        //Reset timer
    if (jumpjudge)
    {
      writeUSB(State0);    //bit bang state to JumpJudge
    }
  //********************
  //Service USB read
  //********************
      readUSB(); //Read JumpJudge reply
  }
}

