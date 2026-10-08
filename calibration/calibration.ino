// calibration.ino   (v0.10.76 - calibration for the mecanum robot, by hand: L293D shield,
//                    Bluetooth on A0/A1 as shipped)
//
// One number at a time: run a test, measure, and the number goes into the
// CONFIG constants of mecanum_holonomic.ino AND this file (they must match:
// node scripts/check-mecanum-shared.js). Flash, run the test again: it should
// now come out right. Then the next number. No EEPROM numbers, no offsets:
// both sketches drive on the constants in the code.
//
// COMMANDS (USB or Bluetooth, 9600 baud, from the Workshop's command box)
//   o<mode>,<power>,<ms>;   a measuring run: every wheel at that power (0-255,
//                  straight to the shield: no curve, no trims, no ramp) for
//                  that long, then stop. mode 0 forward, 1 spin clockwise,
//                  2 strafe right. run: start ... run: done
//   m<bearing>,<mm>,<turn>;  a move exactly as mecanum_holonomic drives it
//                  (the same speed curve, ramps and timing; no gyro)
//   k?;            the numbers this build drives on (K,cal,...)
//   km<i>=<v>;     set motion number i (K,cal order) to v, live
//   kw;            save the numbers on the robot (kept through power-off and uploads)
//   kd;            back to the code's numbers, forgetting the saved ones
//   kN<name>;      Bluetooth name, given to the module at the next start-up
//   x              stop        ?  this list
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <avr/eeprom.h>

#define FIRMWARE_VERSION "0.10.76"   // same line as mecanum_holonomic (bump both together)
#define SKETCH_NAME "calibration"

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
// Bluetooth module too, so the app hears replies however it connected.
class DualPrint : public Print {
 public:
  size_t write(uint8_t c) override {
    Serial.write(c);
#if USE_BLUETOOTH_SOFTSERIAL
    bluetoothSerial.write(c);
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
// CONFIG - motion numbers: the numbers both sketches drive on. Calibrating
// changes them HERE and in mecanum_holonomic.ino, one at a time.
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

const unsigned long CALIBRATION_RUN_MS = 2000;
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

// Raw access by shield channel, bypassing the wiring table. Used by the
// twitch command so the table can be worked out in the first place.
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
  int incoming;
  while ((incoming = readCommandByte()) >= 0) {
    if (incoming == 'x' || incoming == 'X') {
      abortRequested = true;
    }
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
// COMMANDS
// ===========================================================================
// o<mode>,<power>,<ms>;: every wheel at that power for that long, then stop.
void handleRun(const char *text) {
  int value[3];
  parseThreeInts(text, value);
  int p = constrain(value[1], 0, 255);
  abortRequested = false;
  out.println(F("run: start"));
  setWheelPower(0, p);
  setWheelPower(1, value[0] ? -p : p);
  setWheelPower(2, value[0] == 2 ? -p : p);
  setWheelPower(3, value[0] == 1 ? -p : p);
  for (unsigned long t = millis(); millis() - t < (unsigned long)value[2] && !checkForAbort();) {}
  stopAllWheels();
  out.println(abortRequested ? F("run: ABORTED") : F("run: done"));
}

// m<bearing>,<mm>,<turn>;: mm on a bearing at DEMO_SPEED_MM_PER_SEC while
// turning turn degrees; mm 0 = turn in place. Blocks; x aborts.
void handleMove(const char *text) {
  int value[3];
  parseThreeInts(text, value);
  abortRequested = false;
  out.println(F("move: start"));
  if (value[1] > 0) driveStraightWhileSpinning(value[0], value[1], DEMO_SPEED_MM_PER_SEC, value[2]);
  else spinInPlace(value[2], DEFAULT_SPIN_DEG_PER_SEC);
  stopAllWheels();
  out.println(abortRequested ? F("move: ABORTED") : F("move: done"));
}

// k?; (the numbers, ending K,end) and kN<name>; (* and # stand for x and X:
// those stop a command).
void handleConfigText(char *text) {
  if (strcmp(text, "?") == 0) {
    out.println(F("K,fw," FIRMWARE_VERSION));
    out.println(F("K,role," SKETCH_NAME));
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
  out.println(F("o<mode>,<power>,<ms>; m<bearing>,<mm>,<turn>; k?; km<i>=<v>; kw; kd; kN<name>; x ?"));
  out.println(F("ready"));
}

// An o, m or k command collects text up to ';' (or a newline) before it runs.
char textCommand = 'k';
bool awaitingConfigText = false;
char configText[20];
byte configTextLength = 0;

void handleCommand(char command) {
  if (awaitingConfigText) {
    if (command == 'x') {
      awaitingConfigText = false;   // no command text contains x: it is a stop, handle it below
    } else if (command == ';') {
      awaitingConfigText = false;
      configText[configTextLength] = 0;
      if (textCommand == 'o') handleRun(configText);
      else if (textCommand == 'm') handleMove(configText);
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
    case 'x': stopAllWheels(); out.println(F("stop")); break;
    case 'k':
    case 'm':
    case 'o': textCommand = command; awaitingConfigText = true; configTextLength = 0; break;
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
  out.println(F(SKETCH_NAME " v" FIRMWARE_VERSION));
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
    // Upper case is read as lower case, except right after k (kN, the name)
    if (incoming >= 'A' && incoming <= 'Z' && !(awaitingConfigText && textCommand == 'k')) incoming = incoming - 'A' + 'a';
    if (incoming == '\r' || incoming == '\n') {
      handleCommand(';');
      continue;
    }
    if (incoming == ' ') continue;
    handleCommand(incoming);
  }
}
