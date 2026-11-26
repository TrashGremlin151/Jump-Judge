/*
  QuizBox
  Monitors 8 digital inputs for change of state
  transition order displays on an 8-digit, 7-segment display.
  each input is reflected on the decimal point
  Buzzer is sounded on first input that changes
  Data is bit banged on serial port for PC
 */
#include <EEPROM.h>

// Alternate LED Display style
//#define MAX7219
//#define YL44
#define StopWatchButton
//#define BeepTone 3300 
//#define BeepTone 2700 //J431
#define BeepTone 2750 //J432
#define BuzzTone 1050 //J431

#define SLIP_END 0xC0 
#define SLIP_ESC 0xDB 
#define SLIP_ESC_END 0xDC 
#define SLIP_ESC_ESC 0xDD 
#define SLIP_MAX 100 

#define JJ_RESET 0 
#define JJ_SETSTYLE 1 
#define JJ_SETMASK 2 
#define JJ_SETLATCH 3 
#define JJ_GETORDER 4 
#define JJ_GETTIMER 5 
#define JJ_SETSTATE 6 
#define JJ_SETORDER 7 
#define JJ_SETTIMER 8 
#define JJ_VERSION 9 
#define JJ_PRACTICE 10 

#define RestartHold 1750
#define DEBOUNCE 3


#if defined MAX7219
// MAX7219 font
#define digitA B01110111
#define digitL B00001110
#define digitU B00111110
#define digitP B01100111
#define digit_ B00000000
#define digitS B01011011
#define digitI B01000100
#define digitT B01000110
#define digitD B00111101
#define digitN B00010101
#define digitJ B00111100
#define digitM B01110110
#define digitG B01111011
#define digitE B01001111
#define digitF B01000111
#define digitC B01001110
#define digit0 B01111110
#define digit1 B00110000
#define digit2 B01101101
#define digit3 B01111001
#define digit4 B00110011
#define digit5 B01011011
#define digit6 B01011111
#define digit7 B01110000
#define digit8 B01111111
#define digit9 B01110011
#define digitX B00000001
#define digitR B00000101
#define decpnt B10000000

#else
// TM1638 font
#define digitA B01110111
#define digitL B00111000
#define digitU B00111110
#define digitP B01110011
#define digit_ B00000000
#define digitS B01101101
#define digitI B00010001
#define digitT B00110001
#define digitD B01011110
#define digitN B01010100
#define digitJ B00011110
#define digitM B00110111
#define digitG B01101111
#define digitE B01111001
#define digitF B01110001
#define digitC B00111001
#define digit0 B00111111
#define digit1 B00000110
#define digit2 B01011011
#define digit3 B01001111
#define digit4 B01100110
#define digit5 B01101101
#define digit6 B01111101
#define digit7 B00000111
#define digit8 B01111111
#define digit9 B01100111
#define digitX B01000000
#define digitR B01010000
#define decpnt B10000000
#endif

//byte jjversion[4] = {digitJ,digit4,digit3,digit0}; //JumpJudge Protocol Version 4.3.0 Mar 29, 2017
byte jjversion[4] = {digitJ,digit4,digit3,digit2}; //JumpJudge Protocol Version 4.3.1 Apr 10, 2019

int buzzer = 13;      //AKA Arduino LED buzzer output
int dataPin = 12;
int clock = 11;
int load = 10;
int dataPin2 = 12;    //same data pin
int clock2 = 11;      //same clock
int load2 = 18;      //AKA Analog-4 different strobe
int stopwatch = 19;  //AKA Analog-5;

unsigned int QuizMode = 0;        //0 = Jump quiz regular, 1 = Practice jump, 2=Latch(table quizzing)
int MaxJumpers = 4;    //artificially limit how many are displayed
int ChairPin[8] = {9,8,7,6,5,4,3,2};
int ChairMap[8] = {4,5,6,7,3,2,1,0};
int ChairBit[8] = {B10000000,B01000000,B00100000,B00010000,B00000001,B00000010,B00000100,B00001000};
int ChairState[8] = {LOW,LOW,LOW,LOW,LOW,LOW,LOW,LOW};  //Current seat status
int ChairOrder[8] = {0,0,0,0,0,0,0,0};  //Who is first, second, third...
int ChairSequence[8] = {0,0,0,0,0,0,0,0};  //Who is first, second, third...
unsigned long ChairTimer[8] = {0,0,0,0,0,0,0,0};  //ReferenceTime milliseconds between jumps
int ChairStyle[8];      //normally open or normally closed seat style? (classic = normally closed)
int ChairMask[8];       //seat occupided or not?
int MaskIdx;            //mask counter?
int NextMask[8];        //seat occupided or not?
int NextIdx;            //mask counter?
byte SeatBits = 0;      //Bit Bang data
byte number[10] = {digit0,digit1,digit2,digit3,digit4,digit5,digit6,digit7,digit8,digit9}; //number font
byte text[8];           //LED storage
byte text2[8];          //second LED storage
int JumpIdx = 0;        //Jump order counter
int Latch = 0;          //Latch seat flickers = off
unsigned int StopWatchVal = 0; //Stopwatch second counter
unsigned long flicker = 0;  //lag before turning lights off after all down
int StopRef;          //StopWatch reference status
int PaintIdx;
int PaintMode;
unsigned long ReferenceTime;
unsigned long PracticeTime;  // 0 = timer expired, >millis = timer active, <millis = sound buzzer
unsigned long PaintTime;
unsigned long HeartBeat;
unsigned long BuzzerTime;
unsigned long StopWatchTime = 0;
unsigned long StopWatchDebounce = 0;
unsigned long ReStartTime = 0;
boolean jumpjudge = false;
//unsigned char SlipDebug = 255;

void toneON(int value)
{
  tone(buzzer, value);
}

void toneOFF()
{
#if defined YL44
//  digitalWrite(buzzer, HIGH);
  tone(buzzer, -10);
#else
  noTone(buzzer);
#endif
}

#if defined MAX7219
//***************************
// Output MAX7219 .36 data onto the 8-digit display
//***************************
void displayDATA(byte AdrCode, byte RegData)
{
//Serial.print ("\nLED: ");
//Serial.print (AdrCode,HEX);
//Serial.print (RegData,HEX);
  digitalWrite(load,LOW);
  shiftOut(dataPin, clock, MSBFIRST, AdrCode); // shift out Address
  shiftOut(dataPin, clock, MSBFIRST, RegData);  // shift out Data
  digitalWrite(load,HIGH);
}//end displayDATA

void display2DATA(byte AdrCode, byte RegData)
{
//Serial.print ("\nLED: ");
//Serial.print (AdrCode,HEX);
//Serial.print (RegData,HEX);
  digitalWrite(load2,LOW);
  shiftOut(dataPin2, clock2, MSBFIRST, AdrCode); // shift out Address
  shiftOut(dataPin2, clock2, MSBFIRST, RegData);  // shift out Data
  digitalWrite(load2,HIGH);
}//end displayDATA

//***************************
// Initialize MAX7219 .36 LED Display
//***************************
void initdisplay()
{
// MAX7219 protocol
  displayDATA(0x0c, 1);    //shutdown mode off
  displayDATA(0x0a, 8);    //intensity
  displayDATA(0x09, 0);    //BCD mode off
  displayDATA(0x0b, 7);    //scan limit = all 8 digits
}

#else

//***************************
// Output TM1638 .56 command without data
//***************************
void displayCMD(byte RegData)
{
  digitalWrite(load,LOW);
  shiftOut(dataPin, clock, LSBFIRST, RegData);  // shift out Data
  digitalWrite(load,HIGH);
}//end displyCMD

void display2CMD(byte RegData)  //dual displyCMD
{
  digitalWrite(load2,LOW);
  shiftOut(dataPin2, clock2, LSBFIRST, RegData);  // shift out Data
  digitalWrite(load2,HIGH);
}//end displyCMD

//***************************
// Output TM1638 .56 data onto the 8-digit display
//***************************
void displayDATA(byte AdrCode, byte RegData)
{
  digitalWrite(load,LOW);
  shiftOut(dataPin, clock, LSBFIRST, 0x44);     // shift data command
  digitalWrite(load,HIGH);
  digitalWrite(load,LOW);
  shiftOut(dataPin, clock, LSBFIRST, 0xC0 | ((8 - AdrCode) << 1)); // shift out Address
  shiftOut(dataPin, clock, LSBFIRST, RegData);  // shift out Data
  digitalWrite(load,HIGH);
}//end displayDATA
void display2DATA(byte AdrCode, byte RegData)
{
  digitalWrite(load2,LOW);
  shiftOut(dataPin2, clock2, LSBFIRST, 0x44);     // shift data command
  digitalWrite(load2,HIGH);
  digitalWrite(load2,LOW);
  shiftOut(dataPin2, clock2, LSBFIRST, 0xC0 | ((8 - AdrCode) << 1)); // shift out Address
  shiftOut(dataPin2, clock2, LSBFIRST, RegData);  // shift out Data
  digitalWrite(load2,HIGH);
}//end display2DATA

//***************************
// Initialize TM1638 .56 LED Display
//***************************
void initdisplay()
{
  digitalWrite(load,HIGH);       //? TM1638 protocol ?
  digitalWrite(clock,HIGH);      //? TM1638 protocol ?
  
  displayCMD(0x40);           //? TM1638 protocol ?
  displayCMD(0x80 | 8 | 4);   //? TM1638 protocol ? active display + mid intensity

  for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
  {
    displayDATA(ChairIdx, 0);             //Clear the display
  }
  //dual display
  digitalWrite(load2,HIGH);       //? TM1638 protocol ?
  digitalWrite(clock2,HIGH);      //? TM1638 protocol ?
  
  display2CMD(0x40);           //? TM1638 protocol ?
  display2CMD(0x80 | 8 | 4);   //? TM1638 protocol ? active display + mid intensity

  for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
  {
    display2DATA(ChairIdx, 0);             //Clear the display
  }
}
#endif

//***************************
// Display MAX7219 .36 test pattern
//***************************
void displayclear()
{
  for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
  {
    displayDATA(ChairIdx+1, digit_);             //Clear the display
    display2DATA(ChairIdx+1, digit_);             //Clear the display
  }
}//end displayclear

//***************************
// Display MAX7219 .36 test pattern
//***************************
void displaytestpattern()
{
//    displayDATA(0x0f, 1);      //display test on
  for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
  {
    displayDATA(ChairIdx+1, digit8 | decpnt);             //Light all segments
    display2DATA(ChairIdx+1, digit8 | decpnt);            //  on both displays
  }
  //********************
  //Sound the buzzer with reboot sound
  //********************
//  int TestTone = 2600;
//  for (int ii=0; ii < 100; ii++)
//  {
//    displaymillis(0, TestTone);
  for (int i=0; i < 4; i++)
  {
//    digitalWrite(buzzer, HIGH);   // turn the LED on (HIGH is the voltage level)
//    toneON(TestTone); 
    toneON(BeepTone); 
    delay(50);               // wait for a moment
//    digitalWrite(buzzer, LOW);    // turn the LED off by making the voltage LOW
    toneOFF();               // turn sound OFF
    delay(50);               // wait for a moment
  }
//    TestTone += 3;
//  }
  delay(200);               // wait for a second
//    displayDATA(0x0f, 0);      //display test off
  displayclear();           //clear the display

}//end displaytestpattern

//***************************
// Display MAX7219 .36 test pattern
//***************************
void displayversion(byte mode, byte view)
{
    text[7] = jjversion[0];    //display module version and config setting
    text[6] = jjversion[1];
    text[5] = jjversion[2];
    text[4] = jjversion[3];
    text[3] = digit_;
    text[2] = digitC;
    text[1] = mode;
    text[0] = view;
//display splash screen for a second
    for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
    {
      displayDATA(ChairIdx+1, text[ChairIdx]);             //Print spash screen info
      display2DATA(ChairIdx+1, text[ChairIdx]);            //  on both displays
    }
    delay(800);               // wait for a second
  displayclear();           //clear the display
}//end displayversion

//***************************
// Display MAX7219 .36 test pattern
//***************************
void displayjumpjudge()
{
  text[7] = digitJ;            //Digit 8 = J
  text[6] = digitM;            //Digit 7 = M
  text[5] = digitP;            //Digit 6 = P
  text[4] = digitJ;            //Digit 5 = J
  text[3] = digitU;            //Digit 4 = U
  text[2] = digitD;            //Digit 3 = d
  text[1] = digitG;            //Digit 2 = g
  text[0] = digitE;            //Digit 1 = E

//display splash screen for a second
  for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
  {
    displayDATA(ChairIdx+1, text[ChairIdx]);             //Print Digit with seat state
    display2DATA(ChairIdx+1, text[ChairIdx]);             //Print Digit with seat state
  }
  delay(500);               // wait for a second
  displayclear();           //clear the display
}//end displayjumpjudge

//***************************
// Blink chair jump order
//***************************
void blinkchair(byte *text, int ChairIdx)
{
/*
          ChairOrder[ChairIdx] = JumpIdx;
          ChairSequence[JumpIdx] = ChairIdx;
          text[ChairIdx] = number[JumpIdx];        //save jump order in chair position


        displayDATA(ChairIdx+1, text[ChairIdx]);      //display jump order
      }
        display2DATA(7-ChairIdx+1, text[ChairIdx]);      //display jump order
*/

  for (int Idx=0; Idx < 8; Idx++)
  {
    if (Idx == ChairIdx)
    {
      if (StopWatchVal == 0)    //if StopWatch is not running
        displayDATA(Idx+1, digit_);             //Clear the chair
      display2DATA(7-Idx+1, digit_);             //Clear the chair
    }
    else
    {
      if (StopWatchVal == 0)    //if StopWatch is not running
        displayDATA(Idx+1, text[Idx]);             //Paint the chair
      display2DATA(7-Idx+1, text[Idx]);             //Paint the chair
    }
  }
}//end blinkchair

//***************************
// Display chair jump order
//***************************
void displaychair(int PaintIdx, int ChairIdx)
{
/*
          ChairOrder[ChairIdx] = JumpIdx;
          ChairSequence[JumpIdx] = ChairIdx;
          text[ChairIdx] = number[JumpIdx];        //save jump order in chair position


        displayDATA(ChairIdx+1, text[ChairIdx]);      //display jump order
      }
        display2DATA(7-ChairIdx+1, text[ChairIdx]);      //display jump order
*/

  for (int Idx=0; Idx < 8; Idx++)
  {
    if (Idx == ChairIdx)
    {
      if (StopWatchVal == 0)    //if StopWatch is not running
        displayDATA(Idx+1, number[PaintIdx]);             //Paint the chair
      display2DATA(7-Idx+1, number[PaintIdx]);             //Paint the chair
    }
    else
    {
      if (StopWatchVal == 0)    //if StopWatch is not running
        displayDATA(Idx+1, digit_);             //Clear the display
      display2DATA(7-Idx+1, digit_);             //Clear the display
    }
  }
}//end displaychair

//***************************
// Display milliseconds
//***************************
void displaymillis(int chairidx, unsigned long timer)
{
  byte textx[8];           //LED storage
  //display chair position Rn or Gn & seconds to three decimal places
//  if (chairidx < 4)
//  {
//    textx[7] = digitG;            //red chair
//    textx[6] = number[4-chairidx];    //chair index
//  }
//  else
//  {
//    textx[7] = digitR;            //green chair
//    textx[6] = number[chairidx-3];    //chair index
//  }
  textx[0] = digit_;    //blank
  textx[1] = digit_;    //blank
  if (timer > 9999) timer = 9999;
  textx[2] = number[timer%10];    //remainder = thousanths digit
  timer /= 10;
  textx[3] = number[timer%10];    //remainder = hundreths digit
  timer /= 10;
  textx[4] = number[timer%10];    //remainder = tenths digit
  textx[5] = number[timer / 10];  //result = ones digit
  textx[5] |= decpnt ;      //include decimal point
  textx[6] = digit_;    //blank
  textx[7] = digit_;    //blank
  //display jump timer
  for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
  {
    if (StopWatchVal == 0)    //if StopWatch is not running
      displayDATA(ChairIdx+1, textx[ChairIdx]);             //Print Jump Timer on front
    display2DATA(ChairIdx+1, textx[ChairIdx]);             //Print Jump Timer on back
  }

}//end displaymillis

/***************************
 Read PC data using SLIP - serial line interface protocol
****************************/
// preserve the SLIP message and attributes between calls
static boolean escapeMode = false;
static byte SLIPmsg[SLIP_MAX];
static int SLIPidx=0;            //count of decoded bytes received
void readSLIP(byte *data, byte *len_ptr)
{
int readChar;
  
  *len_ptr = 0;      //initialize input
  do
  {
    readChar = Serial.read();    //Get a char
    switch (readChar)          //is data available?
    {
    case -1:    //buffer empty
      break;    //do nothing  
    case SLIP_END: //end of message
      if (SLIPidx > 0)      //have any bytes been received?
      {
        while(*len_ptr < SLIPidx) //parse complete SLIP message
        {
          data[*len_ptr] = SLIPmsg[*len_ptr];  //stuff one byte at a time into output buffer
          (*len_ptr)++;
        }
        readChar = -1;    //force an exit 
      }
      SLIPidx = 0;              //reset received byte count
      escapeMode = false;      //reset escape mode
      break;
    case SLIP_ESC: //escape mode
      escapeMode = true;        //turn on escape mode
      break;
    case SLIP_ESC_END: //substitute for end char
      if (escapeMode)  //  if in escape mode
        SLIPmsg[SLIPidx++] = SLIP_END;  //stuff the end char
      else
        SLIPmsg[SLIPidx++] = SLIP_ESC_END;  //otherwise stuff the substitute end char
      escapeMode = false;      //reset escape mode
      break;
    case SLIP_ESC_ESC: //substitute for escape char
      if (escapeMode)  //  if in escape mode
        SLIPmsg[SLIPidx++] = SLIP_ESC;  //stuff the escape char
      else
        SLIPmsg[SLIPidx++] = SLIP_ESC_ESC;  //otherwise stuff the substitute escape char
      escapeMode = false;      //reset escape mode
      break;
    default:
      SLIPmsg[SLIPidx++] = readChar;  //stuff a regular character
      escapeMode = false;      //reset escape mode
    }//end switch
    if (SLIPidx >= SLIP_MAX)  //if message overflow
      SLIPidx = 0;     //then flush the buffer
  }while (readChar > 0);    //stream through the data

}//end readSLIP

/***************************
 Send data back to PC using SLIP - serial line interface protocol
****************************/
void writeSLIP(byte *data, byte length)
{

//  SlipDebug -= 1;
//  Serial.write (SlipDebug);    //Start byte
  Serial.write (SLIP_END);    //Start byte
  for (int i=0; i < length; i++)
  {
    if (data[i] == SLIP_END)
    {
      Serial.write (SLIP_ESC);    //Subsitute byte
      Serial.write (SLIP_ESC_END);    //Subsitute "C0"
    }
    else if (data[i] == SLIP_ESC)
    {
      Serial.write (SLIP_ESC);    //Subsitute byte
      Serial.write (SLIP_ESC_ESC);    //Subsitute "DB"
    }
    else
      Serial.write (data[i]);    //Print real byte
  }
  Serial.write (SLIP_END);    //End byte

}//end writeSLIP

/***************************
 Service USB read from JumpJudge
****************************/
int readUSB()
{
  byte USBreadmsg[SLIP_MAX];
  byte USBwritemsg[SLIP_MAX];
  byte USBlen;
  byte binval;
  int Idx;
  int ChairIdx;
  int retval;
  
  retval = 0;  //default no action
//  if (Serial.available() > 0)
    readSLIP(&USBreadmsg[0], &USBlen); //Read JumpJudge message
  if (USBlen > 0)
  {
  switch (USBreadmsg[0])
    {
    case JJ_SETSTYLE: //set seat style    //input: byte encoded with one bit per seat - Classic = 0, else = 1
      if (USBlen == 2)
      {
        for (int Idx = 0; Idx < 8; Idx++)
        {
          if (USBreadmsg[1] & ChairBit[Idx]) ChairStyle[Idx] = 0;
          else ChairStyle[Idx] = 1;
        }
        jumpjudge = true;
        MaxJumpers = 8;      //always count everyone if jumpjudge is active
      }
      retval = 1;  //return trigger
      break;
    case JJ_SETMASK: //set seat mask     //input: byte encoded with one bit per seat - disabled = 0, enabled = 1
      if (USBlen == 2)
      {
        MaskIdx = 0; 
        NextIdx = 0; 
        for (int Idx = 0; Idx < 8; Idx++)    //find the used seats
        {
          if (USBreadmsg[1] & ChairBit[Idx])  //this seat is used
          {
            NextMask[NextIdx] = Idx;               //This chair is used
            NextIdx++;                             //Count a seat
            if (Latch == 0)
            {
              text[Idx] = digitX;                 //set digit to "-"
              displayDATA(Idx+1, text[Idx]);      //set digit to "-"
              display2DATA(7-Idx+1, text[Idx]);   //set digit to "-"
              ChairMask[MaskIdx] = Idx;           //This chair is used
              MaskIdx++;                          //Count a seat
            }
          }
          else if (Latch == 0)                    //this seat is not used
          {
            text[Idx] = digit_;                   //set digit to " "
            displayDATA(Idx+1, text[Idx]);        //set digit to " "
            display2DATA(7-Idx+1, text[Idx]);     //set digit to " "
          }
        }
      }
      retval = 1;  //return trigger
      break;
    case JJ_SETLATCH: //set seat latch    //input: byte latch off = 0, latch on = 1
      if (USBlen == 2)
      {
        Latch = USBreadmsg[1];
        if (Latch == 0)                        //If latch off
        {                                      //  align ChairMask with NextMask
/*
          for (MaskIdx = 0; MaskIdx < NextIdx; MaskIdx++)    //copy the used seats
          {
            ChairMask[MaskIdx] = NextMask[MaskIdx];  //this seat is used
          }
*/
          MaskIdx = 0; 
          for (int Idx = 0; Idx < 8; Idx++)    //find the used seats
          {
            if ((MaskIdx < NextIdx) && (NextMask[MaskIdx] == Idx))  //this seat is used
            {
              ChairMask[MaskIdx] = Idx;           //This chair is used
//              text[Idx] = digitX;                 //set digit to "-"
              MaskIdx++;                          //Count a seat
            }
            else                                    //this seat is not used
            {
              text[Idx] = digit_;                   //set digit to " "
            }
            displayDATA(Idx+1, text[Idx]);        //set digit to " "
            display2DATA(7-Idx+1, text[Idx]);     //set digit to " "
          }

        }
      }
      break;
    case JJ_GETORDER: //get seat order (input: latch or reset)  //return reboot indicator OR 4 bytes encoded with each seat status & jump order
      if (USBlen == 1)
      {
      }
      break;
    case JJ_GETTIMER: //get seat timers  //return 2 bytes per seat in jump order 0-65535 milliseconds
      if (USBlen == 1)
      {
      }
      break;
    case JJ_VERSION: //get supported version value
      if (USBlen == 1)
      {
        USBwritemsg[0] = JJ_VERSION;      // module tag
        USBwritemsg[1] = jjversion[0];    // module version value
        USBwritemsg[2] = jjversion[1];
        USBwritemsg[3] = jjversion[2];
        USBwritemsg[4] = jjversion[3];
        writeSLIP(USBwritemsg, 5);        //tell JumpJudge the version 
      }
      break;
    default: //unknown message
      {
      }
    }
  }
  return (retval);
}//end ReadUSB

/***************************
 Write state and order to JumpJudge
****************************/
void writeUSB(byte CurrentState)
{
// Serial.print ("USB W"); //Write a string
// Serial.write (89);    //Write a character
  byte USBwritemsg[SLIP_MAX];
  int Idx;
  int MsgLen;

    USBwritemsg[0] = JJ_SETSTATE;      //BitBang tag
    USBwritemsg[1] = CurrentState;      //current state
    writeSLIP(USBwritemsg, 2);  //tell JumpJudge about seat state

    if (CurrentState)
    {      // Dont send jump order if all jumpers down
      USBwritemsg[0] = JJ_SETORDER;      //Seat jump order tag
      for (Idx = 0; Idx < 8; Idx++)
      {
        USBwritemsg[Idx+1] = ChairOrder[ChairMap[Idx]];      //seat jump order
      }
      writeSLIP(USBwritemsg, 9);  //tell JumpJudge about a seat order
    }

/*
    USBwritemsg[0] = JJ_GETORDER;      //BitBang tag
    USBwritemsg[1] = CurrentState;      //current state
    MsgLen = 2;
    for (Idx = 1; Idx < 9; Idx++)
    {
      if (ChairSequence[Idx])      //only send active chairs
      {
        USBwritemsg[Idx+1] = ChairSequence[Idx];      //seat jump order
        MsgLen++;
      }
      else
        break;    //All done with active chairs
    }
    writeSLIP(USBwritemsg, MsgLen);  //tell JumpJudge about seat order
*/
}//end WriteUSB

/***************************
 the startup ceremony discovers active seats and seat style:
****************************/
void startupceremony()
{                
  int counter;
  int SeatState;
    
//  ALL UP
    text[7] = digitA;            //Digit 8 = A
    text[6] = digitL;            //Digit 7 = L
    text[5] = digitL;            //Digit 6 = L
    text[4] = digit_;            //Digit 5 = _
    text[3] = digitU;            //Digit 4 = U
    text[2] = digitP;            //Digit 3 = P
    text[1] = digit_;            //Digit 2 = _
    
   for (int DigitIdx=1; DigitIdx < 8; DigitIdx++)
  {
    text2[DigitIdx] = text[DigitIdx];          //Copy to dual display
  }
   
    ReferenceTime = 0;          //force the buzzer on first pass
    counter = 4;                //start with countdown plus one
//    configTrigger = 0;          //initialize config counter
//    previousPass = 0;
    
    while (counter > 0)
    {
      if (millis() > ReferenceTime)
      {
        counter--;
        ReferenceTime = millis() + 1000;
//        tone(buzzer, BuzzTone);
        toneON(BuzzTone); 
        BuzzerTime = millis() + 10;
      }
      text[0] = number[counter];            //Digit 1 = 5,4,3,2,1,0
      text2[0] = text[0];                   //Copy to dual display
      for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
      {
        ChairStyle[ChairIdx] = digitalRead(ChairPin[ChairIdx]);   // read the chair
        ChairMask[ChairIdx] = ChairStyle[ChairIdx];               //initially set to match
        if (ChairStyle[ChairIdx] == HIGH)
        {
          text[ChairIdx] = text[ChairIdx] | decpnt;      //add a decimal point
          text2[7-ChairIdx] = text2[7-ChairIdx] | decpnt;      //add a decimal point
        }
        else
        {
          text[ChairIdx] = text[ChairIdx] & B01111111;      //remove a decimal point
          text2[7-ChairIdx] = text2[7-ChairIdx] & B01111111;      //remove a decimal point
        }
        displayDATA(ChairIdx+1, text[ChairIdx]);             //Print Digit with seat state
        display2DATA(7-ChairIdx+1, text2[7-ChairIdx]);             //Print Digit with seat state
      }
      if (millis() > BuzzerTime)  //Shut off buzzer
      {
//        noTone(buzzer);    // turn the buzzer off
        toneOFF();               // turn sound OFF
#if defined StopWatchButton
        if (digitalRead(stopwatch) == LOW)   //if stopwatch button pressed
        {
          counter = 0;
        }
#endif
      }
    }//end while counter
//    noTone(buzzer);    // turn the buzzer off
    toneOFF();               // turn sound OFF
    
    //----- loop if configuration changed ---
    
//  ALL DOWN
/*
    text[7] = digitS;            //Digit 8 = S
    text[6] = digitI;            //Digit 7 = I
    text[5] = digitT;            //Digit 6 = T
    text[3] = digitD;            //Digit 4 = D
    text[2] = digitN;            //Digit 3 = N
*/
    text[3] = digit0;            //Digit 4 = O
    text[2] = digitF;            //Digit 3 = F
    text[1] = digitF;            //Digit 3 = F
    
   for (int DigitIdx=1; DigitIdx < 8; DigitIdx++)
  {
    text2[DigitIdx] = text[DigitIdx];          //Copy to dual display
  }
    
    displaytestpattern();
    ReferenceTime = 0;          //force the buzzer on first pass
    counter = 4;                //start with countdown plus one
    
    while (counter > 0)
    {
      if (millis() > ReferenceTime)
      {
        counter--;
        ReferenceTime = millis() + 1000;
//        tone(buzzer, BuzzTone);
        toneON(BuzzTone); 
        BuzzerTime = millis() + 10;
      }
      text[0] = number[counter];            //Digit 1 = 5,4,3,2,1,0
      text2[0] = text[0];
      for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
      {
        SeatState = digitalRead(ChairPin[ChairIdx]);   // read the chair
        if (SeatState != ChairStyle[ChairIdx])
        {
          ChairMask[ChairIdx] = SeatState;             //latch if change (never reset)
        }
        if (ChairStyle[ChairIdx] == ChairMask[ChairIdx])  //if no state change
        {
          displayDATA(ChairIdx+1, text[ChairIdx]);          //Print Digit with seat state
          display2DATA(7-ChairIdx+1, text[7-ChairIdx]);     //Print Digit with seat state on display2
        }
        else                                                //if state changed at least once
        {
          displayDATA(ChairIdx+1, digit8);                  //Print seat active
          display2DATA(7-ChairIdx+1, digit8);               //Print seat active on display2
        }
      }
      if (millis() > BuzzerTime)  //Shut off buzzer
      {
//        noTone(buzzer);    // turn the buzzer off
        toneOFF();               // turn sound OFF
#if defined StopWatchButton
        if (digitalRead(stopwatch) == LOW)   //if stopwatch button pressed
        {
          counter = 0;
        }
#endif
      }
    }
//    noTone(buzzer);    // turn the buzzer off
    toneOFF();               // turn sound OFF
    MaskIdx = 0; 
    for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)    //find the used seats
    {
      if (ChairStyle[ChairIdx] != ChairMask[ChairIdx])
      {
        text[ChairIdx] = digitX;                       //set digit to "-"
        text2[7-ChairIdx] = digitX;                       //set digit to "-"
        displayDATA(ChairIdx+1, text[ChairIdx]);      //set digit to "-"
        display2DATA(7-ChairIdx+1, text2[7-ChairIdx]);      //set digit to "-"
        ChairMask[MaskIdx] = ChairIdx;               //This chair is used
        MaskIdx++;                                   //Count a seat
      }
      else
      {
        text[ChairIdx] = digit_;                       //set digit to " "
        text2[7-ChairIdx] = digit_;                       //set digit to " "
        displayDATA(ChairIdx+1, text[ChairIdx]);      //set digit to " "
        display2DATA(7-ChairIdx+1, text2[7-ChairIdx]);      //set digit to " "
      }
    }
    if (MaskIdx == 0)              //If nothing changed - then default to 6 classic seats
    {                              //  assume all seats initialized to unused & blanked...
      for (int ChairIdx=0; ChairIdx < 3; ChairIdx++)    //initialize as used seats
      {
        text[ChairIdx] = digitX;
        text2[7-ChairIdx] = digitX;
        displayDATA(ChairIdx+1, text[ChairIdx]);      //set digit to "-"
        display2DATA(7-ChairIdx+1, text2[7-ChairIdx]);      //set digit to "-"
        ChairStyle[ChairIdx] = LOW;
        ChairMask[MaskIdx] = ChairIdx;               //This chair is used
        MaskIdx++;                                   //Count a seat
      }
      for (int ChairIdx=5; ChairIdx < 8; ChairIdx++)    //initialize as used seats
      {
        text[ChairIdx] = digitX;
        text2[7-ChairIdx] = digitX;
        displayDATA(ChairIdx+1, text[ChairIdx]);      //set digit to "-"
        display2DATA(7-ChairIdx+1, text2[7-ChairIdx]);      //set digit to "-"
        ChairStyle[ChairIdx] = LOW;
        ChairMask[MaskIdx] = ChairIdx;               //This chair is used
        MaskIdx++;                                   //Count a seat
      }
    }
  for (int DigitIdx=0; DigitIdx < 8; DigitIdx++)
  {
    text2[DigitIdx] = digit_;          //Blank out background text
    NextMask[DigitIdx] = ChairMask[DigitIdx];  //Copy mask array
  }
  NextIdx = MaskIdx;                           //Copy mask index
    
#if defined StopWatchButton
  StopRef = digitalRead(stopwatch);   //init stopwatch button
#endif
  HeartBeat = millis();            //Init loop timer
  ReStartTime = millis() + RestartHold;    //Don't repeat immediately
}//end startupceremony

/***************************
 Initialize parameters before loop:
****************************/
void init_loop()
{
  int ChairIdx;
  
//  if ((StopWatchVal > 0) && (StopState == HIGH))    //if StopWatch is running & button not pressed
  if (StopWatchVal > 0)    //if StopWatch is running
    {                         //  then turn it off
      StopWatchVal = 0;
//---      blank out timer digits!!
//      displayDATA(3+1, digit_);      //blank stopwatch (just in case these chairs are inactive)
//      displayDATA(4+1, digit_);      //blank stopwatch
//      digitalWrite(buzzer, HIGH);    //sound buzzer with stopwatch sound;
      toneON(BeepTone); 
      BuzzerTime = millis() + 100;
    }
    displayclear();           //clear the display
    JumpIdx = 0;
    for (int Idx = 0; Idx < 8; Idx++)
    {
      ChairState[Idx] = LOW;
      ChairOrder[Idx] = 0;
      ChairSequence[Idx] = 0;
      ChairTimer[Idx] = 0;
    }
    for (int Idx = 0; Idx < MaskIdx; Idx++)
    {
      ChairIdx = ChairMask[Idx];  //index through the selection array
      text[ChairIdx] = digitX;                //set digit to "-"
      displayDATA(ChairIdx+1, text[ChairIdx]);
      display2DATA(7-ChairIdx+1, text[ChairIdx]);
    }
    SeatBits = 0;
    PaintIdx = 0;
    PaintMode = 0;
    PaintTime = 0;
    if (QuizMode == 1)  //Practice Jump mode
      PracticeTime = millis()+random(4000)+2000;      //pick a time between 2 & 6 seconds
    if (QuizMode == 2) Latch = 1;  //Latch mode        
}//end init_loop


/***************************
 Configure parameters in eeprom:
****************************/
int ConfigureParameter(int numparm, int maxopts, byte *options)
{
#if defined StopWatchButton
  int optidx;
  int delaycounter;
      // ----- Do Configuration  stuff -----
      // loop through all parameters
      // load text1
      text[7] = digitC;            //Digit 8 = C
      text[6] = digitF;            //Digit 7 = F
      text[5] = digitG;            //Digit 6 = g
      text[4] = number[numparm+1]; //config parm #
      text[3] = digit_;            //Digit 4 = _
      text[2] = digit_;            //Digit 3 = _
      text[1] = digit_;            //Digit 2 = _
      optidx = 0;
      while (true)    // loop until value chosen
      {
        text[0] = options[optidx]; // load value1
      // show config text1 and value1
        for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
        {
          displayDATA(ChairIdx+1, text[ChairIdx]);   //show config text1 and value1 on display1
          display2DATA(ChairIdx+1, text[ChairIdx]);  //show config text1 and value1 on display2
        }
      // wait for button press
        delaycounter = 0;
        while (digitalRead(stopwatch) == HIGH);   //while stopwatch button NOT pressed
        while ((digitalRead(stopwatch) == LOW) && (delaycounter < 7))   //while stopwatch button IS pressed
        {
          delay(250);               // wait for a second
          delaycounter++; 
        }
        if (delaycounter > 6) // if long press then break out of loop
        {
//          tone(buzzer, BuzzTone);
          toneON(BuzzTone); 
          delay(250);               // tone for a 1/4 second
//          noTone(buzzer);
          toneOFF();               // turn sound OFF
          delay(1000);               // give a chance to let up the button
          break;
        }
        if (++optidx >= maxopts)
        {
          optidx = 0; // increment to next value option
        }
      } // endloop
      EEPROM.write(numparm, optidx);  // write value to NVM - EEPROM.write(address_n, value_n);
      // keep local value
      return (optidx);
#endif
}//end ConfigureParameter

/***************************
 the setup routine runs once when you press reset:
****************************/
void setup()
{                
  int configTrigger;
//  int previousPass;
  byte SlipMsg[SLIP_MAX];
  byte SlipLen;
  unsigned int Jumpers;
  byte opt1[] = {digit4,digit8};
  byte opt2[] = {digitJ,digitP,digitL};  //0 = Jump quiz regular, 1 = Practice jump, 2=Latch(table quizzing)
  
  pinMode(buzzer, OUTPUT);     // initialize the buzzer pin as an output.
  pinMode(dataPin, OUTPUT);    // initialize the data pin as an output.
  pinMode(clock, OUTPUT);      // initialize the clock pin as an output.
  pinMode(load, OUTPUT);       // initialize the chipselect pin as an output.
  pinMode(stopwatch, INPUT_PULLUP);   // initialize the timer pin as an input.
  pinMode(dataPin2, OUTPUT);   // initialize the data pin as an output.
  pinMode(clock2, OUTPUT);     // initialize the clock pin as an output.
  pinMode(load2, OUTPUT);      // initialize the chipselect pin as an output.
  for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
  {
    pinMode(ChairPin[ChairIdx], INPUT_PULLUP);      // initialize chairs pins as digital input
  }

  PaintIdx = 0;
  PaintMode = 0;
  PaintTime = 0;
  jumpjudge = false;       //default to standalone
  Serial.begin(115200);      //open the bit bang port
//  delay(2000);               // wait for 2 seconds - blog solution for comm failures

  SlipMsg[0] = JJ_RESET;
  writeSLIP(&SlipMsg[0], 1); //Initiate handshake with JumpJudge
  
  //----- show jumpjudge version and config setting -----
    initdisplay();
    displaytestpattern();
#if defined StopWatchButton
    if (digitalRead(stopwatch) == LOW)   //check stopwatch button
    {
  //----- re-configure settings with stopwatch button -----
      Jumpers = ConfigureParameter(0, 2, opt1);  //0 = 4, 1 = 8  max quizzers displayed
      QuizMode = ConfigureParameter(1, 3, opt2);  //0 = Jump quiz regular, 1 = Practice jump, 2=Latch(table quizzing)
    }
    else //no stopwatch button = normal startup
#endif
    {
  //----- get config settings out of non volatile memory -----
      Jumpers = EEPROM.read(0);  //0 = 4, 1 = 8
      QuizMode = EEPROM.read(1);  //0 = Jump quiz regular, 1 = Practice jump, 2=Latch(table quizzing)
    }
    if (Jumpers > 1) //check all parameter options
    {
      MaxJumpers = 4;  //default value if out of range
    }
    else                  // if out of range
    {
      MaxJumpers = (Jumpers+1)*4;  //calculate max jumpers
    }
    if (QuizMode > 2) //check all parameter options
    {
      QuizMode = 0;  //default value if out of range
    }
    displayversion(opt2[QuizMode], opt1[Jumpers]);

  readUSB(); //Read JumpJudge reply
  if (jumpjudge == false)        //if standalone
  {
    startupceremony();
  }
  else    //JumpJudge active
  {       //  then active seats and seat style initialized through SLIP msgs
    displayjumpjudge();
    QuizMode = 0;  //enforce regular quizzing with PC
  }
  init_loop();
}//end setup

/***************************
 the loop routine runs over and over again forever:
****************************/
void loop() 
{
  byte USBmsg[2];
  int RandomIdx;
  int ChairIdx;
  int SampleArray[8];
  int SampleValue;
  static int SampleData[8];
  static unsigned int SampleDebounce[8];
  boolean AllDown;
  byte State0;
  byte State1;
  int BitVal;
  int StopState;          //StopWatch status

  //********************
  //Read Chairs
  //********************
  
  USBmsg[0] = JJ_SETSTATE;      //BitBang tag
  State1 = State0;
  State0 = 0;
  BitVal = 1;
  for (int Idx = 0; Idx < 8; Idx++)    //get all chairs regardless of mask
  {
    SampleValue = digitalRead(ChairPin[Idx]);   // get bits
    if (SampleData[Idx] != SampleValue)
    {
      SampleDebounce[Idx] += 1;
      if (SampleDebounce[Idx] > DEBOUNCE)
      {
        SampleData[Idx] = SampleValue;
        SampleDebounce[Idx] = 0;
      }
    }
//    SampleData[Idx] = digitalRead(ChairPin[Idx]);   // get bits
    State0 += (SampleData[Idx] * BitVal);   // accumulate bits
    BitVal *= 2;  //Next bit
  }

/*
//is this needed?

  if (jumpjudge && (State0 != State1))
  {
    USBmsg[1] = State0;      //current state
    writeSLIP(USBmsg, 2);  //tell JumpJudge about a state change
    writeUSB(State0);    //tell JumpJudge about a state change
  }
//-?
*/
  for (int Idx = 0; Idx < MaskIdx; Idx++)
  {
    SampleArray[Idx] = ChairMask[Idx];  //Load the selection array
  }
  AllDown = true;    //Assume all seats down (find out if we should reset jump order?)
  for (int Idx = MaskIdx-1; Idx >= 0; --Idx)        //Count backwards
  {
    RandomIdx = random(Idx);
    ChairIdx = SampleArray[RandomIdx];         //Select a random seat to sample
    SampleArray[RandomIdx] = SampleArray[Idx];  //backfill the selection array
    if (SampleData[ChairIdx] == ChairStyle[ChairIdx])   // chair is up!
    {
      AllDown = false;    //NOT all down!
      if (ChairOrder[ChairIdx] == 0)       // Just triggered?
      {
        if (JumpIdx == 0)
        {
          //sound buzzer with jumper sound;
//          digitalWrite(buzzer, HIGH);   // turn the LED on (HIGH is the voltage level)
          toneON(BeepTone); 
          if (QuizMode == 1)  //Practice Jump mode
          {
            if (PracticeTime > 0) //Cancel the timer
              PracticeTime = 0;
          }
          else    //not practice mode
            ReferenceTime = millis();          //start timer to see jump variance
          PaintTime = millis() + 2000;  //start timer
          PaintIdx = 0;                      //init paint index
          PaintMode = 0;
          BuzzerTime = millis() + 100;
          flicker = millis() + 500;
        }
        if (JumpIdx < MaxJumpers)            //Artificially limit jumpers
        {
          JumpIdx++;
          ChairOrder[ChairIdx] = JumpIdx;
          ChairSequence[JumpIdx] = ChairIdx;
          text[ChairIdx] = number[JumpIdx];        //save jump order in chair position
          ChairTimer[ChairIdx] = millis() - ReferenceTime;
        }
      }//endif chair changed
      ChairState[ChairIdx] = HIGH;        // Mark seat up
      text[ChairIdx] = text[ChairIdx] | decpnt;        //save current status
      text2[ChairIdx] = text2[ChairIdx] | decpnt;        //save current status
      if (!(SeatBits & ChairBit[ChairIdx])) //SeatBits = binary version of ChairState: 1=up, 0=dn regardless of seat style  
      {
        SeatBits |= ChairBit[ChairIdx]; //SeatBits = binary version of ChairState: 1=up, 0=dn regardless of seat style  
//        if (jumpjudge && (State0 != State1))
//        {
//SlipDebug = 50;
            writeUSB(SeatBits);    //tell JumpJudge about a state change
//        }
      }
    }//endif chair up
    else //chair is down
    {
      if (SeatBits & ChairBit[ChairIdx])
      {
//        SeatBits = SeatBits & (~(ChairBit[ChairIdx]));
        SeatBits &= ~(ChairBit[ChairIdx]);
//        if (jumpjudge && (State0 != State1))
//        {
//SlipDebug = 20;
            writeUSB(SeatBits);    //tell JumpJudge about a state change
//        }
      }
      ChairState[ChairIdx] = LOW;        // Mark seat down
      text[ChairIdx] = text[ChairIdx] & B01111111;        //save current status
      text2[ChairIdx] = text2[ChairIdx] & B01111111;        //save current status
    }//endif chair down
    if ((PaintIdx == 0)&&(PaintMode == 0))  //paint normal jump order
    {
      if (StopWatchVal == 0)    //if StopWatch is not running
      {                         //  then show jump order
        displayDATA(ChairIdx+1, text[ChairIdx]);      //display jump order
      }
        display2DATA(7-ChairIdx+1, text[ChairIdx]);      //display jump order
    }
  }//endfor check all chairs

  //********************
  //Practice Jump mode
  //********************

  if (QuizMode == 1)  //Practice Jump mode
  {
    if ((PracticeTime > 0) && (PracticeTime < millis()))      //sound buzzer with clap sound;
    {             // 0 = timer expired, >millis = timer active, <millis = sound buzzer
      PracticeTime = 0;              //only buzz once!
      ReferenceTime = millis();          //start timer
//      tone(buzzer, BuzzTone);
      toneON(BuzzTone); 
//      digitalWrite(buzzer, HIGH);   // turn the buzzer on (HIGH is the voltage level)
      BuzzerTime = millis() + 100;
    }  
    if ( JumpIdx && (PaintTime < millis())) //Practice Jump & active jumper & 2 seconds later
    {
      if ((PaintIdx < 3)&&(PaintIdx < JumpIdx))        //only paint active chair timers & max 3
      {
        if (PaintMode < 10 )                  //paint chair order
        {
          PaintMode++;                                   //paint chair order then chair time
          ChairIdx = ChairSequence[PaintIdx+1];                   //Which Chair is painted?
          if (ChairTimer[ChairIdx] <= 5000)              //Dont waste time painting after 5 sec
          {
            if (PaintMode & 1)
            {
//              displaychair(PaintIdx+1, ChairIdx);    //paint next chair order
              blinkchair(text, ChairIdx);    //blink next chair order
            }
            else
            {
//              displayclear();           //clear the display
              if (StopWatchVal == 0)    //if StopWatch is not running
              {                         //  then show jump order
                displayDATA(ChairIdx+1, text[ChairIdx]);      //display jump order
              }
                display2DATA(7-ChairIdx+1, text[ChairIdx]);      //display jump order
            }
            PaintTime = millis() + 100;         //re-init paint time
          }
        }
        else                                //paint chair time
        {
          PaintIdx++;                                    //paint chair times in jump order sequence
          PaintMode = 0;                                   //paint chair order then chair time
          ChairIdx = ChairSequence[PaintIdx];                   //Which Chair is painted?
          if (ChairTimer[ChairIdx] <= 5000)              //Dont waste time painting after 5 sec
          {
            displaymillis(ChairIdx, ChairTimer[ChairIdx]);    //paint next chair timer in sequence
            PaintTime = millis() + 2000;         //re-init paint time
          }
        }
      }
      else            //restart cycle
      {
        PaintIdx = 0;                      //reset paint sequence - Chair status will repaint in loop above
        PaintMode = 0;
//        if (StopWatchVal == 0)    //if StopWatch is not running
//        {                         //  then show jump order
//          displayDATA(3+1, digit_);      //blank stopwatch
//          displayDATA(4+1, digit_);      //blank stopwatch
//        }
//        display2DATA(3+1, digit_);      //blank stopwatch
//        display2DATA(4+1, digit_);      //blank stopwatch
        displayclear();           //clear the display
        PaintTime = millis() + 2000;         //re-init paint time
      }
    }
  }
  //********************
  //Check StopWatch button
  //********************
  
#if defined StopWatchButton
  StopState = digitalRead(stopwatch);   //check stopwatch button
  if ((StopState != StopRef) && (StopState == LOW))  //If stopwatch button changed from not pressed to pressed
  {
    if ((StopWatchVal == 0) && (millis() > StopWatchTime + 250))  //debounce button 1/4 second
    {  //Start the stopwatch?
     if (AllDown == false)  //If at least one jumper is up
     {
      StopWatchVal = 20;    //Start the timer
      StopWatchTime = millis();  //Reference time for stopwatch
      text2[4] = digit2;  //Stuff display value with 20
      text2[3] = digit0;
     }
// if no jumpers & not armed then arm for prejump lock
// else if armed then disarm


      else if (QuizMode == 2)  //Latch mode
      {
        for (int ChairIdx=0; ChairIdx < 8; ChairIdx++)
        {
          displayDATA(ChairIdx, digit8 | decpnt);             //Light all segments
          display2DATA(ChairIdx, digit8 | decpnt);            //  on both displays
        }
        delay(50);               // wait for a moment
        displayclear();           //clear the display
        init_loop();
      }


    }
    else if (millis() > StopWatchTime + 250)  //debounce button 1/4 second
    {  //Stop the stopwatch
      StopWatchTime = millis();  //Reference time for stopwatch debounce
      StopWatchVal = 0;    //Stop the timer
      displayDATA(3+1, digit_);      //blank stopwatch
      displayDATA(4+1, digit_);      //blank stopwatch
    }
    ReStartTime = millis() + RestartHold;    //Start the restart timer every button press
  }//end stopwatch button changed
  if (StopState == HIGH) ReStartTime = 0;  //Stop the restart timer whenever button released
  else if (millis() > ReStartTime)  //three seconds ticked off with button down
  {
  //********************
  // reset seats and start over
  //********************
    initdisplay();                            //4.3.1
    displaytestpattern();
    startupceremony();
    init_loop();
    ReStartTime = millis() + RestartHold;    //Don't repeat immediately
  }
  StopRef = StopState;
  if ((StopWatchVal > 0) && (millis() > StopWatchTime + 1000))  //click off one second
  {
    StopWatchTime = millis();  //Reset reference time for stopwatch
    if (--StopWatchVal > 0)    //Still timing
    {
      if (StopWatchVal > 9)
      {
        text2[4] = digit1;  //Stuff tens digit with one
      }
      else
      {
        text2[4] = digit_;  //Stuff tens digit with space
      }
      text2[3] = number[StopWatchVal % 10]; //Stuff ones digit with remainder
    }
    else      //Time has run out
    {
      displayDATA(3+1, digit_);      //blank stopwatch
      displayDATA(4+1, digit_);      //blank stopwatch
//      digitalWrite(buzzer, HIGH);    //sound buzzer with stopwatch sound;
      toneON(BeepTone); 
      BuzzerTime = millis() + 100;
    }
  }

  if (StopWatchVal > 0)    //if StopWatch is running
  {                         //  then show stopwatch
    for (int Idx = 0; Idx < 8; Idx++)
    {
      displayDATA(Idx+1, text2[Idx]);      //display stopwatch

//---- reverse display2 order!!
//      display2DATA(7-Idx+1, text2[Idx]);   //   and seat status
    }
  }
#endif

  //********************
  // IF 1. All chairs are off
  // &  2. The manual latch is off
  // &  3. flicker debounce time expired
  // &  4. at least one chair had been up since last init time 
  // THEN clear the display!!
  //********************
  if ((AllDown == true) && (Latch == 0) && (millis() > flicker) && PaintTime)
  {
    init_loop();
  }
  if (millis() > BuzzerTime)  //Shut off buzzer
  {
//    noTone(buzzer);
//    digitalWrite(buzzer, LOW);    // turn the LED off by making the voltage LOW
    toneOFF();               // turn sound OFF
  }

  if (millis() > HeartBeat + 250) //heartbeat update every quarter second
  {
    HeartBeat = millis();        //Reset timer
    if (jumpjudge)
    {
//      writeUSB(State0);    //bit bang state to JumpJudge
//      writeUSB(SeatBits);    //bit bang state to JumpJudge
    }
  //********************
  //Service USB read
  //********************
    if (readUSB()) //Read JumpJudge reply
    {
      if (Latch == 0)  //what is this all about??
      {
//        init_loop();
      }
    }
  }
}
