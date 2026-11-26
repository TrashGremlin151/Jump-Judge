# Jump Judge: ESP32 and new-display update

Jump Judge is a quiz-jumping judge. Each quizzer sits on a seat switch, and when a question is read, the box works out who jumped first, second, third and so on, beeps on the first jump, and shows the order.

This repo updates it in three steps:

| # | Goal | Sketch | Board | Status |
|---|------|--------|-------|--------|
| 1 | Replace the Arduino + Bluetooth-module **Quiz Box** with an ESP32 that uses its built-in Bluetooth, feeding the Android tablet app | [`Main_Controller_ESP32/`](Main_Controller_ESP32/Main_Controller_ESP32.ino) | ESP32 DevKit | 
| 2 | Keep J432 but drive the new **74HC595** 8-digit display modules | replaced by the ESP32 Display Box | | The Display Box does J432's job with the new displays, using the screen code from the earlier DisplayBox draft that worked on them. |
| 3 | **Wireless Display Box**: an ESP32 with the two displays that talks to the Quiz Box over Bluetooth, so no tablet is needed | [`Display_Box_ESP32/`](Display_Box_ESP32/Display_Box_ESP32.ino) | ESP32 DevKit |

Everything still to do is in [What still needs to be done](#what-still-needs-to-be-done). In the code, search for `TODO`.

---

## How the pieces fit together

```
  Seat switches ──wires──> QUIZ BOX (ESP32) ──Bluetooth──> Android tablet app
                           Main_Controller_ESP32  └──or──> DISPLAY BOX (ESP32) ──> front + back LED displays
                                                           Display_Box_ESP32
```

The Quiz Box accepts **one** Bluetooth connection at a time: either the tablet or the Display Box.

## Folder layout

```
Main_Controller_ESP32/Main_Controller_ESP32.ino   ESP32 Quiz Box (new)
Display_Box_ESP32/Display_Box_ESP32.ino           ESP32 wireless Display Box (new)
BleQuizBox_Arduino/BleQuizBox_Arduino.ino         Original Bluetooth quiz box sketch (unchanged)
J432_Arduino/J432_Arduino.ino                     Original J432 display box sketch (unchanged)
```

Each sketch is in its own folder with a matching name, because the Arduino IDE needs that. It also compiles every `.ino` in a folder together, so two sketches in one folder give "duplicate `setup()`/`loop()`" errors.

## Arduino IDE setup

- **ESP32 sketches:** in Boards Manager, install **"esp32 by Espressif Systems"**. Choose **Tools → Board → ESP32 Dev Module**. You need an *original* ESP32 (ESP32-WROOM-32 DevKit), because the S2, S3, C3 and C6 chips have no Bluetooth Classic. No extra libraries: `BluetoothSerial` and `Preferences` come with the ESP32 core.
- **Original Arduino sketches:** Arduino Uno / Nano. `BleQuizBox_Arduino` is an unfinished experiment and doesn't compile (see the questions below).

---

## Quiz Box (`Main_Controller_ESP32`)

Reads the seats, works out the jump order, beeps on the first jump, and sends everything over Bluetooth Classic as **"QuizBox5"**.

**Wiring:** each switch goes between its pin and GND. No resistors are needed because the internal pull-ups are used.

| Chair | Pin | | Chair | Pin |
|---|---|---|---|---|
| 1 | D13 | | 5 | D26 |
| 2 | D12 * | | 6 | D25 |
| 3 | D14 | | 7 | D33 |
| 4 | D27 | | 8 | D32 |

| Other | Pin |
|---|---|
| Reset button (to GND) | D15 |
| Piezo buzzer (to GND) | D18 |
| Status LED | D2 (built-in) |

\* D12 is a boot-strapping pin. A switch to GND is fine, but don't add an external pull-up resistor to it or the board won't boot.

**What changed from the first ESP32 version** (the protocol the tablet sees is unchanged):
- **Restored J432's half-second guard:** the order can't auto-clear less than 0.5 s after the first jump. Without it, a seat switch bouncing could clear the round and hand out ranks again.
- **Proper debounce:** each switch has to be steady for 10 ms. This replaces a `delay(3)` inside the seat scan.
- **Sends the moment something changes,** instead of waiting up to 100 ms for the next heartbeat. The 100 ms heartbeat is still there for the tablet's 250 ms timeout.
- **Reset button** clears once per press. Anyone still standing must sit before they can jump again, so they don't instantly take rank 1.
- **A changed seat style or mask starts a fresh round,** so seated quizzers can't be ranked by accident. Sending the same value again does nothing.
- **New commands for the Display Box:** practice mode, reaction times, and a "hold" latch for table quizzing (see [protocol](#bluetooth-protocol-tablet-version)). The tablet never sends these.
- Readable names, comments and layout throughout.

## Wireless Display Box (`Display_Box_ESP32`)

This is the wireless J432. It connects to the Quiz Box in the tablet's place and shows the jump order on two 74HC595 displays: the front display shows chair N at position N, and the back display shows the same mirrored. It keeps the J432 features: the start-up seat check ("ALL UP" / "ALL OFF"), the 20-second stopwatch, practice mode, latch (table quizzing) mode, and the settings menu (hold the button at power-on). The settings are saved in flash.

**Wiring:**

| Signal | Pin |
|---|---|
| DIO, both displays | D32 |
| SCK, both displays | D33 |
| RCK, front display | D25 |
| RCK, back display | D26 |
| Button (to GND) | D27 |
| Piezo buzzer (optional) | D18 (set `BUZZER_PIN = -1` if none) |

These are the same display pins, digit map and screen code as the earlier DisplayBox draft that was tested on the real displays. The only changes: it runs alongside Bluetooth (core 1), and it waits about 1 ms between digits instead of busy-waiting 2 ms.

**Using it:** power on the Quiz Box, then the Display Box. Make sure the tablet isn't connected to the Quiz Box. The display shows `bt Conn` until it finds the Quiz Box, then runs the seat check.

| Button | Does |
|---|---|
| Tap while someone is standing | Start the 20 s stopwatch |
| Tap while the stopwatch runs | Stop it |
| Tap with everyone seated (latch mode) | Clear the order |
| Hold 1.75 s | Redo the seat check |
| Hold while powering on | Settings: max jumpers 4/8, mode J/P/L |

**How it works:** the Display Box speaks exactly the same Bluetooth protocol as the tablet. It runs the seat check itself by reading each seat's switch level from the Quiz Box's status messages, then sends the Quiz Box the seat mask and switch style. If the link drops, it reconnects and sends the setup again. Display refresh runs in its own FreeRTOS task, so nothing else can freeze it.

---

## Bluetooth protocol (tablet version)

Bluetooth Classic serial port (SPP). Every message is wrapped in **SLIP** framing: it starts and ends with `0xC0`, a real `0xC0` byte is sent as `0xDB 0xDC`, and a real `0xDB` as `0xDB 0xDD`. The first byte is the message type.

> The tablet uses codes **80 to 90**. The older USB/PC protocol in J432 uses **0 to 10**, and its message layouts differ too.

| Code | Name | Direction | Payload |
|---|---|---|---|
| 80 | `JJ_RESET` | tablet → box | none. Box replies with `JJ_VERSION` |
| 81 | `JJ_SETSTYLE` | → box | 1 byte: bit N = chair N+1 switch inverted |
| 82 | `JJ_SETMASK` | → box | 1 byte: bit N = chair N+1 in use |
| 83 | `JJ_SETLATCH` | → box | 0 = off, 1 = freeze (tablet), 2 = hold (new, Display Box) |
| 85 | `JJ_GETTIMER` | display → box | none (new). Box replies with `JJ_SETTIMER` |
| 87 | `JJ_SETORDER` | box → | 8 bytes, one per chair (below). Sent on every change, and at least every 100 ms |
| 88 | `JJ_SETTIMER` | box → display | 16 bytes: 2 per chair, high byte first, ms (`0xFFFF` = none) (new) |
| 89 | `JJ_VERSION` | box → tablet | `9, 30, 102, 79` (copied from the first ESP32 version, see TODO) |
| 90 | `JJ_PRACTICE` | display → box | none (new): beep "go!" after a random 2 to 6 s |

`JJ_SETORDER` byte for each chair:

```
bit:   7     6          5        4         3 2 1 0
       0   inverted   in use   up now    rank (0 = hasn't jumped)
```

---

## What still needs to be done

### Test on real hardware
- [ ] **Quiz Box, the biggest unknown:** flash the Quiz Box and check that the tablet app connects to "QuizBox5" and shows jumps. The protocol bytes are copied unchanged from the first ESP32 version, whose comments mention fixes made for the tablet (e.g. its 250 ms timeout).
- [ ] **Quiz Box:** the old box had an **HC-10** module, which is Bluetooth **Low Energy**, but this code (like the first ESP32 version) uses Bluetooth **Classic**. If the tablet can't find "QuizBox5", the app probably expects BLE, and the Quiz Box would need to act like an HC-10 (service `FFE0`, characteristic `FFE1`). The free **nRF Connect** app on a phone shows what the old box advertises.
- [ ] **Display Box:** time how long the Bluetooth connection takes. Connecting by name can take several seconds. If it's annoying, connect by the Quiz Box's Bluetooth address instead (TODO in `connectToQuizBox()`).
- [ ] Watch for seats triggering by themselves. If that happens, raise `DEBOUNCE_MS`.

### Open questions
- [ ] The `JJ_VERSION` reply is `89, 9, 30, 102, 79`. Bytes 2 to 4 are "J", "4", "3" in display font, but byte 1 is the old J432 code 9 and the final "2" is missing. Is that what the tablet expects?
- [ ] Latch meaning: in the first ESP32 version (and so for the tablet), latch **freezes** the order and no new jumps count. In J432 (PC), latch only stops auto-clearing and new jumps still count. Both are supported (`1` = freeze, `2` = hold). Which does the tablet app actually mean?
- [ ] Does the tablet ever send `JJ_GETORDER` (84) and expect a reply?
- [ ] Default seat switch style: the Quiz Box assumes "switch closed (LOW) = jumped" until told otherwise, which matches J432's classic seats. Is that right for all the seats in use?
- [ ] `BleQuizBox_Arduino` is an unfinished experiment: it has two `loop()` functions, uses undefined variables, has a malformed UUID, and uses the ArduinoBLE library rather than an HC-10. It would never have compiled. Is the code that actually ran in the old box somewhere?
- [ ] Which way should each display face? Currently the front display has chair 1 at the right-most digit, and the back is mirrored.

### Nice to have
- [ ] A wired Arduino J432 for the new 74HC595 displays, if still wanted.
- [ ] Tablet **and** Display Box at the same time. Bluetooth Classic serial allows one connection, so this would need ESP-NOW between the two ESP32s, or BLE.
- [ ] Let the tablet use practice mode and reaction times (the Quiz Box supports them now).
- [ ] Battery power and enclosures for the ESP32 boxes.

---

## History

- **2017 to 2019:** the original J432 for Arduino with TM1638 displays (JumpJudge protocol 4.3.0 in Mar 2017, 4.3.1 in Apr 2019). Separately, a Bluetooth quiz box for the tablet. Both sketches are in this repo as `J432_Arduino` and `BleQuizBox_Arduino`.
- **Aug 2026:** first ESP32 translation of the Quiz Box (Bluetooth Classic, tablet protocol 80 to 90), and a first 74HC595 display draft whose screen code the Display Box now uses.
- **Nov 2026:** cleanup and completion: this version.
