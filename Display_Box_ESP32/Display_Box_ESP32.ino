/*
 * ============================================================================
 *  JUMP JUDGE - WIRELESS DISPLAY BOX  (ESP32)
 * ============================================================================
 *
 *  WHAT IT DOES
 *    Instead of being wired to the seats, it connects over Bluetooth to the Quiz Box (Main_Controller_ESP32.ino), taking
 *    the tablet's place, and shows the jump order on two 8-digit LED displays:
 *
 *      FRONT display : digit 1 (right-most) = chair 1
 *      BACK display  : the same jump order, with the seat positions mirrored
 *                      so it lines up with the chairs from the other side
 *
 *    It also has:
 *      - start-up seat check ("ALL UP", then "ALL OFF") that works out which
 *        seats are in use and which way their switches work
 *      - 20-second answer stopwatch on the button
 *      - practice mode (random "go!" beep, then shows reaction times)
 *      - table-quizzing (latch) mode: the order stays up until the button
 *      - settings menu (hold the button while powering on)
 *
 *  BOARD
 *    ESP32 DevKit with the ORIGINAL ESP32 chip (ESP32-WROOM-32), because the
 *    link to the Quiz Box uses Bluetooth Classic. Arduino IDE board setting:
 *    "ESP32 Dev Module".
 *
 *  DISPLAYS
 *    Two "0.56 inch 8-digit 7-segment LED display module, 74HC595" boards.
 *    These have no memory of their own: only ONE digit is lit at a time, so
 *    the ESP32 must keep cycling through the 8 digits all the time. A
 *    background task (multiplexDisplay) does that, so delay() anywhere else
 *    in the sketch is fine.
 *
 *  WIRING
 *      Both displays share DIO and SCK; each has its own RCK (latch) pin.
 *        DIO (data)  ....... D32  -> DIO on both displays
 *        SCK (clock) ....... D33  -> SCK/SCLK on both displays
 *        RCK front display . D25  -> RCK/RCLK on the front display
 *        RCK back display .. D26  -> RCK/RCLK on the back display
 *        Button ............ D27  -> GND   (stopwatch / restart / settings)
 *        Piezo buzzer ...... D18  -> GND   (optional, set BUZZER_PIN = -1 if none)
 *
 *      Tip: if digits flicker or show garbage, the displays may be powered
 *      from 5 V while the ESP32 sends 3.3 V signals. Power them from 3.3 V.
 *
 *  HOW TO USE
 *    1. Power on the Quiz Box, then this box. Make sure the tablet is NOT
 *       connected to the Quiz Box (only one device can connect at a time).
 *    2. The display shows "bt Conn" until it finds the Quiz Box.
 *    3. Seat check: on "ALL UP" everyone stands; on "ALL OFF" everyone sits.
 *       Seats that changed become active ("-"). Press the button to skip.
 *    4. Quiz! The front display shows each chair's rank where that chair is.
 *       A decimal point means that seat is up right now.
 *    Button:
 *      - tap while someone is standing ... start the 20-second stopwatch
 *      - tap while the stopwatch runs .... stop it
 *      - tap with everyone seated (latch mode only) ... clear the order
 *      - hold 1.75 s ..................... redo the seat check
 *      - hold while powering on .......... settings menu
 *
 *  STATUS / TO-DO
 *    Written to match J432's behaviour. The screen code matches the tested
 *    DisplayBox draft. The rest is not yet tested on real hardware.
 *    Search this file for "TODO". The full list is in README.md.
 * ============================================================================
 */

#include "BluetoothSerial.h"
#include <Preferences.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth is not enabled. Choose an original-ESP32 board such as 'ESP32 Dev Module'."
#endif
#if !defined(CONFIG_BT_SPP_ENABLED)
#error "Bluetooth Serial (SPP) is only available on the original ESP32 chip."
#endif


// ============================================================================
//  SETTINGS - the things you're most likely to want to change
// ============================================================================

const char *QUIZBOX_NAME = "QuizBox5";     // Bluetooth name of the Quiz Box to connect to
const char *MY_NAME      = "JJ-Display";   // this box's own Bluetooth name

// Pins
const int dataPin    = 32;   // DIO, shared by both displays
const int clockPin   = 33;   // SCK, shared by both displays
const int latchPin1  = 25;   // RCK of display 1 (front)
const int latchPin2  = 26;   // RCK of display 2 (back)
const int BUTTON_PIN = 27;   // stopwatch / restart / settings button, to GND
const int BUZZER_PIN = 18;   // piezo to GND, or -1 if there's no buzzer

// Sounds
const int BEEP_TONE_HZ = 2750;    // stopwatch / test pattern
const int BUZZ_TONE_HZ = 1050;    // countdown ticks / menu

// Timing
const unsigned long RESTART_HOLD_MS   = 1750;  // hold the button this long to redo the seat check
const int           STOPWATCH_SECONDS = 20;
const unsigned long LINK_TIMEOUT_MS   = 2000;  // no message for this long = connection lost


// ============================================================================
//  JUMPJUDGE MESSAGE PROTOCOL  (must match Main_Controller_ESP32.ino)
// ============================================================================

const byte JJ_SETSTYLE = 81;  // display -> box : bit N = chair N+1's switch is inverted
const byte JJ_SETMASK  = 82;  // display -> box : bit N = chair N+1 is in use
const byte JJ_SETLATCH = 83;  // display -> box : LATCH_* value
const byte JJ_GETTIMER = 85;  // display -> box : please send reaction times
const byte JJ_SETORDER = 87;  // box -> display : status of all 8 chairs
const byte JJ_SETTIMER = 88;  // box -> display : reaction times, 2 bytes per chair
const byte JJ_PRACTICE = 90;  // display -> box : start a practice round

const byte LATCH_OFF    = 0;  // order clears when everyone sits
const byte LATCH_FREEZE = 1;  // nothing changes (used during the seat check)
const byte LATCH_HOLD   = 2;  // new jumps counted, order kept until released

const byte SLIP_END     = 0xC0;
const byte SLIP_ESC     = 0xDB;
const byte SLIP_ESC_END = 0xDC;
const byte SLIP_ESC_ESC = 0xDD;
const int  SLIP_MAX     = 100;

const unsigned int NO_TIME = 0xFFFF;


// ============================================================================
//  7-SEGMENT FONT
//
//        -- a --          bit 0 = a    bit 4 = e
//       |       |         bit 1 = b    bit 5 = f
//       f       b         bit 2 = c    bit 6 = g
//       |       |         bit 3 = d    bit 7 = decimal point
//        -- g --
//       |       |
//       e       c
//       |       |
//        -- d --   .dp
// ============================================================================

#define digitA 0b01110111
#define digitL 0b00111000
#define digitU 0b00111110
#define digitP 0b01110011
#define digit_ 0b00000000   // blank
#define digitS 0b01101101
#define digitI 0b00010001
#define digitT 0b00110001
#define digitD 0b01011110
#define digitN 0b01010100   // n
#define digitJ 0b00011110
#define digitM 0b00110111
#define digitG 0b01101111
#define digitE 0b01111001
#define digitF 0b01110001
#define digitC 0b00111001
#define digit0 0b00111111
#define digit1 0b00000110
#define digit2 0b01011011
#define digit3 0b01001111
#define digit4 0b01100110
#define digit5 0b01101101
#define digit6 0b01111101
#define digit7 0b00000111
#define digit8 0b01111111
#define digit9 0b01100111
#define digitX 0b01000000   // dash "-"
#define digitR 0b01010000   // r
#define digitB 0b01111100   // b   (for "bt Conn")
#define digitO 0b01011100   // o
#define digitt 0b01111000   // t
#define decpnt 0b10000000

const byte number[10] = {digit0, digit1, digit2, digit3, digit4, digit5, digit6, digit7, digit8, digit9};
const byte jjversion[4] = {digitJ, digit4, digit3, digit2};   // shown on the version screen


// ============================================================================
//  STATE
// ============================================================================

BluetoothSerial SerialBT;
Preferences prefs;

// --- What the Quiz Box last told us about each chair ---
struct Seat {
  byte rank;       // 0 = hasn't jumped, 1 = first...
  bool up;         // seat is up (jumped) right now
  bool inUse;
  bool inverted;   // switch style
};
Seat seat[8];
unsigned int seatTimeMs[8];        // reaction times (practice mode), NO_TIME if unknown
bool haveStatus = false;
unsigned long lastMessageAt = 0;

// --- Bluetooth receive buffer ---
byte rxMessage[SLIP_MAX];
int  rxLength = 0;
bool rxEscape = false;

// --- Settings (saved in flash, changed in the settings menu) ---
int MaxJumpers = 4;                // show ranks 1..4 or 1..8
unsigned int QuizMode = 0;         // 0 = regular jump quiz, 1 = practice jump, 2 = latch (table quizzing)

// --- Seat setup found by the start-up seat check ---
bool seatsConfigured = false;
byte styleBits = 0;                // sent with JJ_SETSTYLE
byte maskBits = 0;                 // sent with JJ_SETMASK

// --- Round / display state ---
bool roundActive = false;          // somebody has a rank
int PaintIdx = 0;                  // practice mode: which jumper is being shown
int PaintMode = 0;                 // practice mode: blink step
unsigned long PaintTime = 0;       // practice mode: next paint step
unsigned long nextTimerRequestAt = 0;

unsigned int StopWatchVal = 0;     // seconds left on the stopwatch, 0 = not running
unsigned long StopWatchTime = 0;
unsigned long ReStartTime = 0;
int StopRef = HIGH;                // button reading last time round
unsigned long BuzzerTime = 0;

// --- Start-up seat check working data ---
byte ceremonyText[8];
bool ceremonyUpIsHigh[8];
bool ceremonyUsed[8];


// ============================================================================
//  BUZZER
// ============================================================================

bool buzzerOn = false;

void toneON(int hz) {
  if (BUZZER_PIN >= 0) tone(BUZZER_PIN, hz);
  buzzerOn = true;
}

// Only call noTone() when the buzzer is actually on: on the ESP32 every
// noTone() call queues work for a background task, so don't spam it.
void toneOFF() {
  if (buzzerOn && BUZZER_PIN >= 0) noTone(BUZZER_PIN);
  buzzerOn = false;
}

// Start a beep that loop() switches off after `ms`.
void beepFor(int hz, unsigned long ms) {
  toneON(hz);
  BuzzerTime = millis() + ms;
}


// ============================================================================
//  DISPLAY DRIVER  (74HC595 modules)
//
//  Lights one digit at a time on both displays, fast enough that all 8 look
//  lit at once.
//    - It runs on core 1 at a slightly higher priority than loop(), because
//      Bluetooth uses core 0 and would make the digits flicker.
//    - It waits with vTaskDelay(1) (about 1 ms) between digits, so the rest
//      of the sketch keeps running. That is about 125 full refreshes a second.
//
//  Digit positions: position 1 = right-most digit, position 8 = left-most. displayDATA() writes display 1 (front),
//  display2DATA() writes display 2 (back).
// ============================================================================

// Maps logical digit positions to physical 74HC595 shift register pins
const byte hwMap[8] = {4, 5, 6, 7, 0, 1, 2, 3};

// Independent memory buffers for the front and back screens
volatile byte dispBuffer1[8] = {0, 0, 0, 0, 0, 0, 0, 0};
volatile byte dispBuffer2[8] = {0, 0, 0, 0, 0, 0, 0, 0};

TaskHandle_t DisplayTask;

// Background task: continuously multiplexes the dual displays
void multiplexDisplay(void *parameter) {
  for (;;) {
    for (int i = 0; i < 8; i++) {

      // Reverse read index to render text correctly from left-to-right
      int bufferIndex = 7 - i;

      // --- DISPLAY 1 (Front) ---
      digitalWrite(latchPin1, LOW);
      shiftOut(dataPin, clockPin, MSBFIRST, ~dispBuffer1[bufferIndex]);
      shiftOut(dataPin, clockPin, MSBFIRST, 1 << hwMap[i]);
      digitalWrite(latchPin1, HIGH);

      // --- DISPLAY 2 (Back) ---
      digitalWrite(latchPin2, LOW);
      shiftOut(dataPin, clockPin, MSBFIRST, ~dispBuffer2[bufferIndex]);
      shiftOut(dataPin, clockPin, MSBFIRST, 1 << hwMap[i]);
      digitalWrite(latchPin2, HIGH);

      vTaskDelay(1);   // ~1 ms per digit
    }
  }
}

// Writes a character to a specific position (1-8) on the front display
void displayDATA(byte AdrCode, byte RegData) {
  if (AdrCode >= 1 && AdrCode <= 8) dispBuffer1[AdrCode - 1] = RegData;
}

// Writes a character to a specific position (1-8) on the back display
void display2DATA(byte AdrCode, byte RegData) {
  if (AdrCode >= 1 && AdrCode <= 8) dispBuffer2[AdrCode - 1] = RegData;
}

void initdisplay() {
  pinMode(dataPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(latchPin1, OUTPUT);
  pinMode(latchPin2, OUTPUT);
  for (int i = 0; i < 8; i++) {
    dispBuffer1[i] = digit_;
    dispBuffer2[i] = digit_;
  }
  // Start the background multiplexing task (core 1, priority 2 - see above)
  xTaskCreatePinnedToCore(multiplexDisplay, "DisplayTask", 10000, NULL, 2, &DisplayTask, 1);
}


// ============================================================================
//  SCREENS
// ============================================================================

// Show the same 8 characters on both displays. text[0] = right-most.
void showText(const byte *text) {
  for (int i = 0; i < 8; i++) {
    displayDATA(i + 1, text[i]);
    display2DATA(i + 1, text[i]);
  }
}

void displayclear() {
  for (int i = 0; i < 8; i++) {
    displayDATA(i + 1, digit_);
    display2DATA(i + 1, digit_);
  }
}

// All segments on + 4 beeps: check for dead LEDs.
void displaytestpattern() {
  for (int i = 0; i < 8; i++) {
    displayDATA(i + 1, digit8 | decpnt);
    display2DATA(i + 1, digit8 | decpnt);
  }
  for (int i = 0; i < 4; i++) {
    toneON(BEEP_TONE_HZ);
    delay(50);
    toneOFF();
    delay(50);
  }
  delay(200);
  displayclear();
}

// "J432 C<mode><jumpers>" for 0.8 s
void displayversion(byte mode, byte view) {
  byte text[8] = {view, mode, digitC, digit_, jjversion[3], jjversion[2], jjversion[1], jjversion[0]};
  showText(text);
  delay(800);
  displayclear();
}

// "bt Conn" while looking for the Quiz Box
void displayconnecting() {
  byte text[8] = {digit_, digitN, digitN, digitO, digitC, digit_, digitt, digitB};
  showText(text);
}

// Reaction time "  S.mmm  " on both displays (practice mode)
void displaymillis(unsigned long timer) {
  byte text[8];
  if (timer > 9999) timer = 9999;
  text[0] = digit_;
  text[1] = digit_;
  text[2] = number[timer % 10];  timer /= 10;   // thousandths
  text[3] = number[timer % 10];  timer /= 10;   // hundredths
  text[4] = number[timer % 10];                 // tenths
  text[5] = number[timer / 10] | decpnt;        // seconds
  text[6] = digit_;
  text[7] = digit_;
  for (int i = 0; i < 8; i++) {
    if (StopWatchVal == 0) displayDATA(i + 1, text[i]);   // front only if stopwatch isn't using it
    display2DATA(i + 1, text[i]);
  }
}

// What chair `chair` shows: "-" until it has a rank, then the rank number,
// plus a decimal point while the seat is up. Unused seats are blank.
byte chairGlyph(int chair) {
  if (!seat[chair].inUse) return digit_;
  byte g = digitX;
  if (seat[chair].rank >= 1 && seat[chair].rank <= MaxJumpers) g = number[seat[chair].rank];
  if (seat[chair].up) g |= decpnt;
  return g;
}

// Draw the jump order. Front: chair N at position N. Back: mirrored.
// blankChair >= 0 leaves that chair dark (used for blinking in practice mode).
void drawJumpOrder(int blankChair) {
  for (int chair = 0; chair < 8; chair++) {
    byte g = (chair == blankChair) ? digit_ : chairGlyph(chair);
    if (StopWatchVal == 0) displayDATA(chair + 1, g);   // stopwatch uses the front display
    display2DATA(8 - chair, g);
  }
}

// The stopwatch on the front display: seconds in positions 4-5, plus a
// decimal point for every seat that's up.
void drawStopwatch() {
  for (int pos = 0; pos < 8; pos++) {
    byte g = digit_;
    if (pos == 4 && StopWatchVal >= 10) g = number[StopWatchVal / 10];
    if (pos == 3) g = number[StopWatchVal % 10];
    if (seat[pos].inUse && seat[pos].up) g |= decpnt;
    displayDATA(pos + 1, g);
  }
}


// ============================================================================
//  BLUETOOTH LINK TO THE QUIZ BOX
// ============================================================================

void writeSLIP(const byte *data, int length) {
  SerialBT.write(SLIP_END);
  for (int i = 0; i < length; i++) {
    if (data[i] == SLIP_END) {
      SerialBT.write(SLIP_ESC);
      SerialBT.write(SLIP_ESC_END);
    } else if (data[i] == SLIP_ESC) {
      SerialBT.write(SLIP_ESC);
      SerialBT.write(SLIP_ESC_ESC);
    } else {
      SerialBT.write(data[i]);
    }
  }
  SerialBT.write(SLIP_END);
}

void sendCommand(byte command) {
  writeSLIP(&command, 1);
}

void sendCommand(byte command, byte value) {
  byte msg[2] = {command, value};
  writeSLIP(msg, 2);
}

void handleMessage(const byte *msg, int length) {
  lastMessageAt = millis();
  if (msg[0] == JJ_SETORDER && length >= 9) {
    for (int chair = 0; chair < 8; chair++) {
      byte b = msg[1 + chair];
      seat[chair].rank     = b & 0x0F;
      seat[chair].up       = b & 0x10;
      seat[chair].inUse    = b & 0x20;
      seat[chair].inverted = b & 0x40;
    }
    haveStatus = true;
  } else if (msg[0] == JJ_SETTIMER && length >= 17) {
    for (int chair = 0; chair < 8; chair++) {
      seatTimeMs[chair] = (msg[1 + 2 * chair] << 8) | msg[2 + 2 * chair];
    }
  }
}

// Read whatever has arrived and unwrap the SLIP framing.
void serviceLink() {
  while (SerialBT.available()) {
    byte c = SerialBT.read();
    if (c == SLIP_END) {
      if (rxLength > 0) handleMessage(rxMessage, rxLength);
      rxLength = 0;
      rxEscape = false;
      continue;
    }
    if (c == SLIP_ESC) {
      rxEscape = true;
      continue;
    }
    if (rxEscape && c == SLIP_ESC_END) c = SLIP_END;
    if (rxEscape && c == SLIP_ESC_ESC) c = SLIP_ESC;
    rxEscape = false;
    if (rxLength < SLIP_MAX) rxMessage[rxLength++] = c;
    else rxLength = 0;
  }
}

// The switch level as the Quiz Box reads it (HIGH = switch open).
// The box reports "up" = (pin LOW) XOR inverted, so we can undo that.
bool seatPinIsHigh(int chair) {
  bool pinLow = seat[chair].up != seat[chair].inverted;
  return !pinLow;
}

// Keep trying until we're connected and the Quiz Box is talking to us.
// TODO: connecting by name takes several seconds. To speed it up, connect
//       by the Quiz Box's Bluetooth address instead (printed in its serial
//       monitor), using SerialBT.connect(address).
void connectToQuizBox() {
  haveStatus = false;
  while (true) {
    displayconnecting();
    Serial.print("Connecting to ");
    Serial.println(QUIZBOX_NAME);
    if (SerialBT.connect(QUIZBOX_NAME)) {
      // Wait up to 2 s for the first status message
      unsigned long start = millis();
      while (!haveStatus && millis() - start < LINK_TIMEOUT_MS) {
        serviceLink();
        delay(5);
      }
      if (haveStatus) break;
      SerialBT.disconnect();
    }
    Serial.println("...not found, retrying");
    delay(1000);
  }
  Serial.println("Connected to the Quiz Box");
  lastMessageAt = millis();
  displayclear();
}

// Is the link still alive? (No message for 2 s = lost. The box sends every 100 ms.)
bool linkIsUp() {
  return SerialBT.connected() && (millis() - lastMessageAt) < LINK_TIMEOUT_MS;
}

void sendSeatSetup() {
  sendCommand(JJ_SETSTYLE, styleBits);
  sendCommand(JJ_SETMASK, maskBits);
}


// ============================================================================
//  ROUNDS
// ============================================================================

// Get ready for the next question.
void newRound() {
  if (StopWatchVal > 0) {           // a round ended with the stopwatch running: stop it
    StopWatchVal = 0;
    beepFor(BEEP_TONE_HZ, 100);
  }
  displayclear();
  PaintIdx = 0;
  PaintMode = 0;
  PaintTime = 0;
  for (int chair = 0; chair < 8; chair++) seatTimeMs[chair] = NO_TIME;

  if (QuizMode == 1) sendCommand(JJ_PRACTICE);   // box beeps "go!" in 2-6 s
}

// Tell the Quiz Box how to behave for the current mode, then start a round.
void startQuizzing() {
  sendCommand(JJ_SETLATCH, QuizMode == 2 ? LATCH_HOLD : LATCH_OFF);
  newRound();
}

// Watch the ranks coming from the Quiz Box for the start and end of a round.
void trackRound() {
  bool anyRank = false;
  for (int chair = 0; chair < 8; chair++) {
    if (seat[chair].rank > 0) anyRank = true;
  }
  if (anyRank && !roundActive) {            // first jump of the round
    PaintTime = millis() + 2000;            // practice mode: start showing times in 2 s
    PaintIdx = 0;
    PaintMode = 0;
  }
  if (!anyRank && roundActive) {            // the Quiz Box cleared the round
    newRound();
  }
  roundActive = anyRank;
}

// Latch mode: clear the held order (box clears when the latch is released).
void clearHeldRound() {
  sendCommand(JJ_SETLATCH, LATCH_OFF);
  sendCommand(JJ_SETLATCH, LATCH_HOLD);
}

bool allSeatsDown() {
  for (int chair = 0; chair < 8; chair++) {
    if (seat[chair].inUse && seat[chair].up) return false;
  }
  return true;
}


// ============================================================================
//  START-UP SEAT CHECK
//
//  Step 1 "ALL UP":  everyone stands. Each seat's switch reading is noted
//                    as that seat's "up" level.
//  Step 2 "ALL OFF": everyone sits. Any seat whose reading changes is in use.
//  If nobody changed, it falls back to 6 classic seats
//  (chairs 1-3 and 6-8).
// ============================================================================

// A 3-2-1-0 countdown, one beep a second. Calls eachPass(value)
// over and over until it's done. The button skips it.
void runCountdown(void (*eachPass)(int)) {
  int value = 4;
  unsigned long nextTickAt = millis();
  unsigned long beepOffAt = 0;
  bool beepOn = false;
  while (value > 0) {
    serviceLink();
    if ((long)(millis() - nextTickAt) >= 0) {
      value--;
      nextTickAt = millis() + 1000;
      toneON(BUZZ_TONE_HZ);
      beepOn = true;
      beepOffAt = millis() + 10;
    }
    eachPass(value);
    if (beepOn && (long)(millis() - beepOffAt) >= 0) {
      toneOFF();
      beepOn = false;
    }
    if (!beepOn && digitalRead(BUTTON_PIN) == LOW) value = 0;   // skip
    delay(2);
  }
  toneOFF();
}

void ceremonyAllUpPass(int value) {
  ceremonyText[0] = number[value];
  for (int chair = 0; chair < 8; chair++) {
    ceremonyUpIsHigh[chair] = seatPinIsHigh(chair);
    byte dp = ceremonyUpIsHigh[chair] ? decpnt : 0;   // dot = this seat reads HIGH
    displayDATA(chair + 1, ceremonyText[chair] | dp);
    display2DATA(8 - chair, ceremonyText[7 - chair] | dp);
  }
}

void ceremonyAllOffPass(int value) {
  ceremonyText[0] = number[value];
  for (int chair = 0; chair < 8; chair++) {
    if (seatPinIsHigh(chair) != ceremonyUpIsHigh[chair]) ceremonyUsed[chair] = true;   // it changed!
    displayDATA(chair + 1, ceremonyUsed[chair] ? digit8 : ceremonyText[chair]);
    display2DATA(8 - chair, ceremonyUsed[chair] ? digit8 : ceremonyText[7 - chair]);
  }
}

void startupceremony() {
  sendCommand(JJ_SETLATCH, LATCH_FREEZE);   // no beeps or ranks from the box during the check

  // Step 1: "ALL UP"
  byte allUp[8] = {digit0, digit_, digitP, digitU, digit_, digitL, digitL, digitA};
  memcpy(ceremonyText, allUp, 8);
  runCountdown(ceremonyAllUpPass);

  // Step 2: "ALL OFF"
  displaytestpattern();
  byte allOff[8] = {digit0, digitF, digitF, digit0, digit_, digitL, digitL, digitA};
  memcpy(ceremonyText, allOff, 8);
  for (int chair = 0; chair < 8; chair++) ceremonyUsed[chair] = false;
  runCountdown(ceremonyAllOffPass);

  // Step 3: work out the seat setup and send it to the Quiz Box
  styleBits = 0;
  maskBits = 0;
  int used = 0;
  for (int chair = 0; chair < 8; chair++) {
    if (ceremonyUsed[chair]) {
      maskBits |= (1 << chair);
      used++;
      // The box counts "up" as (pin LOW) unless inverted, so a seat whose
      // "up" level is HIGH needs inverting.
      if (ceremonyUpIsHigh[chair]) styleBits |= (1 << chair);
    }
  }
  if (used == 0) {
    maskBits = 0b11100111;   // default: chairs 1-3 and 6-8, classic seats (up = LOW)
    styleBits = 0;
  }
  seatsConfigured = true;
  sendSeatSetup();
  Serial.print("Seat check done. mask=");
  Serial.print(maskBits, 16);
  Serial.print(" style=");
  Serial.println(styleBits, 16);

  // Show "-" on the seats in use for a moment
  for (int chair = 0; chair < 8; chair++) {
    byte g = (maskBits & (1 << chair)) ? digitX : digit_;
    displayDATA(chair + 1, g);
    display2DATA(8 - chair, g);
  }
  delay(500);
  ReStartTime = millis() + RESTART_HOLD_MS;
}


// ============================================================================
//  SETTINGS MENU  (saved in flash)
//
//  Shows "CFG<n>  <value>". Tap to step through the values, hold ~1.75 s to
//  choose (it beeps).
// ============================================================================

int ConfigureParameter(int numparm, int maxopts, const byte *options) {
  byte text[8] = {digit_, digit_, digit_, digit_, number[numparm + 1], digitG, digitF, digitC};
  int optidx = 0;
  while (true) {
    text[0] = options[optidx];
    showText(text);
    int held = 0;
    while (digitalRead(BUTTON_PIN) == HIGH) delay(5);                // wait for a press
    while (digitalRead(BUTTON_PIN) == LOW && held < 7) {             // how long is it held?
      delay(250);
      held++;
    }
    if (held > 6) {                                                  // long press: choose this one
      toneON(BUZZ_TONE_HZ);
      delay(250);
      toneOFF();
      delay(1000);                                                   // time to let go
      break;
    }
    optidx = (optidx + 1) % maxopts;                                 // short press: next value
  }
  return optidx;
}


// ============================================================================
//  STOPWATCH BUTTON
// ============================================================================

void handleButton() {
  int StopState = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  if (StopState != StopRef && StopState == LOW) {             // just pressed
    if (StopWatchVal == 0 && now > StopWatchTime + 250) {
      if (!allSeatsDown()) {                                  // someone is up: start the stopwatch
        StopWatchVal = STOPWATCH_SECONDS;
        StopWatchTime = now;
      } else if (QuizMode == 2) {                             // latch mode, all seated: clear the order
        for (int i = 0; i < 8; i++) {
          displayDATA(i + 1, digit8 | decpnt);
          display2DATA(i + 1, digit8 | decpnt);
        }
        delay(50);
        displayclear();
        clearHeldRound();
      }
    } else if (now > StopWatchTime + 250) {                   // stopwatch running: stop it
      StopWatchTime = now;
      StopWatchVal = 0;
      displayDATA(4, digit_);                                 // blank the stopwatch digits
      displayDATA(5, digit_);
    }
    ReStartTime = now + RESTART_HOLD_MS;
  }

  if (StopState == HIGH) {
    ReStartTime = 0;
  } else if (ReStartTime != 0 && now > ReStartTime) {        // held 1.75 s: redo the seat check
    displaytestpattern();
    startupceremony();
    startQuizzing();
    ReStartTime = millis() + RESTART_HOLD_MS;
  }
  StopRef = StopState;

  // Count the stopwatch down once a second
  if (StopWatchVal > 0 && millis() > StopWatchTime + 1000) {
    StopWatchTime = millis();
    if (--StopWatchVal == 0) {                                // time's up
      displayDATA(4, digit_);
      displayDATA(5, digit_);
      beepFor(BEEP_TONE_HZ, 100);
    }
  }
}


// ============================================================================
//  PRACTICE MODE DISPLAY
//
//  2 s after the first jump, for each of the first 3 jumpers: blink their
//  chair for 1 s, then show their reaction time for 2 s. Then show the order
//  for 2 s and repeat, until the round is cleared.
// ============================================================================

// Which chair got this rank? -1 if none.
int chairWithRank(int rank) {
  for (int chair = 0; chair < 8; chair++) {
    if (seat[chair].rank == rank) return chair;
  }
  return -1;
}

int jumpCount() {
  int n = 0;
  for (int chair = 0; chair < 8; chair++) {
    if (seat[chair].rank > n) n = seat[chair].rank;
  }
  return n;
}

unsigned int timeForChair(int chair) {
  return chair < 0 ? NO_TIME : seatTimeMs[chair];
}

void runPracticeDisplay() {
  if (QuizMode != 1 || !roundActive) return;

  // Keep the reaction times fresh while the round is on.
  if ((long)(millis() - nextTimerRequestAt) >= 0) {
    sendCommand(JJ_GETTIMER);
    nextTimerRequestAt = millis() + 250;
  }

  if (millis() < PaintTime) return;
  int jumpers = jumpCount();

  if (PaintIdx < 3 && PaintIdx < jumpers) {
    if (PaintMode < 10) {                                     // blink this jumper's chair
      PaintMode++;
      int chair = chairWithRank(PaintIdx + 1);
      if (timeForChair(chair) <= 5000) {                      // skip jumpers slower than 5 s
        drawJumpOrder((PaintMode & 1) ? chair : -1);
        PaintTime = millis() + 100;
      }
    } else {                                                  // then show their time
      PaintIdx++;
      PaintMode = 0;
      int chair = chairWithRank(PaintIdx);
      if (timeForChair(chair) <= 5000) {
        displaymillis(timeForChair(chair));
        PaintTime = millis() + 2000;
      }
    }
  } else {                                                    // start the cycle again
    PaintIdx = 0;
    PaintMode = 0;
    displayclear();
    PaintTime = millis() + 2000;
  }
}


// ============================================================================
//  SETUP AND MAIN LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  if (BUZZER_PIN >= 0) pinMode(BUZZER_PIN, OUTPUT);
  for (int chair = 0; chair < 8; chair++) seatTimeMs[chair] = NO_TIME;

  initdisplay();
  displaytestpattern();

  // Settings: hold the button at power-on to change them
  const byte opt1[] = {digit4, digit8};                 // most jumpers shown: 4 or 8
  const byte opt2[] = {digitJ, digitP, digitL};         // J = jump quiz, P = practice, L = latch
  prefs.begin("jumpjudge", false);
  unsigned int jumpers;
  if (digitalRead(BUTTON_PIN) == LOW) {
    jumpers  = ConfigureParameter(0, 2, opt1);
    QuizMode = ConfigureParameter(1, 3, opt2);
    prefs.putUChar("jumpers", jumpers);
    prefs.putUChar("mode", QuizMode);
  } else {
    jumpers  = prefs.getUChar("jumpers", 0);
    QuizMode = prefs.getUChar("mode", 0);
  }
  if (jumpers > 1) jumpers = 0;
  if (QuizMode > 2) QuizMode = 0;
  MaxJumpers = (jumpers + 1) * 4;
  displayversion(opt2[QuizMode], opt1[jumpers]);

  // Connect to the Quiz Box, check the seats, and go
  SerialBT.begin(MY_NAME, true);                        // true = we start the connection
  connectToQuizBox();
  startupceremony();
  startQuizzing();
  StopRef = digitalRead(BUTTON_PIN);
}

void loop() {
  // Lost the Quiz Box? Reconnect and send it our seat setup again
  // (it forgets everything if it was switched off).
  if (!linkIsUp()) {
    SerialBT.disconnect();
    connectToQuizBox();
    if (seatsConfigured) sendSeatSetup();
    else startupceremony();
    startQuizzing();
  }

  serviceLink();
  trackRound();
  handleButton();
  runPracticeDisplay();

  // Normal jump-order display (skipped while practice mode is showing times)
  if (PaintIdx == 0 && PaintMode == 0) drawJumpOrder(-1);
  if (StopWatchVal > 0) drawStopwatch();

  if (millis() > BuzzerTime) toneOFF();
  delay(1);
}
