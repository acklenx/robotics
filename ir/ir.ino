// ir.ino   (v0.10.76 - the mecanum robot driven and programmed from the kit's IR remote;
//           L293D shield, Bluetooth on A0/A1 as shipped, HC-SR04 sonar on A2/A3 as shipped,
//           IR receiver on IR_RECEIVER_PIN)
//
// Everything a kid needs works from the remote alone (no phone, no screen):
// hold an arrow to drive, type a program of steps, save it in a slot, run it.
// The robot answers with small moves: a WIGGLE (quick left-right) means "I am
// listening", a NOD (forward-back) means "got it", a slow HEAD SHAKE means "no".
// The sonar stops the robot when anything is nearer than SONAR_STOP_MM ahead.
// It drives on the same motion numbers as mecanum_holonomic.ino (the CONFIG
// constants below, or the set saved on the robot by the Calibrate page), so a
// calibrated robot drives its programs true. The shared code must stay
// identical: node scripts/check-mecanum-shared.js.
//
// THE REMOTE (the kit's 17-key remote: arrows, OK, 0-9, * and #)
//   hold an arrow     drive that way (up forward, down back, left/right slide)
//   hold 1 / 3        turn left / right in place
//   0 or OK           stop
//   * * *             program mode: the robot wiggles and listens
//   * * * * <1-9>     add to the program in that slot: wiggle + nod, its steps are
//                     loaded, then add steps as usual (the 4th * must come within 2 s)
//     * <arrow or 1/3> <digits>   a step: how far in cm (1/3: how many degrees);
//                                 no digits = 25 cm / 90 degrees. The next * ends it.
//     * 7 <cm>  * 9 <cm>          diagonal forward-left / forward-right
//     * 5 <seconds>               pause (no digits = 1 s)
//     * 8 <times>                 do everything before it again, that many times in all
//                                 (no digits = 2; a second 8 repeats only what came after the first)
//     * 0 <cm across>             a circle that wide, one lap, nose pointed at the middle (no digits = 1 m)
//     * * *           done: the robot wiggles and waits for a slot number 1-9
//     <1-9> * *       saved in that slot (nod)
//     OK (after * * *)   try the program now without saving
//   * <1-9>           run the program in that slot (any key stops it). A slot nobody
//                     has saved to runs its built-in demo (1 square .. 9 dance)
//   * 0               program 0: a circle 1 m across, facing in (always this)
//   #                 cancel / stop, anywhere
//   # # #             calibrate (three steps; a test runs when you press OK, * runs it
//                     again, OK confirms a number, # cancels and puts the saved numbers back):
//     1 start power   a short forward pulse; DOWN = a bit less, UP = a bit more, OK keeps it
//     2 one metre     OK drives 1 m; type the cm it really went, OK; then LEFT or RIGHT and
//                     the cm it drifted, OK (OK alone = none)
//     3 four turns    OK turns right 90 four times; RIGHT and the degrees it went past, or
//                     LEFT and the degrees it fell short, OK. Then it saves the numbers.
//
// COMMANDS (USB or Bluetooth, 9600 baud, for the instructor's phone)
//   i<keys>;       press remote keys: U D L R O(k) * # 0-9, e.g. i***;  iU50*;  i*5;
//   p<n>;          run slot n       p?;   list the slots (P,<n>,<steps>; ,builtin = the demo)
//   p<n>=<steps>;  store steps in slot n: f/b/l/r<cm> t<deg> (t-90 = left) q/e<cm> diagonals
//                  p<s> pause n<times> repeat, e.g. p3=f50 t90 n4;
//                  p<n>=;  empties the slot
//   u              one sonar reading (U,<mm>, -1 = nothing within ~4 m)
//   v<f>,<r>,<s>;  keep driving forward / right / clockwise at -100..100 % of full
//                  speed (the Blocks page's keep-driving and the two-stick controller);
//                  stops unless repeated within VECTOR_TIMEOUT_MS. No reply. The sonar
//                  zeroes the forward part when something is ahead.
//   m<bearing>,<mm>,<turn>;  a move exactly as mecanum_holonomic drives it
//   k?;  km<i>=<v>;  kw;  kd;  kN<name>;   the motion numbers and the Bluetooth name (as calibration.ino);
//                  k? also answers K,name,<name>, and the boot lines include ir: name <name>
//   x              stop        ?  this list
// Every remote key the robot hears is echoed as "ir: <key>", so the phone can
// watch what a kid presses.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <avr/eeprom.h>

#define FIRMWARE_VERSION "0.10.76"   // same line as mecanum_holonomic (bump both together)
#define SKETCH_NAME "ir"

// A program step (the IR sketch's own): which way, and how far / how many degrees
enum StepDirection { S_FORWARD, S_BACK, S_LEFT, S_RIGHT, S_TURN_LEFT, S_TURN_RIGHT, S_DIAG_LEFT, S_DIAG_RIGHT, S_WAIT, S_REPEAT, S_CIRCLE };
struct Step {
  byte dir;          // StepDirection
  uint16_t amount;   // cm, or degrees for turns
};
bool movingForward = false;   // the current move goes forward: the sonar may stop it

// ===========================================================================
// CONFIG - Bluetooth (classic SPP module on SoftwareSerial, as shipped)
// ===========================================================================
#define USE_BLUETOOTH_SOFTSERIAL 1
const byte BLUETOOTH_RX_PIN = A0;   // module TXD -> this pin
const byte BLUETOOTH_TX_PIN = A1;   // this pin -> module RXD

#if USE_BLUETOOTH_SOFTSERIAL
#include <SoftwareSerial.h>
SoftwareSerial bluetoothSerial(BLUETOOTH_RX_PIN, BLUETOOTH_TX_PIN);
#endif

// Everything the sketch says goes to USB and, when compiled in, to the
// Bluetooth module too, so the phone hears replies however it connected. The
// Bluetooth bytes wait in a buffer and go out when the IR line is quiet
// (btFlush, below): SoftwareSerial turns interrupts off for every byte it
// sends, and a remote key arriving then would be lost.
void btEnqueue(uint8_t c);
class DualPrint : public Print {
 public:
  size_t write(uint8_t c) override {
    Serial.write(c);
#if USE_BLUETOOTH_SOFTSERIAL
    btEnqueue(c);
#endif
    return 1;
  }
};
DualPrint out;

// Next command byte from either port, or -1.
int readCommandByte() {
  if (Serial.available()) return Serial.read();
#if USE_BLUETOOTH_SOFTSERIAL
  if (bluetoothSerial.available()) return bluetoothSerial.read();
#endif
  return -1;
}

// ===========================================================================
// CONFIG - geometry
// ===========================================================================
const float WHEELBASE_MM      = 150.0;  // front axle centre to rear axle centre
const float TRACK_MM          = 130.0;  // left wheel centre to right wheel centre

// ===========================================================================
// CONFIG - IR remote receiver (VS1838B / HX1838 / KY-022: OUT, VCC 5V, GND)
// ===========================================================================
// Any pin works: the decoder samples the pin from a Timer1 tick (every 100 us)
// instead of a pin interrupt, because SoftwareSerial (the Bluetooth module)
// owns every pin-change interrupt. 10 is the signal pin of the shield's SER1
// servo header: the receiver plugs in, no soldering. (The servo headers'
// hardware PWM is lost; nothing here uses it.)
const byte IR_RECEIVER_PIN = 10;
const unsigned long IR_RELEASE_MS = 250;        // the remote repeats every 108 ms while held
const unsigned long IR_STAR_TIMEOUT_MS = 3000;  // a lone * is forgotten after this

// ===========================================================================
// CONFIG - HC-SR04 sonar (A3 trig / A2 echo as shipped): a stop, nothing more
// ===========================================================================
#define USE_SONAR 1
const byte SONAR_ECHO_PIN = A2;
const byte SONAR_TRIG_PIN = A3;
const int SONAR_STOP_MM = 250;                  // anything nearer ahead stops the robot
const bool SONAR_STOPS_EVERYTHING = false;      // false: only forward motion (so it can back away); true: every move
const unsigned long SONAR_PERIOD_MS = 60;       // how often it looks while moving
const unsigned long SONAR_TIMEOUT_US = 25000;   // round trip, ~4.3 m (the u reading)

// ===========================================================================
// CONFIG - programs and gestures
// ===========================================================================
const byte PROGRAM_SLOTS = 9;                   // * 1 .. * 9
const byte PROGRAM_MAX_STEPS = 16;
const int DEFAULT_STEP_CM = 25;                 // a step with no digits
const int DEFAULT_TURN_DEG = 90;
const int DEFAULT_WAIT_S = 1;
const int DEFAULT_REPEAT_TIMES = 2;
const int DEFAULT_CIRCLE_CM = 100;             // across; * 0 on its own is this circle
const unsigned long STEP_PAUSE_MS = 300;        // breath between steps
const int GESTURE_PWM_OVER_START = 30;          // gesture power: this much over the start power
const unsigned int GESTURE_TWITCH_MS = 70;      // one wiggle / nod twitch
const unsigned int GESTURE_SLOW_MS = 180;       // one head-shake swing

// Programs in EEPROM: 9 slots x 34 bytes at 600..905, before the motion
// numbers at 920 (mecanum_holonomic's sensor settings live at 0 and 400).
const int PROGRAM_EEPROM_ADDRESS = 600;
const byte PROGRAM_MAGIC = 0xA8;   // A8 (0.10.76): 4-bit step kinds (diagonals, pause, repeat)
const byte PROGRAM_SLOT_BYTES = 2 + 2 * PROGRAM_MAX_STEPS;


// ===========================================================================
// CONFIG - motion numbers: the numbers every sketch drives on (the same as
// mecanum_holonomic.ino and calibration.ino: calibrating changes all three).
// ===========================================================================
const int PWM_DEADBAND = 84;                   // one under CURVE_LOW_PWM: anything below is "off"
const int CURVE_LOW_PWM = 85;                  // start power: the highest any robot needs (robot 1: 78 on tired batteries, robot 2: 85, 2026-10-07)
const float SPEED_AT_LOW_PWM = 335.0;         // at CURVE_LOW_PWM 85, robot 2 on fresh batteries, 2026-10-07: 650 mm in 2 s, 1320 in 4 s (robot 1 at 78, tired batteries: 249)
const float HALF_POWER_MM_PER_SEC = 480.0;     // speed at PWM 128 (robot 2, fresh batteries, 2026-10-07; robot 1 tired: 360)
const float SPEED_AT_PWM_192 = 600.0;          // (robot 1 tired: 505)
const float MAX_WHEEL_MM_PER_SEC = 690.0;      // speed at PWM 255 (robot 1 tired: 590)
const float SPIN_EFFICIENCY = 0.612;          // robot 2, 2026-10-07, fit of 0.607 +10, 0.610 +15, 0.614 -7, 0.624 -15: the turn repeats to about +-15 deg, so this is as good as it gets (robot 1: 0.705)
const float STRAFE_EFFICIENCY = 0.540;        // measured 2026-10-07: a 1 m slide went 1200 mm with 0.45
// Sliding sideways, the front wheels get this much MORE of the sideways wheel
// speed and the back this much less (0.064: front 1.064 x, back 0.936 x). The
// back wheels carry more weight and grip the floor better sideways, so with
// equal speeds a slide bends forward and the robot twists; this evens the
// sideways push front to back. Only sliding uses it. Measured 2026-10-07: a
// 1 m slide ended 300 mm ahead with 0.
const float STRAFE_FRONT_BIAS = 0.065;        // robot 2, 2026-10-07: 0.017 twisted left 45 deg (robot 1 held its heading at 0.017)
// Sliding, a stronger diagonal pair creeps the robot forward or back (no
// twist). Two slide-only shares: STRAFE_DIAG_BIAS takes from the FL+RR pair
// and gives to FR+RL (0.1: FL and RR 0.9 x, FR and RL 1.1 x), whichever way
// it slides; STRAFE_FWD_ROLL_BIAS takes from the pair rolling FORWARD and
// gives to the pair rolling backwards (motors weaker in reverse). The
// calibration page's step 10 sets them from a right and a left slide.
const float STRAFE_DIAG_BIAS = 0.34;          // robot 2, 2026-10-07: creep 650 at 0, 450 at 0.237 (heading held). The most the share can do: beyond this the slow pair hits the slide start speed. (Robot 1: 0.101 twisted the slides badly)
const float STRAFE_FWD_ROLL_BIAS = 0.0;       // (0.039 from the same slides, taken out with it)
const long SPIN_COAST_MS = -46;               // robot 2, hard stop, 2026-10-07: a 1 m move went ~1070 with -255 (negative: the motors run late at the start speed)
constexpr float WHEEL_START[4] = { 0, 0, 0, 0 };
constexpr float WHEEL_GAIN[4] = { 1, 1, 1, 1 };
const float STRAFE_START_SPEED = SPEED_AT_LOW_PWM;
const float ACCEL_MM_PER_S2 = 1500;            // moves speed up from the start speed at this (mm/s per second)
const byte SURFACE_TAG = 0;
enum { M_DEADBAND, M_LOW_PWM, M_LOW_SPEED, M_HALF, M_192, M_MAX, M_SPIN_EFF, M_STRAFE_EFF, M_COAST, M_TAG,
       M_GAIN0, M_START0 = M_GAIN0 + 4, M_STRAFE_START = M_START0 + 4, M_ACCEL, M_FRONT_BIAS, M_DIAG_BIAS, M_FWD_ROLL_BIAS, M_COUNT };
#define MOTION_DEFAULTS { PWM_DEADBAND, CURVE_LOW_PWM, SPEED_AT_LOW_PWM, HALF_POWER_MM_PER_SEC, SPEED_AT_PWM_192, \
  MAX_WHEEL_MM_PER_SEC, SPIN_EFFICIENCY, STRAFE_EFFICIENCY, SPIN_COAST_MS, SURFACE_TAG, \
  WHEEL_GAIN[0], WHEEL_GAIN[1], WHEEL_GAIN[2], WHEEL_GAIN[3], WHEEL_START[0], WHEEL_START[1], WHEEL_START[2], WHEEL_START[3], STRAFE_START_SPEED, \
  ACCEL_MM_PER_S2, STRAFE_FRONT_BIAS, STRAFE_DIAG_BIAS, STRAFE_FWD_ROLL_BIAS }
// The numbers the robot drives on: the constants above, unless a set saved
// on the robot (kw) overrides them. km<i>=<value>; changes one live, kw saves
// them all to EEPROM (mecanum_holonomic reads the same place), kd goes back
// to the code's numbers and forgets the saved ones.
float motion[M_COUNT] = MOTION_DEFAULTS;
const float MOTION_DEFAULT_VALUES[M_COUNT] PROGMEM = MOTION_DEFAULTS;

const unsigned long CONTROL_TICK_MS    = 20;      // update rate for time-varying moves
const float DEFAULT_SPIN_DEG_PER_SEC   = 90.0;    // turns in place (m with mm 0)
const float DEMO_SPEED_MM_PER_SEC      = 480.0;   // m moves: the program speed (half power, well off the stall edge)
const unsigned long VECTOR_TIMEOUT_MS  = 500;     // v stops unless repeated within this

// ===========================================================================
// MOTOR SHIELD (L293D, Adafruit Motor Shield v1 layout)
// ===========================================================================
//   shift register:  DATA 8   CLOCK 4   LATCH 12   ENABLE 7 (active LOW)
//   speed PWM:       M1 11    M2 3      M3 6       M4 5
const byte SHIFT_DATA_PIN   = 8;
const byte SHIFT_CLOCK_PIN  = 4;
const byte SHIFT_LATCH_PIN  = 12;
const byte SHIFT_ENABLE_PIN = 7;

struct MotorChannel {
  byte pwmPin;
  byte forwardBit;   // 74HC595 output driven HIGH to run this channel "forward"
  byte reverseBit;   // 74HC595 output driven HIGH to run it the other way
};

// Shield channels M1..M4 in that order. Fixed by the shield; do not edit.
const MotorChannel MOTOR_CHANNELS[4] = {
  { 11, 2, 3 },   // M1
  {  3, 1, 4 },   // M2
  {  6, 5, 7 },   // M3
  {  5, 0, 6 }    // M4
};

// Which shield channel drives which corner (wheel order FL FR RL RR), and
// whether that motor is wired backwards. t0..t3 twitch each channel to check.
struct WheelWiring {
  byte channel;    // 0 = M1, 1 = M2, 2 = M3, 3 = M4
  bool inverted;   // true if the wheel rolls BACKWARDS when asked for forward
};

WheelWiring wheelWiring[4] = {
  { 3, false },   // 0 FL  <- M4
  { 2, false },   // 1 FR  <- M3
  { 0, false },   // 2 RL  <- M1
  { 1, false }    // 3 RR  <- M2
};

int lastWheelPwm[4] = { 0, 0, 0, 0 };   // for telemetry / host tests
byte shiftRegisterState = 0;

void pushShiftRegister() {
  digitalWrite(SHIFT_LATCH_PIN, LOW);
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, MSBFIRST, shiftRegisterState);
  digitalWrite(SHIFT_LATCH_PIN, HIGH);
}

// PWM straight on the timer compare registers of the shield's four speed
// pins: M1 11 = OC2A, M2 3 = OC2B (Timer2), M3 6 = OC0A, M4 5 = OC0B
// (Timer0). Duty 0 disconnects the pin; 255 is a constant HIGH.
void setChannelPwm(byte channelIndex, byte duty) {
  volatile uint8_t *control = channelIndex < 2 ? &TCCR2A : &TCCR0A;
  byte connect = (channelIndex & 1) ? _BV(COM2B1) : _BV(COM2A1);   // same bits in TCCR0A
  switch (channelIndex) {
    case 0: OCR2A = duty; break;
    case 1: OCR2B = duty; break;
    case 2: OCR0A = duty; break;
    default: OCR0B = duty; break;
  }
  if (duty) *control |= connect;
  else *control &= ~connect;
}

// Raw access by shield channel, bypassing the wiring table.
void setChannelPower(int channelIndex, int power) {
  const MotorChannel &channel = MOTOR_CHANNELS[channelIndex];
  bitWrite(shiftRegisterState, channel.forwardBit, power > 0);
  bitWrite(shiftRegisterState, channel.reverseBit, power < 0);
  pushShiftRegister();
  setChannelPwm(channelIndex, abs(power));
}

void initMotorPins() {
  pinMode(SHIFT_DATA_PIN, OUTPUT);
  pinMode(SHIFT_CLOCK_PIN, OUTPUT);
  pinMode(SHIFT_LATCH_PIN, OUTPUT);
  pinMode(SHIFT_ENABLE_PIN, OUTPUT);
  digitalWrite(SHIFT_ENABLE_PIN, HIGH);   // outputs off while we clear the register
  shiftRegisterState = 0;
  pushShiftRegister();
  for (int channelIndex = 0; channelIndex < 4; channelIndex++) {
    digitalWrite(MOTOR_CHANNELS[channelIndex].pwmPin, LOW);
    pinMode(MOTOR_CHANNELS[channelIndex].pwmPin, OUTPUT);
    setChannelPwm(channelIndex, 0);
  }
  digitalWrite(SHIFT_ENABLE_PIN, LOW);    // active low: outputs on
}

// power: -255..255, positive = wheel rolls the robot FORWARD.
void setWheelPower(int wheelIndex, int power) {
  power = constrain(power, -255, 255);
  lastWheelPwm[wheelIndex] = power;
  if (wheelWiring[wheelIndex].inverted) {
    power = -power;
  }
  setChannelPower(wheelWiring[wheelIndex].channel, power);
}

void stopAllWheels() {
  for (int wheelIndex = 0; wheelIndex < 4; wheelIndex++) {
    setWheelPower(wheelIndex, 0);
  }
}

// ===========================================================================
// EEPROM: the motion numbers (the same place and layout as mecanum_holonomic:
// a 2-byte mark, a layout number, then the numbers) and the Bluetooth name
// ===========================================================================
const int MOTION_EEPROM_ADDRESS = 920;
const uint16_t MOTION_MAGIC = 0x4D43;
const byte MOTION_LAYOUT = 4;   // 4 (0.10.72): the three slide shares after the acceleration
bool motionFromEeprom = false;

void saveMotion() {
  eeprom_update_word((uint16_t *)MOTION_EEPROM_ADDRESS, MOTION_MAGIC);
  eeprom_update_byte((uint8_t *)(MOTION_EEPROM_ADDRESS + 2), MOTION_LAYOUT);
  eeprom_update_block(motion, (void *)(MOTION_EEPROM_ADDRESS + 3), sizeof(motion));
  motionFromEeprom = true;
}

void loadMotion() {
  byte layout = eeprom_read_byte((const uint8_t *)(MOTION_EEPROM_ADDRESS + 2));
  motionFromEeprom = eeprom_read_word((const uint16_t *)MOTION_EEPROM_ADDRESS) == MOTION_MAGIC && layout >= 1 && layout <= MOTION_LAYOUT;
  if (motionFromEeprom) {
    // Older layouts are shorter: what they lack keeps its default.
    eeprom_read_block(motion, (const void *)(MOTION_EEPROM_ADDRESS + 3),
                      (layout == 1 ? M_STRAFE_START : layout == 2 ? M_ACCEL : layout == 3 ? M_FRONT_BIAS : M_COUNT) * sizeof(float));
    if (layout == 1) motion[M_STRAFE_START] = motion[M_LOW_SPEED];
  }
}

// Bluetooth name (kN): given to the module at the next start-up
const int BT_NAME_EEPROM_ADDRESS = 1040;   // after the motion numbers (920 + 3 + 23 x 4 = 1015)
const byte BT_NAME_PENDING = 0x5A;

// A name saved with kN: tell the module before anything connects (modules
// only take AT commands while unpaired), and forget it either way. The kit's
// module is a classic HC-06: AT+NAME<name> with NO line end (a line end would
// become part of the name), answer OKsetname. Then, for other firmware, the
// newer HC-06 AT+NAME=<name> and the BLE modules' AT+NAME<name>, both with a
// line end. Between tries a line end and a pause clear what the module made of
// the last one (an ERROR has an O in it too). (write, not print: print would
// pull Print's code in for this port alone.)
void btSend(const char *text, bool fromFlash) {
  char c;
  while ((c = fromFlash ? pgm_read_byte(text) : *text)) {
    bluetoothSerial.write(c);
    text++;
  }
}

// The robot's name: the Bluetooth name last saved with kN (the rename and
// calibration pages). The pending flag is cleared once the module has it; the
// letters stay, so the robot can say who it is ("ir: name Rexy" at boot,
// K,name,Rexy on k?). Nothing printed when it was never named.
void printRobotName(const __FlashStringHelper *prefix) {
  char name[17];
  eeprom_read_block(name, (const void *)(BT_NAME_EEPROM_ADDRESS + 1), 16);
  name[16] = 0;
  if (name[0] < ' ' || name[0] > '~') return;
  for (byte i = 0; i < 16 && name[i]; i++) if (name[i] < ' ' || name[i] > '~') name[i] = 0;
  out.print(prefix);
  out.println(name);
}

// Send an AT command and collect the answer (the HC-06 answers after ~1 s).
byte btAsk(const char *command, const char *argument, char *reply, byte size) {
  while (bluetoothSerial.read() >= 0) {}
  btSend(command, true);
  if (argument) btSend(argument, false);
  byte length = 0;
  for (unsigned long t = millis(); millis() - t < 1500;) {
    int c = bluetoothSerial.read();
    if (c >= ' ' && c <= '~' && length < size - 1) reply[length++] = c;
  }
  reply[length] = 0;
  return length;
}

void bluetoothStartUp() {
  delay(500);   // the module boots too
  char reply[24];
  Serial.print(F("bt module: "));
  Serial.println(btAsk(PSTR("AT+VERSION"), NULL, reply, sizeof(reply)) ? reply : "no answer (something is connected to it)");
  if (eeprom_read_byte((const uint8_t *)BT_NAME_EEPROM_ADDRESS) != BT_NAME_PENDING) return;
  char name[17];
  eeprom_read_block(name, (const void *)(BT_NAME_EEPROM_ADDRESS + 1), 17);
  name[16] = 0;
  eeprom_update_byte((uint8_t *)BT_NAME_EEPROM_ADDRESS, 0);   // once, either way
  bool ok = btAsk(PSTR("AT+NAME"), name, reply, sizeof(reply)) && strstr(reply, "OK");
  out.print(F("bt name: "));
  out.print(name);
  out.println(ok ? F(" - set") : F(" - the module did not answer"));
}

// ===========================================================================
// KINEMATICS AND MOVES (the same as mecanum_holonomic's)
// ===========================================================================
float rotationLeverMm() {
  return (WHEELBASE_MM + TRACK_MM) / 2.0;
}

// ===========================================================================
// KINEMATICS - body velocity -> four wheel speeds (mm/s at the wheel rim)
// (the same as mecanum_holonomic's)
// ===========================================================================
void bodyVelocityToWheelSpeeds(float forwardMmPerSec, float rightMmPerSec,
                               float clockwiseRadPerSec, float wheelSpeeds[4]) {
  float strafe = rightMmPerSec / motion[M_STRAFE_EFF];
  float front  = strafe * (1 + motion[M_FRONT_BIAS]);   // the front wheels' share of the sideways speed
  float back   = strafe * (1 - motion[M_FRONT_BIAS]);
  // Sliding right, FL and RR roll forward and FR and RL backwards; left, the
  // other way round. roll: + when FL and RR are the forward-rolling pair.
  float roll   = rightMmPerSec >= 0 ? motion[M_FWD_ROLL_BIAS] : -motion[M_FWD_ROLL_BIAS];
  float diagFLRR = 1 - motion[M_DIAG_BIAS] - roll;
  float diagFRRL = 1 + motion[M_DIAG_BIAS] + roll;
  float spin   = clockwiseRadPerSec * rotationLeverMm() / motion[M_SPIN_EFF];
  wheelSpeeds[0] = forwardMmPerSec + front * diagFLRR + spin;   // FL
  wheelSpeeds[1] = forwardMmPerSec - front * diagFRRL - spin;   // FR
  wheelSpeeds[2] = forwardMmPerSec - back * diagFRRL + spin;    // RL
  wheelSpeeds[3] = forwardMmPerSec + back * diagFLRR - spin;    // RR
}

float largestMagnitude(float values[4]) {
  float largest = 0;
  for (int i = 0; i < 4; i++) {
    if (fabs(values[i]) > largest) largest = fabs(values[i]);
  }
  return largest;
}

float curvePwm(byte i) { return i < 2 ? motion[i] : i * 64 - (i == 4); }
float curveSpeed(byte i) { return i ? motion[M_LOW_PWM + i] : 0; }

// The start speed for the motion being driven: the strafe one when it goes
// more sideways than forward, else the forward one (driveBody sets it).
float startSpeed = 0;
float startSpeedFor(float forward, float right) {
  return motion[fabs(right) > fabs(forward) ? M_STRAFE_START : M_LOW_SPEED];
}

int wheelSpeedToPwm(byte wheel, float mmPerSec) {
  // Nothing between off and the motors' start speed: below it they hum and
  // do not turn. Under half of it is off, the rest gets the start speed.
  float speed = fabs(mmPerSec);
  if (speed < startSpeed * 0.5) return 0;
  if (speed < startSpeed) speed = startSpeed;
  byte i = 1;
  while (i < 4 && speed > curveSpeed(i)) i++;
  float pwm = curvePwm(i - 1) + (speed - curveSpeed(i - 1)) / (curveSpeed(i) - curveSpeed(i - 1)) * (curvePwm(i) - curvePwm(i - 1));
  // This wheel's own start and strength.
  pwm += motion[M_START0 + wheel] + (pwm - motion[M_DEADBAND]) * (motion[M_GAIN0 + wheel] - 1);
  int magnitude = (int)constrain(pwm + 0.5, 0, 255);
  return (mmPerSec < 0) ? -magnitude : magnitude;
}

// Drive the body at the requested velocity. If a wheel would need more than
// MAX_WHEEL_MM_PER_SEC, ALL four are scaled down together so the motion shape
// is preserved (it just happens slower). Returns the scale applied (<= 1).
float driveBody(float forwardMmPerSec, float rightMmPerSec, float clockwiseRadPerSec) {
  float wheelSpeeds[4];
  bodyVelocityToWheelSpeeds(forwardMmPerSec, rightMmPerSec, clockwiseRadPerSec, wheelSpeeds);
  startSpeed = startSpeedFor(forwardMmPerSec, rightMmPerSec);
  float scale = 1.0;
  float peak = largestMagnitude(wheelSpeeds);
  if (peak > motion[M_MAX]) {
    scale = motion[M_MAX] / peak;
  }
  for (int wheelIndex = 0; wheelIndex < 4; wheelIndex++) {
    setWheelPower(wheelIndex, wheelSpeedToPwm(wheelIndex, wheelSpeeds[wheelIndex] * scale));
  }
  return scale;
}

bool abortRequested = false;   // set by 'x' arriving during any blocking move

bool checkForAbort() {
  btFlush();
  int incoming;
  while ((incoming = readCommandByte()) >= 0) {
    if (incoming == 'x' || incoming == 'X') {
      abortRequested = true;
    }
  }
  if (irTakeKey() >= 0) abortRequested = true;   // any remote key stops a move
  if ((movingForward || SONAR_STOPS_EVERYTHING) && wallAhead()) {
    abortRequested = true;
    out.println(F("stop: something ahead"));
  }
  return abortRequested;
}

float degreesToRadians(float degrees) { return degrees * PI / 180.0; }

// A straight line on a bearing while the body turns: the same speed scaling
// (never faster than the top speed, never slower than the start speed) and
// ramps as mecanum_holonomic's executeTimedBodyMotion, without the gyro.
void executeTimedBodyMotion(float v0Forward, float v0Right, float worldRotationRadPerSec,
                            float spinRadPerSec, float durationSec) {
  if (durationSec <= 0) return;
  float relativeRadPerSec = worldRotationRadPerSec - spinRadPerSec;

  float peak = 0;
  for (int sample = 0; sample <= 36; sample++) {
    float t = durationSec * sample / 36.0;
    float angle = relativeRadPerSec * t;
    float forwardNow = v0Forward * cos(angle) - v0Right * sin(angle);
    float rightNow   = v0Forward * sin(angle) + v0Right * cos(angle);
    float wheelSpeeds[4];
    bodyVelocityToWheelSpeeds(forwardNow, rightNow, spinRadPerSec, wheelSpeeds);
    float sampleMax = largestMagnitude(wheelSpeeds);
    if (sampleMax > peak) peak = sampleMax;
  }
  float least = startSpeedFor(v0Forward, v0Right);
  float scale = 1.0;
  if (peak > motion[M_MAX]) {
    scale = motion[M_MAX] / peak;
    out.println(F("move: slowing"));
  } else if (peak > 0 && peak < least) {
    // Slower than the motors can start: they would run at the start speed
    // anyway, for the whole (too long) time. Go at the start speed, for less time.
    scale = least / peak;
  }
  v0Forward         *= scale;
  v0Right           *= scale;
  spinRadPerSec     *= scale;
  relativeRadPerSec *= scale;
  durationSec       /= scale;

  // Ramps (see mecanum_holonomic): k is the share of the move's speed, s how
  // far along the move is in seconds at full speed; it speeds up at motion[M_ACCEL].
  float rampPerSec = motion[M_ACCEL] / (peak * scale);   // share of the move's speed per second
  float k0 = constrain(least / (peak * scale), 0.05, 1.0);
  float k = k0, s = 0;
  float endAt = durationSec - k0 * motion[M_COAST] / 1000.0;
  unsigned long nextTickAt = millis();

  while (s < endAt) {
    if (checkForAbort()) break;
    if (millis() >= nextTickAt) {
      nextTickAt += CONTROL_TICK_MS;
      float angle = worldRotationRadPerSec * s - spinRadPerSec * s;
      float forwardNow = v0Forward * cos(angle) - v0Right * sin(angle);
      float rightNow   = v0Forward * sin(angle) + v0Right * cos(angle);
      driveBody(k * forwardNow, k * rightNow, k * spinRadPerSec);
      s += k * (CONTROL_TICK_MS / 1000.0);
      // Up at the ramp rate to full; no ramp-down: the move runs at full
      // speed to its end and the motors cut (a hard stop).
      k = min(k + rampPerSec * (CONTROL_TICK_MS / 1000.0), 1.0);
    }
    delay(1);
  }
  stopAllWheels();
}

// Straight line on a bearing (0 = forward, 90 = right) while the body turns
// spinDegrees over the move.
void driveStraightWhileSpinning(float bearingDegrees, float distanceMm,
                                float speedMmPerSec, float spinDegrees) {
  if (speedMmPerSec <= 0 || distanceMm <= 0) return;
  float bearing = degreesToRadians(bearingDegrees);
  float durationSec = distanceMm / speedMmPerSec;
  executeTimedBodyMotion(speedMmPerSec * cos(bearing), speedMmPerSec * sin(bearing),
                         0, degreesToRadians(spinDegrees) / durationSec, durationSec);
}

// Spin about the robot's own centre. Positive = clockwise.
void spinInPlace(float degrees, float degreesPerSec) {
  if (degreesPerSec <= 0 || fabs(degrees) < 0.01) return;
  float sign = (degrees < 0) ? -1.0 : 1.0;
  float durationSec = fabs(degrees) / degreesPerSec;
  executeTimedBodyMotion(0, 0, 0, sign * degreesToRadians(degreesPerSec), durationSec);
}

// "12,-3,40" -> value[0..2] (missing ones stay 0).
void parseThreeInts(const char *text, int value[3]) {
  value[0] = value[1] = value[2] = 0;
  byte n = 0;
  bool negative = false;
  for (const char *c = text; ; c++) {
    if (*c == '-') {
      negative = true;
    } else if (*c >= '0' && *c <= '9') {
      value[n] = value[n] * 10 + (*c - '0');
    } else {
      if (negative) value[n] = -value[n];
      negative = false;
      if (!*c || ++n == 3) break;
    }
  }
}

// K,cal,<the M_ numbers in order>,<lever mm>,<e = EEPROM, d = defaults>
void printMotion() {
  out.print(F("K,cal"));
  for (byte i = 0; i < M_ACCEL; i++) {   // (M_ACCEL comes after the lever, below)
    out.print(',');
    out.print(motion[i], 3);
  }
  out.print(',');
  out.print(rotationLeverMm(), 1);
  // The rest after the lever (apps before them read 19 numbers and the lever, and stop there):
  // acceleration, then the three slide shares.
  for (byte i = M_ACCEL; i < M_COUNT; i++) {
    out.print(',');
    out.print(motion[i], 3);
  }
  out.println(motionFromEeprom ? F(",e") : F(",d"));   // e: saved on the robot, d: the code's
}

// ===========================================================================
// BLUETOOTH OUTPUT - buffered, sent while the IR line is quiet
// ===========================================================================
// The decoder's state (the Timer1 interrupt alone writes these)
volatile uint8_t *irPinRegister;
uint8_t irPinMask;
bool irLastLevel = true;
byte irBitCount = 0xFF;                   // 0xFF = waiting for a leader
uint16_t irBits = 0;                      // the last 16 bits: command, ~command
unsigned long irLastEdgeUs = 0;
// Key presses wait in a small queue (the ISR fills it, irTakeKey empties it),
// so keys pressed while the robot is busy with a gesture or a reply are kept
// in order instead of overwriting each other.
volatile byte irKeyQueue[8];
volatile byte irKeyHead = 0, irKeyTail = 0;
volatile bool irRepeatSeen = false;       // a repeat frame arrived

char btOut[256];
byte btOutHead = 0, btOutTail = 0;   // (byte indexes wrap with the 256-byte buffer)

// No IR edge for 15 ms: between frames, or nobody pressing (a held key
// repeats every 108 ms with ~40 ms of quiet between frames).
bool irQuiet() {
  noInterrupts();
  unsigned long last = irLastEdgeUs;
  interrupts();
  return micros() - last > 15000UL;
}

void btFlush() {
#if USE_BLUETOOTH_SOFTSERIAL
  while (btOutHead != btOutTail && irQuiet()) {
    bluetoothSerial.write(btOut[btOutTail++]);
  }
#endif
}

void btEnqueue(uint8_t c) {
#if USE_BLUETOOTH_SOFTSERIAL
  if ((byte)(btOutHead + 1) == btOutTail) bluetoothSerial.write(btOut[btOutTail++]);   // full: the oldest goes out now
#endif
  btOut[btOutHead++] = c;
}

// A pause that keeps the Bluetooth output flowing
void waitMs(unsigned long ms) {
  for (unsigned long t = millis(); millis() - t < ms;) {
    btFlush();
    delay(1);
  }
}

// ===========================================================================
// IR REMOTE - NEC decoder, sampling the receiver pin every 100 us from Timer1
// ===========================================================================
// The receiver's output idles high and pulls low for each burst of 38 kHz.
// NEC is decoded from the time between FALLING edges alone:
//   13.5 ms  leader (9 ms burst + 4.5 ms space): a new frame, 32 bits follow
//   11.25 ms repeat (9 ms + 2.25 ms): the key is still held, every 108 ms
//   1.125 ms a 0 bit, 2.25 ms a 1 bit, least significant first:
//            address, ~address, command, ~command
// Only the command byte and its inverse are checked, so any address works.
// Sampling at 100 us leaves lots of margin on every one of those times.

ISR(TIMER1_COMPA_vect) {
  bool level = *irPinRegister & irPinMask;
  if (level == irLastLevel) return;
  irLastLevel = level;
  if (level) return;                       // only falling edges
  unsigned long now = micros();
  unsigned long gap = now - irLastEdgeUs;
  irLastEdgeUs = now;
  if (gap > 9500 && gap < 15500) {    // (margin: a byte going out on Bluetooth can delay an edge 1 ms)
    if (gap > 12000) {
      irBitCount = 0;                      // leader: 32 bits follow
    } else {
      irRepeatSeen = true;                 // repeat: the key is still held
      irBitCount = 0xFF;
    }
    return;
  }
  if (irBitCount >= 32) return;
  if (gap < 800 || gap > 2800) {           // not a bit: wait for the next leader
    irBitCount = 0xFF;
    return;
  }
  irBits >>= 1;
  if (gap > 1700) irBits |= 0x8000;        // over 1.7 ms: a 1
  if (++irBitCount == 32) {
    byte command = irBits;
    if ((byte)(command ^ (irBits >> 8)) == 0xFF) {
      byte next = (irKeyHead + 1) & 7;
      if (next != irKeyTail) {            // (a full queue drops the newest key)
        irKeyQueue[irKeyHead] = command;
        irKeyHead = next;
      }
    }
    irBitCount = 0xFF;
  }
}

void irBegin() {
  pinMode(IR_RECEIVER_PIN, INPUT);   // the receiver drives its output (it has its own pull-up)
  irPinRegister = portInputRegister(digitalPinToPort(IR_RECEIVER_PIN));
  irPinMask = digitalPinToBitMask(IR_RECEIVER_PIN);
  TCCR1A = 0;                               // Timer1: CTC, clk/8 = 0.5 us a tick
  TCCR1B = _BV(WGM12) | _BV(CS11);
  OCR1A = 199;                              // 100 us
  TIMSK1 = _BV(OCIE1A);
}

// The command byte of a new key press since the last call, or -1.
int irTakeKey() {
  int key = -1;
  noInterrupts();
  if (irKeyTail != irKeyHead) {
    key = irKeyQueue[irKeyTail];
    irKeyTail = (irKeyTail + 1) & 7;
  }
  interrupts();
  return key;
}

// NEC command byte -> the key's letter: U D L R (arrows), O (OK), * # 0-9.
// The kit's 17-key remote. Unlisted codes are printed, so another remote can
// be added here.
struct IrKeyCode {
  byte code;
  char key;
};
const IrKeyCode IR_KEYS[] PROGMEM = {
  { 0x18, 'U' }, { 0x52, 'D' }, { 0x08, 'L' }, { 0x5A, 'R' }, { 0x1C, 'O' },
  { 0x16, '*' }, { 0x0D, '#' }, { 0x19, '0' },
  { 0x45, '1' }, { 0x46, '2' }, { 0x47, '3' }, { 0x44, '4' }, { 0x40, '5' },
  { 0x43, '6' }, { 0x07, '7' }, { 0x15, '8' }, { 0x09, '9' },
};

char irKeyLetter(byte code) {
  for (byte i = 0; i < sizeof(IR_KEYS) / sizeof(IR_KEYS[0]); i++) {
    if (pgm_read_byte(&IR_KEYS[i].code) == code) return pgm_read_byte(&IR_KEYS[i].key);
  }
  return 0;
}

// ===========================================================================
// SONAR - true when something is nearer than SONAR_STOP_MM
// ===========================================================================
// One ping; mm, or -1 for no echo within timeoutUs. Trig goes back to an
// input afterwards. Blocks for the echo (at most timeoutUs).
long sonarMm(unsigned long timeoutUs) {
#if USE_SONAR
  pinMode(SONAR_TRIG_PIN, OUTPUT);
  digitalWrite(SONAR_TRIG_PIN, HIGH);                // a 10 us pulse starts a ping
  delayMicroseconds(10);
  digitalWrite(SONAR_TRIG_PIN, LOW);
  pinMode(SONAR_TRIG_PIN, INPUT);
  volatile uint8_t *echoPort = portInputRegister(digitalPinToPort(SONAR_ECHO_PIN));
  byte echoMask = digitalPinToBitMask(SONAR_ECHO_PIN);
  unsigned long startedAt = micros();
  while (!(*echoPort & echoMask)) {
    if (micros() - startedAt > 2000) return -1;
  }
  startedAt = micros();
  while (*echoPort & echoMask) {
    if (micros() - startedAt > timeoutUs) return -1;
  }
  return (micros() - startedAt) * 343UL / 2000;      // 343 m/s, there and back
#else
  (void)timeoutUs;
  return -1;
#endif
}

// Looks at most every SONAR_PERIOD_MS, and only as far as it needs to (a
// short timeout: anything farther than the stop distance is "clear").
unsigned long sonarLookedAt = 0;
bool sonarBlocked = false;
bool wallAhead() {
#if USE_SONAR
  if (millis() - sonarLookedAt >= SONAR_PERIOD_MS || sonarLookedAt == 0) {
    sonarLookedAt = millis();
    long mm = sonarMm((unsigned long)SONAR_STOP_MM * 2 * 1000 / 343 + 300);
    sonarBlocked = mm >= 0 && mm < SONAR_STOP_MM;
  }
  return sonarBlocked;
#else
  return false;
#endif
}

// ===========================================================================
// GESTURES - the robot's answers (small raw moves, no curve or trims)
// ===========================================================================
int gesturePower() {
  return constrain((int)motion[M_LOW_PWM] + GESTURE_PWM_OVER_START, 0, 255);
}

// spin: +1 clockwise, -1 counter-clockwise, 0 = everything forward (sign by nod)
void twitch(int spin, int nod, unsigned int ms) {
  int p = gesturePower();
  setWheelPower(0, spin ? spin * p : nod * p);          // FL
  setWheelPower(1, spin ? -spin * p : nod * p);         // FR
  setWheelPower(2, spin ? spin * p : nod * p);          // RL
  setWheelPower(3, spin ? -spin * p : nod * p);         // RR
  waitMs(ms);
}

// "I am listening": a quick left-right-left-right
void gestureWiggle() {
  out.println(F("gesture: wiggle"));
  for (byte i = 0; i < 2; i++) {
    twitch(1, 0, GESTURE_TWITCH_MS);
    twitch(-1, 0, GESTURE_TWITCH_MS);
  }
  stopAllWheels();
}

// "got it": forward, back
void gestureNod() {
  out.println(F("gesture: nod"));
  twitch(0, 1, GESTURE_TWITCH_MS);
  twitch(0, -1, GESTURE_TWITCH_MS);
  stopAllWheels();
}

// "no": one slow swing each way
void gestureNo() {
  out.println(F("gesture: no"));
  twitch(1, 0, GESTURE_SLOW_MS);
  twitch(-1, 0, GESTURE_SLOW_MS);
  stopAllWheels();
}

// ===========================================================================
// PROGRAMS - steps, running, EEPROM slots
// ===========================================================================
const char STEP_LETTERS[] PROGMEM = "fblrttqepnc";   // for the p commands: f b l r t t, q e diagonals, p pause, n repeat, c circle

void printDirection(byte dir) {
  switch (dir) {
    case S_FORWARD: out.print(F("forward")); break;
    case S_BACK: out.print(F("back")); break;
    case S_LEFT: out.print(F("left")); break;
    case S_RIGHT: out.print(F("right")); break;
    case S_TURN_LEFT: out.print(F("turn left")); break;
    case S_TURN_RIGHT: out.print(F("turn right")); break;
    case S_DIAG_LEFT: out.print(F("diagonal left")); break;
    case S_DIAG_RIGHT: out.print(F("diagonal right")); break;
    case S_WAIT: out.print(F("wait")); break;
    case S_CIRCLE: out.print(F("circle, facing in,")); break;
    default: out.print(F("do it all again")); break;
  }
}

bool isTurn(byte dir) { return dir == S_TURN_LEFT || dir == S_TURN_RIGHT; }
bool isDriveStep(byte dir) { return dir != S_WAIT && dir != S_REPEAT && dir != S_CIRCLE; }   // what holding the key drives
int defaultAmount(byte dir) {
  return dir == S_WAIT ? DEFAULT_WAIT_S : dir == S_REPEAT ? DEFAULT_REPEAT_TIMES : dir == S_CIRCLE ? DEFAULT_CIRCLE_CM : isTurn(dir) ? DEFAULT_TURN_DEG : DEFAULT_STEP_CM;
}

void printStep(const Step &step) {
  printDirection(step.dir);
  out.print(' ');
  out.print(step.amount);
  out.print(step.dir == S_WAIT ? F(" s") : step.dir == S_REPEAT ? F(" times") : step.dir == S_CIRCLE ? F(" cm across") : isTurn(step.dir) ? F(" deg") : F(" cm"));
}

// Drive one step. False when stopped (a key, x, or the sonar).
bool runStep(const Step &step) {
  abortRequested = false;
  movingForward = step.dir == S_FORWARD || step.dir == S_DIAG_LEFT || step.dir == S_DIAG_RIGHT;
  float mm = step.amount * 10.0;
  switch (step.dir) {
    case S_FORWARD: driveStraightWhileSpinning(0, mm, DEMO_SPEED_MM_PER_SEC, 0); break;
    case S_BACK: driveStraightWhileSpinning(180, mm, DEMO_SPEED_MM_PER_SEC, 0); break;
    case S_LEFT: driveStraightWhileSpinning(270, mm, DEMO_SPEED_MM_PER_SEC, 0); break;
    case S_RIGHT: driveStraightWhileSpinning(90, mm, DEMO_SPEED_MM_PER_SEC, 0); break;
    case S_DIAG_LEFT: driveStraightWhileSpinning(315, mm, DEMO_SPEED_MM_PER_SEC, 0); break;
    case S_DIAG_RIGHT: driveStraightWhileSpinning(45, mm, DEMO_SPEED_MM_PER_SEC, 0); break;
    case S_TURN_LEFT: spinInPlace(-(float)step.amount, DEFAULT_SPIN_DEG_PER_SEC); break;
    case S_TURN_RIGHT: spinInPlace(step.amount, DEFAULT_SPIN_DEG_PER_SEC); break;
    case S_WAIT:
      for (unsigned long t = millis(); millis() - t < step.amount * 1000UL && !checkForAbort();) { delay(1); }
      break;
    case S_CIRCLE: {
      // One lap clockwise around a point that far ahead, nose locked on it (the
      // Workshop's pk demo): the centre slides left while the body turns with the orbit.
      float radius = step.amount * 5.0;   // across, cm -> radius, mm
      float omega = DEMO_SPEED_MM_PER_SEC / radius;
      executeTimedBodyMotion(0, -DEMO_SPEED_MM_PER_SEC, omega, omega, 2 * PI * radius / DEMO_SPEED_MM_PER_SEC);
      break;
    }
    default: break;   // S_REPEAT: runSteps handles it
  }
  stopAllWheels();
  movingForward = false;
  return !abortRequested;
}

// Run steps one after another; any key, x or the sonar stops the lot.
// An 8 (do it all again) sends the run back to the step after the previous 8
// (or the start) until it has gone round that many times in all.
void runSteps(const Step *steps, byte count) {
  int loopStart = 0;
  int roundsLeft = -1;   // -1: not inside a repeat yet
  for (int i = 0; i < count; i++) {
    if (steps[i].dir == S_REPEAT) {
      if (roundsLeft < 0) roundsLeft = (int)steps[i].amount - 1;
      if (roundsLeft > 0) {
        roundsLeft--;
        out.print(F("again: "));
        out.print(roundsLeft + 1);
        out.println(F(" more"));
        i = loopStart - 1;
      } else {
        roundsLeft = -1;
        loopStart = i + 1;
      }
      continue;
    }
    out.print(F("step "));
    out.print(i + 1);
    out.print(F(": "));
    printStep(steps[i]);
    out.println();
    if (!runStep(steps[i])) {
      out.println(F("run: stopped"));
      return;
    }
    // A breath between steps (a key during it stops too)
    for (unsigned long t = millis(); millis() - t < STEP_PAUSE_MS;) {
      if (checkForAbort()) {
        out.println(F("run: stopped"));
        return;
      }
    }
  }
  out.println(F("run: done"));
}

int slotAddress(byte slot) {   // slot 1..PROGRAM_SLOTS
  return PROGRAM_EEPROM_ADDRESS + (slot - 1) * PROGRAM_SLOT_BYTES;
}

// The steps saved in a slot (0 when the slot is empty or not a program).
byte loadProgram(byte slot, Step *steps) {
  if (slot < 1 || slot > PROGRAM_SLOTS) return 0;
  int address = slotAddress(slot);
  if (eeprom_read_byte((const uint8_t *)address) != PROGRAM_MAGIC) return 0;
  byte count = eeprom_read_byte((const uint8_t *)(address + 1));
  if (count > PROGRAM_MAX_STEPS) return 0;
  for (byte i = 0; i < count; i++) {
    uint16_t packed = eeprom_read_word((const uint16_t *)(address + 2 + 2 * i));
    steps[i].dir = packed >> 12;
    steps[i].amount = packed & 0x0FFF;
  }
  return count;
}

void saveProgram(byte slot, const Step *steps, byte count) {
  int address = slotAddress(slot);
  eeprom_update_byte((uint8_t *)address, count ? PROGRAM_MAGIC : 0);
  eeprom_update_byte((uint8_t *)(address + 1), count);
  for (byte i = 0; i < count; i++) {
    eeprom_update_word((uint16_t *)(address + 2 + 2 * i), ((uint16_t)steps[i].dir << 12) | (steps[i].amount & 0x0FFF));
  }
}

// "f50 t90 n4" -> steps (letters f b l r, q e diagonals, t degrees with t-90 =
// left, p pause seconds, n repeat times; no number = the step's default).
byte parseStepText(const char *text, Step *steps) {
  byte count = 0;
  for (const char *c = text; *c && count < PROGRAM_MAX_STEPS;) {
    char letter = *c++;
    int dir = letter == 'f' ? S_FORWARD : letter == 'b' ? S_BACK : letter == 'l' ? S_LEFT : letter == 'r' ? S_RIGHT : letter == 't' ? S_TURN_RIGHT
            : letter == 'q' ? S_DIAG_LEFT : letter == 'e' ? S_DIAG_RIGHT : letter == 'p' ? S_WAIT : letter == 'n' ? S_REPEAT : letter == 'c' ? S_CIRCLE : -1;
    if (dir < 0) continue;
    bool negative = *c == '-';
    if (negative) c++;
    int amount = 0;
    while (*c >= '0' && *c <= '9') {   // (not min(): it is a macro and would read *c++ twice)
      amount = amount * 10 + (*c++ - '0');
      if (amount > 4095) amount = 4095;
    }
    if (dir == S_TURN_RIGHT && negative) dir = S_TURN_LEFT;
    if (amount == 0) amount = defaultAmount(dir);
    steps[count].dir = dir;
    steps[count].amount = amount;
    count++;
  }
  return count;
}

// The nine demos of the Blocks page, built in: a slot nobody has saved to runs
// its demo, so * 1 is a square on a fresh robot. Saving to the slot replaces
// it; p<n>=; brings the demo back.
const char SLOT_DEMO_1[] PROGMEM = "f50 t90 n4";                                 // square
const char SLOT_DEMO_2[] PROGMEM = "r40 f40 l40 b40";                            // crab box: sliding, no turns
const char SLOT_DEMO_3[] PROGMEM = "f60 t180 n2";                                // out and back
const char SLOT_DEMO_4[] PROGMEM = "f50 t120 n3";                                // triangle
const char SLOT_DEMO_5[] PROGMEM = "f15 r15 n4";                                 // stairs
const char SLOT_DEMO_6[] PROGMEM = "f10 t90 f20 t90 f30 t90 f40 t90 f50 t90 f60 t90";   // spiral
const char SLOT_DEMO_7[] PROGMEM = "t-90 t180 t-90";                             // look around
const char SLOT_DEMO_8[] PROGMEM = "f40 t90 n4 f40 t-90 n4";                     // figure 8: a square each way
const char SLOT_DEMO_9[] PROGMEM = "t-30 t60 t-30 l15 r30 l15 f15 b15 t360";     // dance
const char *const SLOT_DEMOS[PROGRAM_SLOTS] PROGMEM = { SLOT_DEMO_1, SLOT_DEMO_2, SLOT_DEMO_3, SLOT_DEMO_4, SLOT_DEMO_5, SLOT_DEMO_6, SLOT_DEMO_7, SLOT_DEMO_8, SLOT_DEMO_9 };

// The slot's saved steps, or its built-in demo (builtIn says which)
byte loadSlotOrDemo(byte slot, Step *steps, bool &builtIn) {
  byte count = loadProgram(slot, steps);
  builtIn = count == 0;
  if (builtIn) {
    char text[64];
    strcpy_P(text, (PGM_P)pgm_read_ptr(&SLOT_DEMOS[slot - 1]));
    count = parseStepText(text, steps);
  }
  return count;
}

// * 0: program 0 is always a circle 1 m across, facing in
void runCircle() {
  Step circle = { S_CIRCLE, DEFAULT_CIRCLE_CM };
  out.println(F("run: program 0 (circle, 1 m across, facing in)"));
  runSteps(&circle, 1);
}

void runSlot(byte slot) {
  Step steps[PROGRAM_MAX_STEPS];
  bool builtIn;
  byte count = loadSlotOrDemo(slot, steps, builtIn);
  out.print(F("run: slot "));
  out.print(slot);
  if (!count) {
    out.println(F(" is empty"));
    gestureNo();
    return;
  }
  out.println(builtIn ? F(" (built-in demo)") : F(""));
  runSteps(steps, count);
}

// ===========================================================================
// THE REMOTE - what each key does
// ===========================================================================
enum RemoteMode { IDLE, PROGRAMMING, CALIBRATING };
// While programming: waiting for the * that starts a step; for the step's
// direction; for its digits; after * *, for the third; for the slot number;
// after the slot number, for * *.
enum ProgramState { P_STEP_START, P_DIRECTION, P_AMOUNT, P_END, P_SLOT, P_SAVE };

RemoteMode remoteMode = IDLE;
ProgramState programState = P_STEP_START;
Step draft[PROGRAM_MAX_STEPS];
byte draftCount = 0;
Step current;
bool currentHasDigits = false;
byte draftSlot = 0;
unsigned long programEnteredAt = 0;   // a 4th * and a slot digit right after entering = add to that slot
byte stars = 0;                   // * presses in a row (IDLE and P_SAVE)
unsigned long starAt = 0;
byte pounds = 0;                  // # presses in a row (IDLE)

// Hold-to-drive from the remote
bool irDriving = false;
byte driveDirection = S_FORWARD;
unsigned long irHeldAt = 0;

// Keep driving from the phone (v): a vector that must be repeated
bool vectorActive = false;
unsigned long vectorAt = 0;
bool vectorSaidBlocked = false;

// -1 when the key is not a direction (arrows, 1 and 3)
int directionOf(char key) {
  switch (key) {
    case 'U': return S_FORWARD;
    case 'D': return S_BACK;
    case 'L': return S_LEFT;
    case 'R': return S_RIGHT;
    case '1': return S_TURN_LEFT;
    case '3': return S_TURN_RIGHT;
    case '7': return S_DIAG_LEFT;
    case '9': return S_DIAG_RIGHT;
    case '5': return S_WAIT;
    case '8': return S_REPEAT;
    case '0': return S_CIRCLE;
    default: return -1;
  }
}

void stopDriving(bool say) {
  if (irDriving && say) out.println(F("drive: stop"));
  irDriving = false;
  vectorActive = false;
  stopAllWheels();
}

// v<forward>,<right>,<spin>; at -100..100 % of full speed, like mecanum_holonomic's
// vector drive: full speed is the top wheel speed, spin % is that speed at the lever.
void handleVector(const char *text) {
  int value[3];
  parseThreeInts(text, value);
  irDriving = false;
  float speed = motion[M_MAX];
  float forward = constrain(value[0], -100, 100) * 0.01;
  float right = constrain(value[1], -100, 100) * 0.01;
  float spin = constrain(value[2], -100, 100) * 0.01;
  movingForward = forward > 0;
  if (movingForward && wallAhead()) {
    forward = 0;   // the phone may still strafe and turn
    if (!vectorSaidBlocked) out.println(F("stop: something ahead"));
    vectorSaidBlocked = true;
  } else {
    vectorSaidBlocked = false;
  }
  vectorActive = true;
  vectorAt = millis();
  driveBody(forward * speed, right * speed, spin * speed / rotationLeverMm());
}

void startDriving(byte direction) {
  vectorActive = false;
  irDriving = true;
  driveDirection = direction;
  irHeldAt = millis();
  movingForward = direction == S_FORWARD || direction == S_DIAG_LEFT || direction == S_DIAG_RIGHT;
  if (movingForward && wallAhead()) {
    out.println(F("stop: something ahead"));
    stopDriving(false);
    return;
  }
  float v = DEMO_SPEED_MM_PER_SEC;
  float spin = degreesToRadians(DEFAULT_SPIN_DEG_PER_SEC);
  out.print(F("drive: "));
  printDirection(direction);
  out.println();
  switch (direction) {
    case S_FORWARD: driveBody(v, 0, 0); break;
    case S_BACK: driveBody(-v, 0, 0); break;
    case S_LEFT: driveBody(0, -v, 0); break;
    case S_RIGHT: driveBody(0, v, 0); break;
    case S_DIAG_LEFT: driveBody(v * 0.7071, -v * 0.7071, 0); break;
    case S_DIAG_RIGHT: driveBody(v * 0.7071, v * 0.7071, 0); break;
    case S_TURN_LEFT: driveBody(0, 0, -spin); break;
    default: driveBody(0, 0, spin); break;
  }
}

void beginStep(byte direction) {
  current.dir = direction;
  current.amount = 0;
  currentHasDigits = false;
  programState = P_AMOUNT;
}

void commitStep() {
  if (!currentHasDigits || current.amount == 0) {
    current.amount = defaultAmount(current.dir);
  }
  if (draftCount >= PROGRAM_MAX_STEPS) {
    out.println(F("program: full"));
    gestureNo();
  } else {
    draft[draftCount++] = current;
    out.print(F("step "));
    out.print(draftCount);
    out.print(F(": "));
    printStep(current);
    out.println();
    gestureNod();
  }
  programState = P_DIRECTION;   // the * that ended this step may start the next
}

void enterProgramming() {
  stopDriving(false);
  remoteMode = PROGRAMMING;
  programState = P_STEP_START;
  draftCount = 0;
  stars = 0;
  programEnteredAt = millis();
  out.println(F("program: listening"));
  gestureWiggle();
}

// * * * * <slot>: the slot's steps become the draft, to add to
void addToSlot(byte slot) {
  draftCount = loadProgram(slot, draft);
  programState = P_STEP_START;
  out.print(F("program: adding to slot "));
  out.print(slot);
  out.print(F(" ("));
  out.print(draftCount);
  out.println(F(" steps)"));
  gestureWiggle();
  gestureNod();
}

void cancelProgramming() {
  remoteMode = IDLE;
  draftCount = 0;
  stars = 0;
  out.println(F("program: cancelled"));
  gestureNo();
}

void finishProgram() {
  if (draftCount == 0) {
    cancelProgramming();
    return;
  }
  programState = P_SLOT;
  stars = 0;
  out.print(F("program: "));
  out.print(draftCount);
  out.println(F(" steps - which slot? (1-9, or OK to try it)"));
  gestureWiggle();
}

void saveDraft() {
  saveProgram(draftSlot, draft, draftCount);
  out.print(F("program: saved in slot "));
  out.println(draftSlot);
  remoteMode = IDLE;
  stars = 0;
  gestureNod();
}

// ===========================================================================
// CALIBRATION FROM THE REMOTE - offsets to the code's numbers, saved on the robot
// ===========================================================================
// Three steps, the ballpark only (no strafe): the start power, the speeds and
// the left/right balance from one metre, the turn from four right 90s. The
// numbers go in motion[] as the Calibrate page would set them, and are saved
// at the end (the same place: mecanum_holonomic and calibration.ino read them).
enum CalibrationStep { C_START_POWER, C_DISTANCE, C_DRIFT, C_TURN };
CalibrationStep calStep = C_START_POWER;
int calPower = 0;              // the start power being tried
unsigned int calValue = 0;     // the number being typed
bool calHasDigits = false;
int calSign = 0;               // +1 RIGHT (drifted right / turned past), -1 LEFT (left / short)
bool calRunDone = false;       // the step's test finished (not stopped)
const int CAL_POWER_STEP = 3;
const int CAL_RUN_MM = 1000;

// A short forward pulse at raw power p (every wheel, no curve): does it move?
void calPulse(int p) {
  out.print(F("calibrate: pulse at "));
  out.println(p);
  abortRequested = false;
  movingForward = true;
  for (int wheel = 0; wheel < 4; wheel++) setWheelPower(wheel, p);
  for (unsigned long t = millis(); millis() - t < 400 && !checkForAbort();) {}
  stopAllWheels();
  movingForward = false;
}

void calPrompt() {
  switch (calStep) {
    case C_START_POWER: out.println(F("calibrate: 1 start power - DOWN less, UP more, OK keeps it")); break;
    case C_DISTANCE:
      if (calRunDone) out.println(F("calibrate: 2 how many cm did it go? digits, OK (* drives again)"));
      else out.println(F("calibrate: 2 put it on a mark - OK drives 1 m"));
      break;
    case C_DRIFT: out.println(F("calibrate: 2 drift? LEFT or RIGHT, cm, OK (OK = none, * drives again)")); break;
    default:
      if (calRunDone) out.println(F("calibrate: 3 off by? RIGHT = past, LEFT = short, degrees, OK (* turns again)"));
      else out.println(F("calibrate: 3 mark where it faces - OK turns right 90 four times"));
      break;
  }
  calValue = 0;
  calHasDigits = false;
  calSign = 0;
}

// The step's own test: 1 m forward, or four right 90s
void calRunTest() {
  waitMs(400);
  abortRequested = false;
  if (calStep == C_DRIFT) calStep = C_DISTANCE;   // measured again: the distance question comes back
  if (calStep == C_DISTANCE) {
    out.println(F("calibrate: driving 1 m"));
    Step metre = { S_FORWARD, CAL_RUN_MM / 10 };
    calRunDone = runStep(metre);
  } else {
    out.println(F("calibrate: turning right 90, four times"));
    Step quarter = { S_TURN_RIGHT, 90 };
    calRunDone = true;
    for (byte i = 0; i < 4 && calRunDone; i++) {
      calRunDone = runStep(quarter);
      for (unsigned long t = millis(); millis() - t < 400 && calRunDone;) {
        if (checkForAbort()) calRunDone = false;
      }
    }
  }
  if (!calRunDone) out.println(F("calibrate: stopped"));
  calPrompt();
}

void enterCalibration() {
  stopDriving(false);
  remoteMode = CALIBRATING;
  calStep = C_START_POWER;
  calPower = (int)motion[M_LOW_PWM];
  pounds = 0;
  out.println(F("calibrate: start"));
  gestureWiggle();
  calPrompt();
  calPulse(calPower);
}

void leaveCalibration() {
  remoteMode = IDLE;
  pounds = 0;
}

void calCancel() {
  // The saved numbers come back (or the code's, if none are saved)
  memcpy_P(motion, MOTION_DEFAULT_VALUES, sizeof(motion));
  loadMotion();
  out.println(F("calibrate: cancelled"));
  gestureNo();
  leaveCalibration();
}

void calPrintNumber(const __FlashStringHelper *what, float value, byte decimals) {
  out.print(F("calibrate: "));
  out.print(what);
  out.print(' ');
  out.println(value, decimals);
}

// OK: take the typed number for the current step
void calAccept() {
  switch (calStep) {
    case C_START_POWER:
      motion[M_LOW_PWM] = calPower;
      motion[M_DEADBAND] = calPower - 1;
      calPrintNumber(F("start power"), calPower, 0);
      gestureNod();
      calStep = C_DISTANCE;
      calRunDone = false;
      calPrompt();
      return;
    case C_DISTANCE:
      if (!calRunDone) { calRunTest(); return; }   // OK: drive the metre
      if (calHasDigits && calValue > 0) {
        // It went calValue cm when asked for 1 m: it is that much faster or slower than it believed
        float factor = constrain(calValue * 10.0 / CAL_RUN_MM, 0.5, 2.0);
        byte speeds[] = { M_LOW_SPEED, M_HALF, M_192, M_MAX, M_STRAFE_START };
        for (byte i = 0; i < sizeof speeds; i++) motion[speeds[i]] *= factor;
        calPrintNumber(F("speeds x"), factor, 3);
      }
      gestureNod();
      calStep = C_DRIFT;
      calPrompt();
      return;
    case C_DRIFT:
      if (calHasDigits && calValue > 0 && calSign) {
        // Drifted d mm over L: the side it drifted towards is the weaker one. A gain
        // share g on the wheel speeds (one side up, the other down) bends the path
        // back: d = g spinEff L^2 / (2 lever). Gains act on the PWM above the dead
        // band, so the speed share becomes a PWM share through the curve's slope at
        // the driving speed.
        float d = calValue * 10.0;
        float g = 2 * d * rotationLeverMm() / (motion[M_SPIN_EFF] * (float)CAL_RUN_MM * CAL_RUN_MM);
        float slope = (motion[M_HALF] - motion[M_LOW_SPEED]) / (128 - motion[M_LOW_PWM]);   // mm/s per PWM
        if (slope < 0.5) slope = 3;
        float share = constrain(g * DEMO_SPEED_MM_PER_SEC / slope / (128 - motion[M_DEADBAND]), -0.3, 0.3);
        // drifted right: the left wheels are the strong ones: left down, right up
        float left = 1 - calSign * share, right = 1 + calSign * share;
        motion[M_GAIN0 + 0] = constrain(motion[M_GAIN0 + 0] * left, 0.5, 1.5);    // FL
        motion[M_GAIN0 + 2] = constrain(motion[M_GAIN0 + 2] * left, 0.5, 1.5);    // RL
        motion[M_GAIN0 + 1] = constrain(motion[M_GAIN0 + 1] * right, 0.5, 1.5);   // FR
        motion[M_GAIN0 + 3] = constrain(motion[M_GAIN0 + 3] * right, 0.5, 1.5);   // RR
        calPrintNumber(F("left gain x"), left, 3);
      }
      gestureNod();
      calStep = C_TURN;
      calRunDone = false;
      calPrompt();
      return;
    default:
      if (!calRunDone) { calRunTest(); return; }   // OK: the four turns
      if (calHasDigits && calValue > 0 && calSign) {
        // Four 90s went calSign * calValue degrees past 360: it turns that much more per
        // degree asked than it believed
        motion[M_SPIN_EFF] = constrain(motion[M_SPIN_EFF] * (360.0 + calSign * (float)calValue) / 360.0, 0.2, 2.0);
        calPrintNumber(F("spin efficiency"), motion[M_SPIN_EFF], 3);
      }
      saveMotion();
      out.println(F("calibrate: saved on the robot"));
      printMotion();
      gestureNod();
      gestureWiggle();
      leaveCalibration();
      return;
  }
}

void handleKeyCalibrating(char key) {
  if (key == '#') { calCancel(); return; }
  if (key == '*') {   // the step's test once more (digits typed so far are dropped)
    if (calStep == C_START_POWER) calPulse(calPower);
    else calRunTest();
    return;
  }
  if (calStep == C_START_POWER) {
    if (key == 'U') { calPower = min(calPower + CAL_POWER_STEP, 200); calPulse(calPower); }
    else if (key == 'D') { calPower = max(calPower - CAL_POWER_STEP, 20); calPulse(calPower); }
    else if (key == 'O') calAccept();
    return;
  }
  if (key >= '0' && key <= '9') {
    calValue = min(calValue * 10 + (key - '0'), 9999);
    calHasDigits = true;
  } else if (key == 'L' || key == 'R') {
    calSign = key == 'R' ? 1 : -1;
  } else if (key == 'O') {
    calAccept();
  }
}

void handleKeyIdle(char key) {
  if (key == '*') {
    stars++;
    starAt = millis();
    if (stars >= 3) enterProgramming();
    return;
  }
  if (stars == 1 && key >= '0' && key <= '9') {
    stars = 0;
    stopDriving(false);
    if (key == '0') runCircle(); else runSlot(key - '0');
    return;
  }
  stars = 0;
  if (key == '#') {
    stopDriving(true);
    pounds++;
    starAt = millis();
    if (pounds >= 3) enterCalibration();
    return;
  }
  pounds = 0;
  int direction = directionOf(key);
  if (direction >= 0 && isDriveStep(direction)) {
    startDriving(direction);
  } else if (key == '0' || key == 'O') {
    stopDriving(true);
  }
}

void handleKeyProgramming(char key) {
  if (key == '#') {
    cancelProgramming();
    return;
  }
  int direction = directionOf(key);
  switch (programState) {
    case P_STEP_START:
      if (key == '*') programState = P_DIRECTION;
      else if (direction >= 0) beginStep(direction);   // forgot the *: fine
      break;
    case P_DIRECTION:
      if (key == '*') programState = P_END;             // * * so far
      else if (key >= '1' && key <= '9' && draftCount == 0 && millis() - programEnteredAt < 2000) addToSlot(key - '0');   // * * * * <slot>
      else if (direction >= 0) beginStep(direction);
      break;
    case P_END:
      if (key == '*') finishProgram();                  // * * *
      else if (direction >= 0) beginStep(direction);    // the extra * was a slip
      break;
    case P_AMOUNT:
      if (key >= '0' && key <= '9') {
        current.amount = min(current.amount * 10 + (key - '0'), 4095);
        currentHasDigits = true;
      } else if (key == '*') {
        commitStep();
      } else if (direction >= 0) {                      // forgot the *: fine
        commitStep();
        beginStep(direction);
      }
      break;
    case P_SLOT:
    case P_SAVE:
      if (key >= '1' && key <= '9') {
        draftSlot = key - '0';
        stars = 0;
        programState = P_SAVE;
        out.print(F("program: slot "));
        out.print(draftSlot);
        out.println(F(" - * * saves"));
      } else if (key == 'O') {
        out.println(F("run: the new program"));
        runSteps(draft, draftCount);
      } else if (key == '*' && programState == P_SAVE) {
        if (++stars >= 2) saveDraft();
      }
      break;
  }
}

void handleKey(char key) {
  if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
  out.print(F("ir: "));
  switch (key) {
    case 'U': out.println(F("up")); break;
    case 'D': out.println(F("down")); break;
    case 'L': out.println(F("left")); break;
    case 'R': out.println(F("right")); break;
    case 'O': out.println(F("ok")); break;
    default: out.println(key); break;
  }
  if (remoteMode == IDLE) handleKeyIdle(key);
  else if (remoteMode == CALIBRATING) handleKeyCalibrating(key);
  else handleKeyProgramming(key);
}

void irPoll() {
  bool repeated;
  noInterrupts();
  repeated = irRepeatSeen;
  irRepeatSeen = false;
  interrupts();
  if (repeated && irDriving) irHeldAt = millis();
  int code = irTakeKey();
  if (code >= 0) {
    char key = irKeyLetter(code);
    if (key) {
      handleKey(key);
    } else {
      out.print(F("ir: key 0x"));
      out.print(code, HEX);
      out.println(F(" not mapped"));
    }
  }
  if (irDriving) {
    if (millis() - irHeldAt > IR_RELEASE_MS) {
      stopDriving(true);
    } else if (movingForward && wallAhead()) {
      out.println(F("stop: something ahead"));
      stopDriving(false);
    }
  }
  if (vectorActive && millis() - vectorAt > VECTOR_TIMEOUT_MS) {
    vectorActive = false;
    stopAllWheels();
  }
  if (remoteMode == IDLE && (stars || pounds) && millis() - starAt > IR_STAR_TIMEOUT_MS) stars = pounds = 0;
}

// ===========================================================================
// COMMANDS (USB / Bluetooth)
// ===========================================================================
// m<bearing>,<mm>,<turn>;: mm on a bearing at DEMO_SPEED_MM_PER_SEC while
// turning turn degrees; mm 0 = turn in place. Blocks; x aborts.
void handleMove(const char *text) {
  int value[3];
  parseThreeInts(text, value);
  abortRequested = false;
  int bearing = ((value[0] % 360) + 360) % 360;
  movingForward = value[1] > 0 && (bearing < 45 || bearing > 315);
  out.println(F("move: start"));
  if (value[1] > 0) driveStraightWhileSpinning(value[0], value[1], DEMO_SPEED_MM_PER_SEC, value[2]);
  else spinInPlace(value[2], DEFAULT_SPIN_DEG_PER_SEC);
  stopAllWheels();
  movingForward = false;
  out.println(abortRequested ? F("move: ABORTED") : F("move: done"));
}

// p?; p<n>; p<n>=<steps>;
void handleProgramText(char *text) {
  Step steps[PROGRAM_MAX_STEPS];
  if (text[0] == '?') {
    for (byte slot = 1; slot <= PROGRAM_SLOTS; slot++) {
      bool builtIn;
      byte count = loadSlotOrDemo(slot, steps, builtIn);
      out.print(F("P,"));
      out.print(slot);
      for (byte i = 0; i < count; i++) {
        out.print(',');
        out.print((char)pgm_read_byte(&STEP_LETTERS[steps[i].dir]));
        out.print(steps[i].dir == S_TURN_LEFT ? -(int)steps[i].amount : (int)steps[i].amount);
      }
      out.println(builtIn ? F(",builtin") : F(""));
    }
    out.println(F("P,end"));
    return;
  }
  byte slot = text[0] - '0';
  if (slot == 0 && text[1] == 0) { runCircle(); return; }   // p0; the circle
  if (slot < 1 || slot > PROGRAM_SLOTS) {
    out.print(F("program: bad "));
    out.println(text);
    return;
  }
  char *equals = strchr(text, '=');
  if (!equals) {
    runSlot(slot);
    return;
  }
  byte count = parseStepText(equals + 1, steps);
  saveProgram(slot, steps, count);
  out.print(F("program: slot "));
  out.print(slot);
  out.print(F(" = "));
  out.print(count);
  out.println(F(" steps"));
}

// k?; (the numbers, ending K,end), km, kw, kd and kN<name>; (* and # stand
// for x and X: those stop a command).
void handleConfigText(char *text) {
  if (strcmp(text, "?") == 0) {
    out.println(F("K,fw," FIRMWARE_VERSION));
    out.println(F("K,role," SKETCH_NAME));
    printRobotName(F("K,name,"));
    printMotion();
    out.println(F("K,end"));
    return;
  }
  if (text[0] == 'm') {   // km<i>=<value>; one number, live (kw keeps it)
    char *equals = strchr(text, '=');
    if (equals) {
      *equals = 0;
      byte index = atoi(text + 1);
      if (index < M_COUNT) motion[index] = atof(equals + 1);
    }
    printMotion();
    return;
  }
  if (strcmp(text, "w") == 0) {   // kw; save them on the robot (kept through power-off and uploads)
    saveMotion();
    out.println(F("config: saved"));
    return;
  }
  if (strcmp(text, "d") == 0) {   // kd; back to the code's numbers, and forget the saved ones
    memcpy_P(motion, MOTION_DEFAULT_VALUES, sizeof(motion));
    eeprom_update_word((uint16_t *)MOTION_EEPROM_ADDRESS, 0);
    motionFromEeprom = false;
    printMotion();
    return;
  }
  if (text[0] == 'N') {
    for (byte i = 0; i < 17; i++) {
      char c = i < 16 ? text[1 + i] : 0;
      if (c == '*') c = 'x';
      if (c == '#') c = 'X';
      eeprom_update_byte((uint8_t *)(BT_NAME_EEPROM_ADDRESS + 1 + i), c);
      if (!c) break;
    }
    eeprom_update_byte((uint8_t *)BT_NAME_EEPROM_ADDRESS, BT_NAME_PENDING);
    out.println(F("bt name: saved - switch the robot off and on"));
    return;
  }
  out.print(F("config: bad "));
  out.println(text);
}

void printHelp() {
  out.println(F("i<keys>; p?; p<n>; p<n>=<steps>; u v<f>,<r>,<s>; m<bearing>,<mm>,<turn>; k?; km<i>=<v>; kw; kd; kN<name>; x ?"));
  out.println(F("ready"));
}

// An i, p, m, v or k command collects text up to ';' (or a newline) before it runs.
// (a full slot from the Blocks page, 16 x "t-180 ", is 99 characters)
char textCommand = 'k';
bool awaitingConfigText = false;
char configText[120];
byte configTextLength = 0;

void handleCommand(char command) {
  if (awaitingConfigText) {
    if (command == 'x') {
      awaitingConfigText = false;   // no command text contains x: it is a stop, handle it below
    } else if (command == ';') {
      awaitingConfigText = false;
      configText[configTextLength] = 0;
      if (textCommand == 'm') handleMove(configText);
      else if (textCommand == 'p') handleProgramText(configText);
      else if (textCommand == 'v') handleVector(configText);
      else if (textCommand == 'i') for (char *c = configText; *c; c++) handleKey(*c);
      else handleConfigText(configText);
      return;
    } else if (configTextLength < sizeof(configText) - 1) {
      configText[configTextLength++] = command;
      return;
    } else {
      awaitingConfigText = false;
      out.println(F("config: too long"));
      return;
    }
  }
  switch (command) {
    case 'x': stopDriving(false); out.println(F("stop")); break;
    case 'u': out.print(F("U,")); out.println(sonarMm(SONAR_TIMEOUT_US)); break;
    case 'k':
    case 'm':
    case 'p':
    case 'v':
    case 'i': textCommand = command; awaitingConfigText = true; configTextLength = 0; break;
    case '?': printHelp(); break;
    default: break;
  }
}

void setup() {
  Serial.begin(9600);
#if USE_BLUETOOTH_SOFTSERIAL
  bluetoothSerial.begin(9600);
  bluetoothStartUp();
#endif
  initMotorPins();
  stopAllWheels();
  irBegin();
  out.println(F(SKETCH_NAME " v" FIRMWARE_VERSION));
  out.print(F("ir: receiver on pin "));
  out.print(IR_RECEIVER_PIN);
  out.print(F(", sonar stop at "));
  out.print(SONAR_STOP_MM);
  out.println(F(" mm"));
  printRobotName(F("ir: name "));
  loadMotion();
  printHelp();
}

void loop() {
  int incomingByte;
#if USE_BLUETOOTH_SOFTSERIAL
  // SoftwareSerial cannot receive while it transmits, and every reply goes
  // out on it. Let a burst from the module finish arriving (3 ms of quiet)
  // before handling any of it, or the reply to its first command cuts off
  // the rest.
  if (bluetoothSerial.available()) {
    int waiting = bluetoothSerial.available();
    unsigned long quietSince = micros();
    while (micros() - quietSince < 3000) {
      if (bluetoothSerial.available() != waiting) {
        waiting = bluetoothSerial.available();
        quietSince = micros();
      }
    }
  }
#endif
  while ((incomingByte = readCommandByte()) >= 0) {
    char incoming = (char)incomingByte;
    // Upper case is read as lower case, except right after k (kN, the name) or i (the keys)
    if (incoming >= 'A' && incoming <= 'Z' && !(awaitingConfigText && (textCommand == 'k' || textCommand == 'i'))) incoming = incoming - 'A' + 'a';
    if (incoming == '\r' || incoming == '\n') {
      handleCommand(';');
      continue;
    }
    if (incoming == ' ' && !(awaitingConfigText && textCommand == 'p')) continue;
    handleCommand(incoming);
  }
  irPoll();
  btFlush();
}
