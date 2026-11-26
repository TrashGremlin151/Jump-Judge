/*
 * ============================================================================
 *  JUMP JUDGE - QUIZ BOX  (ESP32)
 * ============================================================================
 *
 *  WHAT IT DOES
 *    Reads up to 8 quiz-chair seat switches, works out who jumped first,
 *    second, third..., beeps on the first jump, and reports it all over
 *    Bluetooth to EITHER:
 *      - the Android JumpJudge tablet app, OR
 *      - the wireless Display Box (Display_Box_ESP32.ino), which stands in for
 *        the tablet.
 *
 *  BOARD
 *    ESP32 DevKit with the ORIGINAL ESP32 chip (ESP32-WROOM-32).
 *    The ESP32-S2 / S3 / C3 / C6 chips have no Bluetooth Classic and won't work.
 *    Arduino IDE: Boards Manager -> "esp32 by Espressif Systems",
 *    then Tools -> Board -> "ESP32 Dev Module".
 *
 *  WIRING  (each switch goes between its pin and GND. No resistors needed:
 *           the ESP32's built-in pull-ups are used.)
 *
 *      Chair 1 .... D13            Chair 5 .... D26
 *      Chair 2 .... D12  (*)       Chair 6 .... D25
 *      Chair 3 .... D14            Chair 7 .... D33
 *      Chair 4 .... D27            Chair 8 .... D32
 *
 *      Reset button ... D15 -> GND   clears the jump order by hand
 *      Piezo buzzer ... D18 -> GND   beeps on the first jump
 *      Status LED ..... D2           the built-in LED on most DevKits
 *
 *    (*) D12 is a "boot strapping" pin. A switch to GND is fine, but never
 *        add an external pull-UP resistor to D12 or the board won't boot.
 *
 *  BLUETOOTH
 *    Bluetooth Classic serial port (SPP), device name "QuizBox5".
 *    Only ONE device can connect at a time: the tablet OR the Display Box.
 *
 *  HOW A ROUND WORKS
 *    1. Everyone sits. All seats read "down".
 *    2. The first seat to come up gets rank 1 and the buzzer beeps.
 *       The next gets rank 2, and so on (up to 8).
 *    3. When everyone is seated again (and at least 0.5 s has passed since
 *       the first jump) the order clears by itself, unless the tablet or
 *       Display Box has turned on a latch (see LATCH_* below).
 *
 *  STATUS / TO-DO
 *    Search this file for "TODO". The full list is in README.md.
 * ============================================================================
 */

#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth is not enabled. Choose an original-ESP32 board such as 'ESP32 Dev Module'."
#endif
#if !defined(CONFIG_BT_SPP_ENABLED)
#error "Bluetooth Serial (SPP) is only available on the original ESP32 chip."
#endif


// ============================================================================
//  SETTINGS - the things you're most likely to want to change
// ============================================================================

const char *BLUETOOTH_NAME = "QuizBox5";  // the name the tablet / Display Box connects to

const int NUM_CHAIRS = 8;
const int CHAIR_PINS[NUM_CHAIRS] = {13, 12, 14, 27, 26, 25, 33, 32};  // chair 1 ... chair 8
const int RESET_BUTTON_PIN = 15;
const int BUZZER_PIN       = 18;
const int LED_PIN          = 2;

const int JUMP_BEEP_HZ     = 2750;   // first-jump beep
const int PRACTICE_BEEP_HZ = 1050;   // practice-mode "go!" beep
const unsigned long BEEP_LENGTH_MS = 100;

const unsigned long DEBOUNCE_MS          = 10;    // a switch must be steady this long to count
const unsigned long MIN_ROUND_LENGTH_MS  = 500;   // never auto-clear sooner than this after the first jump
const unsigned long HEARTBEAT_MS         = 100;   // send status at least this often (the tablet gives up after 250 ms)
const unsigned long MIN_SEND_GAP_MS      = 10;    // ...and never faster than this
const unsigned long PRACTICE_MIN_WAIT_MS = 2000;  // practice mode: random wait before "go!"
const unsigned long PRACTICE_MAX_WAIT_MS = 6000;


// ============================================================================
//  JUMPJUDGE MESSAGE PROTOCOL
//
//  Every message is wrapped in SLIP framing (see writeSLIP / readBluetooth).
//  The first byte of a message says what kind of message it is.
//
//  These numbers (80-90) are what the Android tablet app uses.
//  Codes marked (Display Box) are only used by the Display Box; the tablet
//  never sends them.
// ============================================================================

const byte JJ_RESET    = 80;  // tablet -> box : "hello". Box replies with VERSION_REPLY.
const byte JJ_SETSTYLE = 81;  // tablet -> box : 1 byte. Bit N set = chair N+1's switch is inverted.
const byte JJ_SETMASK  = 82;  // tablet -> box : 1 byte. Bit N set = chair N+1 is in use.
const byte JJ_SETLATCH = 83;  // tablet -> box : 1 byte. One of the LATCH_* values below.
const byte JJ_GETORDER = 84;  // (not used)
const byte JJ_GETTIMER = 85;  // (Display Box) display -> box : "send me the reaction times"
const byte JJ_SETSTATE = 86;  // (not used: seat state is packed into JJ_SETORDER instead)
const byte JJ_SETORDER = 87;  // box -> tablet : 8 bytes, one per chair. See buildStatusMessage().
const byte JJ_SETTIMER = 88;  // (Display Box) box -> display : 16 bytes, 2 per chair (high byte first), milliseconds
const byte JJ_VERSION  = 89;  // box -> tablet : the reply to JJ_RESET
const byte JJ_PRACTICE = 90;  // (Display Box) display -> box : start a practice round

// Values for JJ_SETLATCH
const byte LATCH_OFF    = 0;  // normal: the order clears by itself when everyone sits down
const byte LATCH_FREEZE = 1;  // tablet "lock": no new jumps are recorded and nothing clears
const byte LATCH_HOLD   = 2;  // (Display Box) table quizzing: new jumps ARE recorded, but the
                              //       order is kept after everyone sits, until the latch is released
// TODO: confirm what the tablet means by latch. "Freeze" is how the
//       earlier ESP32 version behaved. The PC version (J432) behaves like "hold".

// The reply to JJ_RESET. Copied unchanged from the earlier ESP32 version,
// which was tested against the tablet.
// TODO: confirm these bytes. Bytes 2-4 are "J", "4", "3" in display font, but
//       byte 1 is 9 (the OLD J432 code for JJ_VERSION) and the last version
//       digit ("2" = 91) is missing. It may be off by one byte. If the tablet
//       connects happily, leave it alone.
const byte VERSION_REPLY[5] = {JJ_VERSION, 9, 30, 102, 79};

// SLIP framing bytes
const byte SLIP_END     = 0xC0;  // marks the start/end of a message
const byte SLIP_ESC     = 0xDB;  // "the next byte is special"
const byte SLIP_ESC_END = 0xDC;  // ESC + this = a real 0xC0 inside the message
const byte SLIP_ESC_ESC = 0xDD;  // ESC + this = a real 0xDB inside the message
const int  SLIP_MAX     = 100;   // longest message we accept

const unsigned long NO_TIME = 0xFFFF;  // "no reaction time for this chair"


// ============================================================================
//  STATE
// ============================================================================

BluetoothSerial SerialBT;

// --- Settings sent by the tablet / Display Box ---
bool chairInUse[NUM_CHAIRS];      // false = empty seat, ignore it
bool chairInverted[NUM_CHAIRS];   // false: switch CLOSED (pin LOW)  means "jumped"
                                  // true : switch OPEN   (pin HIGH) means "jumped"
byte latchMode = LATCH_OFF;

// --- Debounced switch readings ---
bool pinIsLow[NUM_CHAIRS];                // the steady (debounced) reading
bool rawIsLow[NUM_CHAIRS];                // the latest raw reading
unsigned long rawChangedAt[NUM_CHAIRS];   // when the raw reading last changed

// --- The current round ---
byte jumpRank[NUM_CHAIRS];                // 0 = hasn't jumped. 1 = first, 2 = second...
unsigned long reactionMs[NUM_CHAIRS];     // ms after the reference moment, or NO_TIME
byte nextRank = 1;                        // the rank the next jumper will get
unsigned long firstJumpAt = 0;            // millis() of the first jump
unsigned long referenceAt = 0;            // reaction times are measured from here:
bool referenceValid = false;              //   the first jump, or the practice "go!" beep
bool mustSitFirst[NUM_CHAIRS];            // still standing when the round was cleared:
                                          //   must sit down before they can jump again

// --- Practice mode ---
bool practiceWaiting = false;             // waiting to sound the "go!" beep
unsigned long practiceBeepAt = 0;

// --- Buzzer ---
bool beeping = false;
unsigned long beepEndsAt = 0;

// --- Bluetooth ---
byte rxMessage[SLIP_MAX];                 // message being received
int  rxLength = 0;
bool rxEscape = false;
byte lastSent[1 + NUM_CHAIRS];            // the last status message sent
unsigned long lastSendAt = 0;
bool wasConnected = false;

// --- Reset button ---
bool resetIsPressed = false;
bool resetRawPressed = false;
unsigned long resetChangedAt = 0;


// ============================================================================
//  HELPERS
// ============================================================================

// Is this chair "up" (jumped) right now? Uses the debounced reading.
bool chairIsUp(int chair) {
  return pinIsLow[chair] != chairInverted[chair];   // != on bools works as XOR
}

void startBeep(int hz) {
  tone(BUZZER_PIN, hz);
  digitalWrite(LED_PIN, HIGH);
  beeping = true;
  beepEndsAt = millis() + BEEP_LENGTH_MS;
}

void updateBeep() {
  if (beeping && (long)(millis() - beepEndsAt) >= 0) {
    noTone(BUZZER_PIN);
    digitalWrite(LED_PIN, LOW);
    beeping = false;
  }
}

// Print the jump order to the USB serial monitor (handy for testing).
void printOrder() {
  Serial.print("Jump order: [ ");
  for (int chair = 0; chair < NUM_CHAIRS; chair++) {
    Serial.print(jumpRank[chair]);
    if (chair < NUM_CHAIRS - 1) Serial.print(", ");
  }
  Serial.print(" ]   up: ");
  for (int chair = 0; chair < NUM_CHAIRS; chair++) Serial.print(chairIsUp(chair) ? '^' : '.');
  Serial.println();
}


// ============================================================================
//  SENDING  (box -> tablet / display)
// ============================================================================

// Send one message, wrapped in SLIP framing.
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

// Build the JJ_SETORDER status message. One byte per chair:
//
//     bit:   7    6        5       4       3 2 1 0
//            0  inverted  in use  up now   rank (0 = not jumped)
//
void buildStatusMessage(byte *msg) {
  msg[0] = JJ_SETORDER;
  for (int chair = 0; chair < NUM_CHAIRS; chair++) {
    byte b = jumpRank[chair] & 0x0F;
    if (chairIsUp(chair))     b |= 0x10;
    if (chairInUse[chair])    b |= 0x20;
    if (chairInverted[chair]) b |= 0x40;
    msg[1 + chair] = b;
  }
}

// Send the status message:
//   - right away when anything changed (but not more than every MIN_SEND_GAP_MS)
//   - otherwise every HEARTBEAT_MS so the tablet knows we're still here.
void sendStatusIfNeeded(bool force) {
  byte msg[1 + NUM_CHAIRS];
  buildStatusMessage(msg);

  unsigned long now = millis();
  bool changed      = memcmp(msg, lastSent, sizeof(msg)) != 0;
  bool heartbeatDue = (now - lastSendAt) >= HEARTBEAT_MS;
  bool gapOk        = (now - lastSendAt) >= MIN_SEND_GAP_MS;

  if (force || heartbeatDue || (changed && gapOk)) {
    writeSLIP(msg, sizeof(msg));
    memcpy(lastSent, msg, sizeof(msg));
    lastSendAt = now;
  }
}

// Print the order to the USB serial monitor whenever it changes,
// whether or not anything is connected over Bluetooth.
void printIfChanged() {
  static byte lastPrinted[1 + NUM_CHAIRS];
  byte msg[1 + NUM_CHAIRS];
  buildStatusMessage(msg);
  if (memcmp(msg, lastPrinted, sizeof(msg)) != 0) {
    memcpy(lastPrinted, msg, sizeof(msg));
    printOrder();
  }
}

// Reply to JJ_GETTIMER with every chair's reaction time (2 bytes each, high byte first).
void sendTimers() {
  byte msg[1 + 2 * NUM_CHAIRS];
  msg[0] = JJ_SETTIMER;
  for (int chair = 0; chair < NUM_CHAIRS; chair++) {
    unsigned long t = reactionMs[chair];
    if (t > NO_TIME) t = NO_TIME;
    msg[1 + 2 * chair] = (t >> 8) & 0xFF;
    msg[2 + 2 * chair] = t & 0xFF;
  }
  writeSLIP(msg, sizeof(msg));
}


// ============================================================================
//  ROUNDS
// ============================================================================

// Forget the current jump order and get ready for the next question.
// Anyone still standing (e.g. when the reset button is pressed mid-round)
// has to sit down before they can jump again, so they don't instantly grab
// rank 1 of the new round.
void clearRound() {
  for (int chair = 0; chair < NUM_CHAIRS; chair++) {
    jumpRank[chair] = 0;
    reactionMs[chair] = NO_TIME;
    mustSitFirst[chair] = chairIsUp(chair);
  }
  nextRank = 1;
  referenceValid = false;
  practiceWaiting = false;
}

// Are any in-use chairs up right now?
bool anyChairUp() {
  for (int chair = 0; chair < NUM_CHAIRS; chair++) {
    if (chairInUse[chair] && chairIsUp(chair)) return true;
  }
  return false;
}

// Practice mode (asked for by the Display Box): wait a random 2-6 seconds,
// then beep "go!". Reaction times are measured from that beep.
void startPractice() {
  clearRound();
  practiceWaiting = true;
  practiceBeepAt = millis() + random(PRACTICE_MIN_WAIT_MS, PRACTICE_MAX_WAIT_MS + 1);
}

void updatePractice() {
  if (practiceWaiting && (long)(millis() - practiceBeepAt) >= 0) {
    practiceWaiting = false;
    referenceAt = millis();
    referenceValid = true;
    startBeep(PRACTICE_BEEP_HZ);
  }
}

// Look at every in-use chair and hand out ranks to anyone who just jumped.
//
// FAIRNESS: chairs are checked in a fresh random order every pass, so when
// two people jump in the same instant neither chair is always favoured.
void recordNewJumps() {
  if (latchMode == LATCH_FREEZE) return;   // the tablet has locked the order

  int order[NUM_CHAIRS];
  int count = 0;
  for (int chair = 0; chair < NUM_CHAIRS; chair++) {
    if (chairInUse[chair]) order[count++] = chair;
  }
  // Shuffle (Fisher-Yates)
  for (int i = count - 1; i > 0; i--) {
    int j = random(i + 1);
    int tmp = order[i];
    order[i] = order[j];
    order[j] = tmp;
  }

  for (int k = 0; k < count; k++) {
    int chair = order[k];
    if (!chairIsUp(chair)) {
      mustSitFirst[chair] = false;         // they've sat down: free to jump again
      continue;
    }
    if (mustSitFirst[chair] || jumpRank[chair] != 0 || nextRank > NUM_CHAIRS) continue;

    unsigned long now = millis();
    if (nextRank == 1) {                   // first jumper of the round
      firstJumpAt = now;
      startBeep(JUMP_BEEP_HZ);
      if (practiceWaiting) {
        practiceWaiting = false;           // jumped before "go!": cancel the beep, no times
      } else if (!referenceValid) {
        referenceAt = now;                 // normal round: times are measured from the first jump
        referenceValid = true;
      }
    }
    jumpRank[chair] = nextRank++;
    reactionMs[chair] = referenceValid ? (now - referenceAt) : NO_TIME;
  }
}

// Clear the order by itself once everyone has sat back down.
void autoClearRound() {
  bool roundInProgress = (nextRank > 1);
  bool longEnough = (millis() - firstJumpAt) >= MIN_ROUND_LENGTH_MS;
  if (roundInProgress && latchMode == LATCH_OFF && longEnough && !anyChairUp()) {
    clearRound();
  }
}


// ============================================================================
//  INPUTS
// ============================================================================

// Read every chair switch and debounce it: a new reading only counts once it
// has stayed the same for DEBOUNCE_MS. Every chair gets the same small delay,
// so the jump order is not affected.
// TODO (hardware): if seats ever trigger by themselves, raise DEBOUNCE_MS.
void readChairSwitches() {
  unsigned long now = millis();
  for (int chair = 0; chair < NUM_CHAIRS; chair++) {
    bool low = (digitalRead(CHAIR_PINS[chair]) == LOW);
    if (low != rawIsLow[chair]) {
      rawIsLow[chair] = low;
      rawChangedAt[chair] = now;
    } else if (low != pinIsLow[chair] && (now - rawChangedAt[chair]) >= DEBOUNCE_MS) {
      pinIsLow[chair] = low;
    }
  }
}

// The reset button clears the jump order (once per press).
void checkResetButton() {
  unsigned long now = millis();
  bool pressed = (digitalRead(RESET_BUTTON_PIN) == LOW);
  if (pressed != resetRawPressed) {
    resetRawPressed = pressed;
    resetChangedAt = now;
  } else if (pressed != resetIsPressed && (now - resetChangedAt) >= 20) {
    resetIsPressed = pressed;
    if (pressed) {
      Serial.println("Reset button pressed");
      clearRound();
    }
  }
}


// ============================================================================
//  RECEIVING  (tablet / display -> box)
// ============================================================================

// Act on one complete message.
void handleMessage(const byte *msg, int length) {
  if (length < 1) return;
  byte command = msg[0];

  if (command == JJ_RESET) {
    writeSLIP(VERSION_REPLY, sizeof(VERSION_REPLY));
  }
  else if (command == JJ_SETSTYLE && length >= 2) {
    bool changed = false;
    for (int chair = 0; chair < NUM_CHAIRS; chair++) {
      bool inverted = (msg[1] >> chair) & 1;
      if (inverted != chairInverted[chair]) changed = true;
      chairInverted[chair] = inverted;
    }
    // A new switch style can make seated people suddenly look "up".
    // Start a fresh round so nobody gets a rank by accident.
    if (changed) clearRound();
  }
  else if (command == JJ_SETMASK && length >= 2) {
    bool changed = false;
    for (int chair = 0; chair < NUM_CHAIRS; chair++) {
      bool inUse = (msg[1] >> chair) & 1;
      if (inUse != chairInUse[chair]) changed = true;
      chairInUse[chair] = inUse;
    }
    if (changed) clearRound();
  }
  else if (command == JJ_SETLATCH && length >= 2) {
    if (msg[1] == LATCH_FREEZE)    latchMode = LATCH_FREEZE;
    else if (msg[1] == LATCH_HOLD) latchMode = LATCH_HOLD;
    else                           latchMode = LATCH_OFF;
    // Releasing the latch while everyone is seated clears the round straight away.
    // (The Display Box clears a held round by sending LATCH_OFF then LATCH_HOLD.)
    if (latchMode == LATCH_OFF && !anyChairUp()) clearRound();
  }
  else if (command == JJ_GETTIMER) {
    sendTimers();
  }
  else if (command == JJ_PRACTICE) {
    startPractice();
  }
  // Anything else is ignored.
  // TODO: JJ_GETORDER (84) is defined but nothing uses it. Find out
  //       whether the tablet ever sends it and expects an answer.
}

// Read everything that has arrived over Bluetooth and unwrap the SLIP framing.
void readBluetooth() {
  while (SerialBT.available()) {
    byte c = SerialBT.read();

    if (c == SLIP_END) {                 // end of a message
      if (rxLength > 0) handleMessage(rxMessage, rxLength);
      rxLength = 0;
      rxEscape = false;
      continue;
    }
    if (c == SLIP_ESC) {                 // next byte is an escaped special byte
      rxEscape = true;
      continue;
    }
    if (rxEscape && c == SLIP_ESC_END) c = SLIP_END;
    if (rxEscape && c == SLIP_ESC_ESC) c = SLIP_ESC;
    rxEscape = false;

    if (rxLength < SLIP_MAX) rxMessage[rxLength++] = c;
    else rxLength = 0;                   // too long: throw it away
  }
}


// ============================================================================
//  SETUP AND MAIN LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);

  for (int chair = 0; chair < NUM_CHAIRS; chair++) {
    pinMode(CHAIR_PINS[chair], INPUT_PULLUP);
  }
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  // Defaults until the tablet / Display Box says otherwise:
  // all 8 chairs in use, switch closed (LOW) = jumped.
  // TODO: check the default against the real seats. In J432 the "classic"
  //       seats count as jumped when the pin reads LOW, which matches this.
  delay(5);  // let the pull-ups settle before the first reading
  for (int chair = 0; chair < NUM_CHAIRS; chair++) {
    chairInUse[chair] = true;
    chairInverted[chair] = false;
    bool low = (digitalRead(CHAIR_PINS[chair]) == LOW);
    pinIsLow[chair] = low;
    rawIsLow[chair] = low;
    rawChangedAt[chair] = millis();
  }
  clearRound();

  // The shuffle in recordNewJumps() is fair even without a random seed;
  // it only has to avoid always checking the same chair first.

  // TODO: the old box used an HC-10 module, which is
  //       Bluetooth LOW ENERGY (BLE), but this sketch uses Bluetooth Classic
  //       (like the earlier ESP32 version, whose notes say it worked with the
  //       tablet). If the tablet can't find "QuizBox5", the app probably
  //       expects BLE, and the box would need to act like an HC-10 (service
  //       FFE0, characteristic FFE1) instead. Check with the old box and the
  //       free "nRF Connect" phone app before changing anything.
  SerialBT.begin(BLUETOOTH_NAME);
  Serial.print("Quiz Box ready. Bluetooth name: ");
  Serial.println(BLUETOOTH_NAME);
}

void loop() {
  readBluetooth();        // 1. commands from the tablet / Display Box
  checkResetButton();     // 2. manual reset
  readChairSwitches();    // 3. debounce the seat switches
  updatePractice();       // 4. practice-mode "go!" beep
  recordNewJumps();       // 5. hand out ranks
  autoClearRound();       // 6. clear when everyone is seated again
  updateBeep();           // 7. stop the beep after 100 ms
  printIfChanged();       // 8. debug output on the USB serial monitor

  // 9. Tell the connected device. When something newly connects, send
  //    straight away so it doesn't have to wait for the heartbeat.
  bool connected = SerialBT.hasClient();
  if (connected) sendStatusIfNeeded(!wasConnected);
  wasConnected = connected;
}
