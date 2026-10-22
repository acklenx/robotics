// robot_kernel.ino  —  version 0.1.13
// Trusted resident kernel for the LAFVIN 2WD Smart Robot Car V2.2
// Arduino Uno + TB6612FNG shield + JDY-16 BLE (moved to pins 8/11)
//
// Only built-in libraries: Servo, SoftwareSerial, EEPROM, Wire-ready.
//
// ---------------------------------------------------------------------------
// WIRING CHANGE FROM STOCK
//   The JDY-16 moves OFF the D0/D1 socket so USB uploads never conflict:
//     JDY-16 TXD  ->  D8   (Arduino listens here)
//     JDY-16 RXD  <-  D11  (Arduino transmits here; 1k series resistor is
//                           cheap insurance for the module's 3.3V input)
//   Easiest physical method: leave the module's VCC/GND pins in the shield's
//   BT socket for power, lift/bend the TXD/RXD pins clear of the socket, and
//   jumper those two pins to D8/D11.
//   USB (hardware serial) is now a permanent debug console that works at the
//   same time as Bluetooth. No more unplugging to upload.
//
// ---------------------------------------------------------------------------
// COMMAND PROTOCOL — CASE-INSENSITIVE — same over USB and Bluetooth (9600)
//
//   f b l r s      drive forward / back / spin left / spin right / stop
//   0-9            speed level
//   t a w p        line-track / avoid / follow / light-seek modes
//   y              seek mode: servo salute, 360 chassis survey, lock nearest
//                  target, smooth pursuit on center pings (servo stays
//                  straight); if lost, widening search (25/100/200/360 deg)
//   d              diagnostics snapshot
//   n<seed>        acceleration seed, hundredths of PWM/ms (live, not saved)
//   o<growth>      acceleration growth, thousandths of PWM per ms (live)
//   j<percent>     straight-drive trim -15..15 (added to LEFT motor), saved
//   h<0|1>         telemetry stream off/on (newline-terminated, e.g. h1):
//                  5x/sec: T,mode,lineL,lineR,proxL,proxR,ldrL,ldrR,front,
//                  motorL,motorR,fullScalePwm,servoAngle,railMv,packMv
//   m<l>,<r>       direct motor values -255..255, newline-terminated
//   e<angle>       sonar servo 0..180 (90 = straight ahead), newline-terminated
//   *<n>           select program slot 1-3 (newline-terminated, e.g. *2)
//   [ ... ]        capture a program into the active slot, save to EEPROM
//   g              run the active slot's program
//   k              list the active slot's program
//
// PROGRAM LANGUAGE (one instruction per line, case-insensitive):
//   f <ms>   drive forward for ms        b <ms>   drive backward for ms
//   l <ms>   spin left for ms            r <ms>   spin right for ms
//   w <ms>   wait                        v <0-9>  set speed level
//   e <ang>  point sonar servo (0-180)   x        end program
//   p <n>    repeat n times ... q        (loops, nest up to 4 deep)
//   i <s>    if sensor s ... z           (runs the block only when true)
//            sensors: 1 line-left  2 line-right  3 prox-left
//                     4 prox-right 5 front-closer-than-25cm
//                     6 front-clear 7 right-clear 8 left-clear
//            (6/7/8 are the opposites of 5/4/3 — pairing "i 5 [...] i 6 [...]"
//             gives you if/else, which the language doesn't otherwise have)
//
// IR REMOTE LAYOUT (the whole robot from the kit remote alone):
//   arrows = drive while held    OK = stop
//   1/2/3 = run program slot 1/2/3
//   4 = line track   5 = avoid    6 = follow (simple)
//   7 = light seek   8 = seek (radar-lock follow)
//   9 = speed cycle (3..9, wraps)
//   0 = release control -- next source to act (IR or BLE) takes over
//
// CONTROL ARBITRATION
//   Sources: IR remote, Bluetooth, USB. First to act owns the robot; the
//   others are answered "BUSY". Ownership releases after 8s idle. STOP is
//   always honored from any source. IR works at power-on with no handshake.
// ---------------------------------------------------------------------------

#include <Servo.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <stdio.h>

// ---- Function prototypes --------------------------------------------------
void driveMotors(int leftValue, int rightValue);
void stopMotors();
void serviceMotorRamp();
int rampOneMotor(int currentValue, int targetValue, unsigned long elapsedMs, int *remainderHundredths);
void applyMotorOutputs();
void commandServoAngle(int logicalAngle);
void serviceServoRelax();
void loadServoZeroFromEeprom();
void handleZeroRequest();
int currentDrivePwm();
struct CommandParser;
void readParserSource(CommandParser &parser);
void handleParsedCharacter(CommandParser &parser, char incoming);
void executeSingleCharacterCommand(byte source, char command);
void executeNumericCommand(byte source, char *buffer);
void handleProgramLine(CommandParser &parser);
bool claimControl(byte source);
void releaseControl();
void serviceOwnershipTimeout();
void serviceTelemetry();
void serviceSerialDeadMan();
void markSerialDrive();
int readRailMillivolts();
void servicePackVoltage();
int measurePackMillivolts();
void replyText(const __FlashStringHelper *text);
void replyValue(const __FlashStringHelper *label, int value);
void queueBluetoothLine(const char *line);
void serviceBluetoothReplyFlush();
void copyFlashString(char *destination, const __FlashStringHelper *source, int capacity);
void enterManualStop();
void enterMode(byte newMode, const __FlashStringHelper *announcement);
void printDiagnostics();
int measureFrontDistance();
int probeSonarWithRolesSwapped();
void runLineTracking();
void runObstacleAvoidance();
void runFollow();
void runLightSeeking();
void handleInfraredEdge();
void serviceInfraredRemote();
void clearInfraredCommand();
bool readSensorCondition(int sensorId);
int slotBaseAddress(byte slot);
void saveProgramToEeprom();
bool loadProgramFromEeprom(byte slot);
void selectSlot(byte slot);
void listStoredProgram();
void startProgramSlot(byte source, byte slot);
void stopProgram();
void serviceProgramInterpreter();
void advanceProgramCounter();
int findMatchingEnd(int fromIndex, char openOp, char closeOp);
void initializeSeekMode();
void runSeekFollow();
void beginSurveySpin();
void beginSearchArc();

// ---- Pin map (V2.2, verified) ---------------------------------------------
const byte leftDirectionPin = 2;    // HIGH = left wheel forward
const byte leftSpeedPin = 5;        // PWM
const byte rightDirectionPin = 4;   // LOW = right wheel forward (mirrored)
const byte rightSpeedPin = 6;       // PWM
const byte infraredReceiverPin = 3; // NEC IR receiver (INT1)
const byte lineSensorLeftPin = 7;
const byte lineSensorRightPin = 9;
const byte servoPin = 10;
const byte bluetoothRxPin = 8;      // <- JDY-16 TXD
const byte bluetoothTxPin = 11;     // -> JDY-16 RXD
const byte sonarTriggerPin = 12;
const byte sonarEchoPin = 13;
const byte proximityLeftPin = A1;   // 0 = obstacle seen
const byte proximityRightPin = A2;
const byte lightSensorLeftPin = A0;
const byte lightSensorRightPin = A3;
// A4/A5 reserved: I2C bus for gyro/magnetometer and future sensors.

// ---- Servo orientation & zero ---------------------------------------------
// This chassis has the pan servo mounted mirror-image: commanding "left"
// physically looked right. All angle math in this file stays in LOGICAL
// degrees (90 = straight ahead, >90 = robot's left); the flip happens in
// exactly one place, commandServoAngle().
const bool servoMountedReversed = true;
// Persistent trim: "Z" twice within 6 s makes the current position the new
// logical center (saved to EEPROM, survives power cycles).
const int SERVO_ZERO_MAGIC_ADDRESS = 1021;
const int SERVO_ZERO_OFFSET_ADDRESS = 1022;
const byte SERVO_ZERO_MAGIC = 0xC5;
// Straight-drive trim: percentage added to the LEFT motor (negative weakens
// it). Set from the Workshop's curve calibration via j<percent>; persisted.
const int TRIM_MAGIC_ADDRESS = 1019;
const int TRIM_VALUE_ADDRESS = 1020;
const byte TRIM_MAGIC = 0xB7;

// ---- IR remote keymap (standard LAFVIN NEC remote) ------------------------
const byte IR_KEY_UP = 0x46;
const byte IR_KEY_DOWN = 0x15;
const byte IR_KEY_LEFT = 0x44;
const byte IR_KEY_RIGHT = 0x43;
const byte IR_KEY_OK = 0x40;
const byte IR_KEY_1 = 0x16;
const byte IR_KEY_2 = 0x19;
const byte IR_KEY_3 = 0x0D;
const byte IR_KEY_4 = 0x0C;
const byte IR_KEY_5 = 0x18;
const byte IR_KEY_6 = 0x5E;
const byte IR_KEY_7 = 0x08;
const byte IR_KEY_8 = 0x1C;
const byte IR_KEY_9 = 0x5A;
const byte IR_KEY_0 = 0x52;

// ---- Tuning ---------------------------------------------------------------
const int obstacleStopCentimeters = 20;
const int sideClearCentimeters = 15;
const unsigned long infraredHoldTimeoutMillis = 200;
const unsigned long ownershipIdleTimeoutMillis = 8000;
const int autoCruisePwm = 85;
const int autoTurnPwm = 110;
// ---- Acceleration curve --------------------------------------------------
// Commands set a TARGET; the output ramps toward it. The ramp is EXPONENTIAL,
// not linear: the rate of change starts at a tiny seed and grows in
// proportion to the speed already reached, so the wheels creep off the line
// (no breakaway slip, no launch yaw) and then build briskly.
//
//   step per ms (PWM) = seed/100 + |current| * growth/1000
//
// Defaults are deliberately SLOW so the acceleration is visible to the eye:
// roughly 1.2 s from a standstill to full speed. Tune live with the serial
// commands n<seed> and o<growth>, then bake the winners in here.
//   seed 3, growth 2  -> ~1.25 s to full  (very visible creep-off)
//   seed 8, growth 4  -> ~0.5 s           (brisk but still soft)
//   seed 20, growth 8 -> ~0.2 s           (near the old linear feel)
const int motorRampSeedDefault = 3;    // hundredths of a PWM unit per ms
const int motorRampGrowthDefault = 2;  // thousandths of current PWM per ms
// Slowing down stays LINEAR and quicker than accelerating: stopping promptly
// matters more than stopping smoothly, and braking doesn't break traction.
const int motorDecelPerMillisecond = 2;
const int lightStopThreshold = 55;

// ---- Modes ----------------------------------------------------------------
const byte MODE_MANUAL = 0;
const byte MODE_LINE_TRACK = 1;
const byte MODE_AVOID = 2;
const byte MODE_FOLLOW = 3;
const byte MODE_LIGHT_SEEK = 4;
const byte MODE_PROGRAM = 5;
const byte MODE_SEEK = 6;

// ---- Seek ("radar lock") mode ---------------------------------------------
// Chassis does the big rotations (the servo can't reach 360); the servo does
// the visible conical-scan wobble once locked. Search escalates 25/100/200/360
// degrees, alternating direction, until relock.
const int SEEK_MIN_TARGET_CM = 6;     // closer = probably the robot's own frame
const int SEEK_MAX_TARGET_CM = 150;   // farther = wall/ground clutter, not a person
const unsigned long SCAN_FULL_TURN_MS = 1400;  // 4 x 350 ms = one chassis 360
const unsigned long TURN_MS_PER_DEG_X100 = 389; // 350 ms / 90 deg * 100
const int SEEK_HOLD_DISTANCE_CM = 25;

const byte SEEK_SCAN = 0;
const byte SEEK_AIM = 1;
const byte SEEK_SEARCH = 4;
const byte SEEK_INTRO = 5;
const byte SEEK_TRACK = 6;

byte seekStage = SEEK_INTRO;
unsigned long seekStageStartMillis = 0;
unsigned long seekLastPingMillis = 0;
int seekBestDistanceCm = 9999;
unsigned long seekBestTimeMs = 0;
unsigned int searchArcCount = 0;
byte fruitlessScanCount = 0;
byte seekIntroPhase = 0;
byte trackLostPings = 0;
unsigned long lastSteerLookMillis = 0;
const byte FRUITLESS_SCAN_LIMIT = 3;
const unsigned int SEARCH_ARC_LIMIT = 8;
unsigned long lastTrackStatusMillis = 0;
const unsigned int SEARCH_ARC_DEGREES[4] = {25, 100, 200, 360};

// ---- Control sources ------------------------------------------------------
const byte SOURCE_NONE = 0;
const byte SOURCE_IR = 1;
const byte SOURCE_BLUETOOTH = 2;
const byte SOURCE_USB = 3;

#define KERNEL_VERSION "0.1.13"

// >>> YOUR ROBOT'S NAME — change this, reflash, power-cycle once. <<<
#define ROBOT_BLUETOOTH_NAME "Aqua Kitten"

// >>> BATTERY MONITORING (2S 18650) <<<
// Set BATTERY_SENSE_PIN to -1 until the divider is actually wired, otherwise
// a floating pin reads garbage and the cutoff fires on a phantom.
//   pack(+, after switch) --[R1 100k]--+--[R2 100k]-- GND
//                                      +--> BATTERY_SENSE_PIN  (+ 0.1uF to GND)
// UNPROTECTED cells have no rescue circuit of their own: this cutoff IS the
// protection. 6.6V pack = 3.30V per cell, chosen above the 3.0V damage line
// because the pack sags under motor load — a robot resting at 6.8V is dipping
// under 6.0V on every launch.
#define BATTERY_SENSE_PIN A5          // -1 disables monitoring entirely
#define BATTERY_DIVIDER_R1_KOHMS 100
#define BATTERY_DIVIDER_R2_KOHMS 100
#define BATTERY_CUTOFF_MILLIVOLTS 6600
#define BATTERY_WARN_MILLIVOLTS 7000
// VIN is DEAD when the Uno runs on USB alone (USB power does not back-feed
// it), so the divider reads near zero on the bench. Anything below this is
// read as "not running on battery" — monitored, reported, but never cause
// for a cutoff. Otherwise every USB bench session would brick the motors.
#define BATTERY_ABSENT_MILLIVOLTS 3000
// Per-robot trim, in parts per thousand. 1000 = no correction. The chip's
// internal 1.1V bandgap (which the rail measurement leans on) is only spec'd
// to +/-10%, so each board reads slightly differently. To calibrate: meter
// the VIN pin while the robot is idle on battery, then
//   PERMILLE = 1000 * (metered VIN volts) / (reported PACK volts)
// e.g. VIN 7.73 reported 7.62 -> 1000 * 7.73 / 7.62 = 1014.
// Only do this if the meter is on the VIN PIN — measuring at the pack
// terminals includes switch and wiring drop, which is real, not error.
#define BATTERY_CALIBRATION_PERMILLE 1000

byte currentMode = MODE_MANUAL;
byte controlOwner = SOURCE_NONE;
unsigned long lastOwnerActivityMillis = 0;
int speedLevel = 5;
int straightTrimPercent = 0; // -15..15, applied to the left motor
int packMillivolts = 0;              // 0 = no divider wired
int packSamples[9];
byte packSampleCount = 0;
bool batteryCutoffActive = false;
unsigned long lastPackSampleMillis = 0;
bool serialDriveActive = false;      // manual motion commanded over serial...
unsigned long lastSerialDriveMillis = 0; // ...auto-stops 400ms after the last command
const unsigned long serialDriveTimeoutMillis = 400;
bool telemetryEnabled = true; // boot in sensor mode: stream from power-on

// Bluetooth replies are QUEUED, never inline: transmitting on half-duplex
// SoftwareSerial while the phone is mid-burst blacks out reception and eats
// the very bytes being replied to (the CAPTURE reply was destroying program
// uploads). The queue flushes only after 60ms of receive silence — longer
// than the app's 30ms inter-chunk gap, so a multi-chunk upload finishes
// before the robot says a single word. USB replies stay immediate: hardware
// serial TX is buffered and cannot block reception.
char bluetoothReplyQueue[192];
int bluetoothReplyQueueLength = 0;
const unsigned long replyQuietWindowMillis = 60;
unsigned long lastTelemetryMillis = 0;
unsigned long lastByteReceivedMillis = 0;
const unsigned long receiveQuietWindowMillis = 250;
int lastLeftMotorValue = 0;   // ramped output actually on the pins (telemetry)
int lastRightMotorValue = 0;
int targetLeftMotorValue = 0; // where the ramp is headed
int targetRightMotorValue = 0;
unsigned long lastRampServiceMillis = 0;
int motorRampSeed = motorRampSeedDefault;
int motorRampGrowth = motorRampGrowthDefault;
int leftRampRemainder = 0;    // hundredths of a PWM unit not yet applied
int rightRampRemainder = 0;
int lastFrontDistanceCm = 400;
unsigned int sonarTimeoutStreak = 0; // consecutive pings with no echo at all

Servo panServo;
bool servoIsAttached = false;
int servoCenterOffset = 0;            // trim in physical degrees
int lastLogicalServoAngle = 90;
unsigned long lastServoCommandMillis = 0;
unsigned long pendingZeroRequestMillis = 0;
const unsigned long servoRelaxAfterMillis = 800;

SoftwareSerial bluetoothSerial(bluetoothRxPin, bluetoothTxPin);

// ---- Per-source command parser state --------------------------------------
struct CommandParser {
  byte source;
  Stream *port;
  char lineBuffer[24];
  byte lineLength;
  bool collectingNumericCommand;
  bool capturingProgram;
  unsigned long captureStartedMillis;
};

CommandParser usbParser;
CommandParser bluetoothParser;

// ---- IR decoder state -----------------------------------------------------
volatile unsigned long infraredLastFallMicros = 0;
volatile unsigned long infraredShiftRegister = 0;
volatile byte infraredBitCount = 0;
volatile bool infraredFrameInProgress = false;
volatile byte infraredLastCommand = 0;
volatile unsigned long infraredLastActivityMillis = 0;
bool infraredWasDriving = false;

// ---- Avoidance / follow sub-state -----------------------------------------
const byte STAGE_CRUISING = 0;
const byte STAGE_LOOK_LEFT = 1;
const byte STAGE_LOOK_RIGHT = 2;
const byte STAGE_TURNING = 3;
const byte STAGE_BACKING = 4;
const byte STAGE_FRONT_RECHECK = 5;
const byte STAGE_ROTATE = 6;

const byte AVOID_SCAN_ROUNDS_PER_POSITION = 2;
const byte AVOID_ROTATE_STEP_LIMIT = 8;      // 8 x 45 = full circle tried
const int AVOID_ROTATE_45_MS = 175;          // 45 deg at autoTurnPwm

byte avoidanceStage = STAGE_CRUISING;
byte avoidScanRound = 0;
byte avoidRotateSteps = 0;
unsigned long stageStartMillis = 0;
int scannedLeftDistance = 0;
int scannedRightDistance = 0;
int pendingTurnDirection = 1;
unsigned long lastPingMillis = 0;

// ---- Stored program -------------------------------------------------------
const byte MAX_PROGRAM_STEPS = 100;
const byte SLOT_COUNT = 3;
const int SLOT_STRIDE = 1 + (int)MAX_PROGRAM_STEPS * 3; // length byte + steps

byte activeSlot = 1;   // slot that [, g, k operate on
byte loadedSlot = 0;   // slot currently in RAM (0 = none)
const byte LOOP_STACK_DEPTH = 4;
const byte EEPROM_MAGIC = 0xA8;   // bumped: 3-slot layout

char programOps[MAX_PROGRAM_STEPS];
int programArgs[MAX_PROGRAM_STEPS];
byte programLength = 0;

bool programRunning = false;
int programCounter = 0;
unsigned long instructionEndMillis = 0;
int loopStackStart[LOOP_STACK_DEPTH];
int loopStackRemaining[LOOP_STACK_DEPTH];
byte loopStackDepth = 0;
byte captureCount = 0;

// ===========================================================================

void setup() {
  Serial.begin(9600);

  // BT link runs at 38400: each telemetry line's transmit blackout shrinks
  // 4x versus 9600, which cuts the byte-corruption window the same amount.
  // Self-healing bring-up: first talk at 9600 (the module's shipped rate)
  // and command 38400 (JDY-16: AT+BAUD6); a module already at 38400 sees
  // that as line noise and ignores it. Then reopen at 38400 and set the
  // name. NOTE: after the very first flash the module may need ONE
  // power-cycle to adopt the new rate — after that, every boot is clean.
  bluetoothSerial.begin(9600);
  delay(150);
  bluetoothSerial.print(F("AT+BAUD6\r\n"));
  delay(250);
  bluetoothSerial.end();
  bluetoothSerial.begin(38400);
  delay(150);
  bluetoothSerial.print(F("AT+NAME" ROBOT_BLUETOOTH_NAME "\r\n"));
  delay(200);
  while (bluetoothSerial.available() > 0) {
    bluetoothSerial.read(); // drain +OK so the parser never sees it
  }

  pinMode(leftDirectionPin, OUTPUT);
  pinMode(leftSpeedPin, OUTPUT);
  pinMode(rightDirectionPin, OUTPUT);
  pinMode(rightSpeedPin, OUTPUT);
  pinMode(sonarTriggerPin, OUTPUT);
  pinMode(sonarEchoPin, INPUT);
  pinMode(lineSensorLeftPin, INPUT);
  pinMode(lineSensorRightPin, INPUT);
  pinMode(proximityLeftPin, INPUT);
  pinMode(proximityRightPin, INPUT);
  pinMode(lightSensorLeftPin, INPUT);
  pinMode(lightSensorRightPin, INPUT);
  pinMode(infraredReceiverPin, INPUT);
  if (BATTERY_SENSE_PIN >= 0) {
    pinMode(BATTERY_SENSE_PIN, INPUT);
  }

  usbParser.source = SOURCE_USB;
  usbParser.port = &Serial;
  usbParser.lineLength = 0;
  usbParser.collectingNumericCommand = false;
  usbParser.capturingProgram = false;

  bluetoothParser.source = SOURCE_BLUETOOTH;
  bluetoothParser.port = &bluetoothSerial;
  bluetoothParser.lineLength = 0;
  bluetoothParser.collectingNumericCommand = false;
  bluetoothParser.capturingProgram = false;

  stopMotors();
  loadServoZeroFromEeprom();
  if (EEPROM.read(TRIM_MAGIC_ADDRESS) == TRIM_MAGIC) {
    straightTrimPercent = constrain((int)EEPROM.read(TRIM_VALUE_ADDRESS) - 50, -15, 15);
  }
  // Power-on servo check: 10 deg one way, 10 deg the other, back to center.
  // If you don't see this wiggle, the servo isn't connected.
  commandServoAngle(80);
  delay(250);
  commandServoAngle(100);
  delay(400);
  commandServoAngle(90);
  delay(250);
  panServo.detach(); // relax NOW: the first telemetry burst must find no servo to jitter
  servoIsAttached = false;

  attachInterrupt(digitalPinToInterrupt(infraredReceiverPin), handleInfraredEdge, FALLING);

  if (loadProgramFromEeprom(1)) {
    replyValue(F("SLOT 1 LOADED steps="), programLength);
  }
  replyText(F("READY robot_kernel " KERNEL_VERSION));
}

void loop() {
  readParserSource(usbParser);
  readParserSource(bluetoothParser);
  serviceInfraredRemote();
  serviceOwnershipTimeout();
  serviceTelemetry();
  serviceMotorRamp();
  serviceServoRelax();
  serviceSerialDeadMan();
  serviceBluetoothReplyFlush();
  servicePackVoltage();

  if (currentMode == MODE_LINE_TRACK) {
    runLineTracking();
  } else if (currentMode == MODE_AVOID) {
    runObstacleAvoidance();
  } else if (currentMode == MODE_FOLLOW) {
    runFollow();
  } else if (currentMode == MODE_LIGHT_SEEK) {
    runLightSeeking();
  } else if (currentMode == MODE_PROGRAM) {
    serviceProgramInterpreter();
  } else if (currentMode == MODE_SEEK) {
    runSeekFollow();
  }
}

// ===========================================================================
// Replies go to both consoles so USB always mirrors what Bluetooth sees.
// ===========================================================================

void queueBluetoothLine(const char *line) {
  int lineLength = strlen(line);
  if (bluetoothReplyQueueLength + lineLength + 1 >= (int)sizeof(bluetoothReplyQueue)) {
    bluetoothReplyQueueLength = 0; // overflow: drop backlog, keep the newest
  }
  memcpy(bluetoothReplyQueue + bluetoothReplyQueueLength, line, lineLength);
  bluetoothReplyQueueLength = bluetoothReplyQueueLength + lineLength;
  bluetoothReplyQueue[bluetoothReplyQueueLength] = '\n';
  bluetoothReplyQueueLength = bluetoothReplyQueueLength + 1;
}

void serviceBluetoothReplyFlush() {
  if (bluetoothReplyQueueLength == 0) {
    return;
  }
  if (millis() - lastByteReceivedMillis < replyQuietWindowMillis) {
    return; // the phone is still talking: stay silent, protect its bytes
  }
  if (usbParser.capturingProgram || bluetoothParser.capturingProgram) {
    return;
  }
  bluetoothSerial.write((const uint8_t *)bluetoothReplyQueue, bluetoothReplyQueueLength);
  bluetoothReplyQueueLength = 0;
}

void copyFlashString(char *destination, const __FlashStringHelper *source, int capacity) {
#if defined(__AVR__)
  strncpy_P(destination, (const char *)source, capacity - 1);
#else
  strncpy(destination, (const char *)source, capacity - 1);
#endif
  destination[capacity - 1] = '\0';
}

void replyText(const __FlashStringHelper *text) {
  Serial.println(text);
  char lineBuffer[64];
  copyFlashString(lineBuffer, text, sizeof(lineBuffer));
  queueBluetoothLine(lineBuffer);
}

void replyValue(const __FlashStringHelper *label, int value) {
  Serial.print(label);
  Serial.println(value);
  char lineBuffer[64];
  copyFlashString(lineBuffer, label, sizeof(lineBuffer) - 8);
  char *tail = lineBuffer + strlen(lineBuffer);
  snprintf(tail, 8, "%d", value);
  queueBluetoothLine(lineBuffer);
}

// ===========================================================================
// Ownership
// ===========================================================================

bool claimControl(byte source) {
  if (controlOwner == SOURCE_NONE || controlOwner == source) {
    controlOwner = source;
    lastOwnerActivityMillis = millis();
    return true;
  }
  return false;
}

void releaseControl() {
  controlOwner = SOURCE_NONE;
}

void serviceOwnershipTimeout() {
  if (controlOwner == SOURCE_NONE) {
    return;
  }
  if (currentMode != MODE_MANUAL) {
    lastOwnerActivityMillis = millis(); // autonomous activity keeps ownership alive
    return;
  }
  if (millis() - lastOwnerActivityMillis > ownershipIdleTimeoutMillis) {
    releaseControl();
    stopMotors();
    replyText(F("IDLE control released"));
  }
}

// ===========================================================================
// Motors
// ===========================================================================

void driveMotors(int leftValue, int rightValue) {
  if (batteryCutoffActive) {
    targetLeftMotorValue = 0; // dead pack: nothing moves until it's charged
    targetRightMotorValue = 0;
    serviceMotorRamp();
    return;
  }
  leftValue = leftValue + (leftValue * straightTrimPercent) / 100;
  targetLeftMotorValue = constrain(leftValue, -255, 255);
  targetRightMotorValue = constrain(rightValue, -255, 255);
  serviceMotorRamp(); // take the first step now, not next loop pass
}

void stopMotors() {
  driveMotors(0, 0); // stops ramp down too: same gentle rate, ~43 ms from cruise
}

// Returns the new output for one motor, and carries the sub-PWM remainder so
// the slow early part of the curve isn't lost to integer truncation.
int rampOneMotor(int currentValue, int targetValue, unsigned long elapsedMs,
                 int *remainderHundredths) {
  if (currentValue == targetValue) {
    *remainderHundredths = 0;
    return currentValue;
  }

  bool speedingUp = abs(targetValue) > abs(currentValue) &&
                    (currentValue == 0 || (currentValue > 0) == (targetValue > 0));

  long stepHundredths;
  if (speedingUp) {
    // Exponential: tiny seed, then growth proportional to speed so far.
    stepHundredths = (long)elapsedMs *
                     ((long)motorRampSeed + (long)abs(currentValue) * motorRampGrowth / 10L);
  } else {
    stepHundredths = (long)elapsedMs * motorDecelPerMillisecond * 100L;
  }

  stepHundredths = stepHundredths + *remainderHundredths;
  int wholeStep = (int)(stepHundredths / 100L);
  *remainderHundredths = (int)(stepHundredths % 100L);
  if (wholeStep <= 0) {
    return currentValue; // still creeping: remainder carries into next pass
  }

  if (currentValue < targetValue) {
    int stepped = currentValue + wholeStep;
    return stepped > targetValue ? targetValue : stepped;
  }
  int stepped = currentValue - wholeStep;
  return stepped < targetValue ? targetValue : stepped;
}

void serviceMotorRamp() {
  unsigned long now = millis();
  unsigned long elapsedMs = now - lastRampServiceMillis;
  if (elapsedMs == 0) {
    return;
  }
  lastRampServiceMillis = now;
  if (elapsedMs > 50) {
    elapsedMs = 50; // a blocking sonar ping shouldn't turn into a giant step
  }

  int newLeft = rampOneMotor(lastLeftMotorValue, targetLeftMotorValue, elapsedMs, &leftRampRemainder);
  int newRight = rampOneMotor(lastRightMotorValue, targetRightMotorValue, elapsedMs, &rightRampRemainder);

  if (newLeft != lastLeftMotorValue || newRight != lastRightMotorValue) {
    lastLeftMotorValue = newLeft;
    lastRightMotorValue = newRight;
    applyMotorOutputs();
  }
}

void applyMotorOutputs() {
  digitalWrite(leftDirectionPin, lastLeftMotorValue >= 0 ? HIGH : LOW);
  analogWrite(leftSpeedPin, abs(lastLeftMotorValue));

  digitalWrite(rightDirectionPin, lastRightMotorValue >= 0 ? LOW : HIGH);
  analogWrite(rightSpeedPin, abs(lastRightMotorValue));
}

// ===========================================================================
// Servo — one write path. Detached when idle: SoftwareSerial traffic blocks
// interrupts long enough to jitter servo pulses, so an idle servo gets NO
// pulses at all (it physically cannot twitch). It re-attaches on demand.
// ===========================================================================

void commandServoAngle(int logicalAngle) {
  logicalAngle = constrain(logicalAngle, 0, 180);
  int physicalAngle;
  if (servoMountedReversed) {
    physicalAngle = 90 - (logicalAngle - 90) + servoCenterOffset;
  } else {
    physicalAngle = 90 + (logicalAngle - 90) + servoCenterOffset;
  }
  physicalAngle = constrain(physicalAngle, 0, 180);
  // If the servo buzzes at the very ends of travel, it is binding: back the
  // avoid-scan angles off to 170/10.

  if (!servoIsAttached) {
    panServo.attach(servoPin);
    servoIsAttached = true;
  }
  panServo.write(physicalAngle);
  lastLogicalServoAngle = logicalAngle;
  lastServoCommandMillis = millis();
}

void serviceServoRelax() {
  if (servoIsAttached && millis() - lastServoCommandMillis > servoRelaxAfterMillis) {
    panServo.detach();
    servoIsAttached = false;
  }
}

void loadServoZeroFromEeprom() {
  if (EEPROM.read(SERVO_ZERO_MAGIC_ADDRESS) == SERVO_ZERO_MAGIC) {
    servoCenterOffset = (int)EEPROM.read(SERVO_ZERO_OFFSET_ADDRESS) - 90;
    servoCenterOffset = constrain(servoCenterOffset, -30, 30);
  }
}

void handleZeroRequest() {
  unsigned long now = millis();
  if (now - pendingZeroRequestMillis < 6000UL && pendingZeroRequestMillis != 0) {
    int direction = servoMountedReversed ? -1 : 1;
    servoCenterOffset = constrain(direction * (lastLogicalServoAngle - 90) + servoCenterOffset, -30, 30);
    EEPROM.update(SERVO_ZERO_MAGIC_ADDRESS, SERVO_ZERO_MAGIC);
    EEPROM.update(SERVO_ZERO_OFFSET_ADDRESS, (byte)(servoCenterOffset + 90));
    lastLogicalServoAngle = 90;
    pendingZeroRequestMillis = 0;
    replyValue(F("ZERO SET trim="), servoCenterOffset);
  } else {
    pendingZeroRequestMillis = now;
    replyText(F("ZERO? send Z again within 6s to make THIS position the new center"));
  }
}

int currentDrivePwm() {
  return map(speedLevel, 0, 9, 60, 255);
}

// ===========================================================================
// Command parsing — one independent parser per serial source.
// Case-insensitivity: every command character is normalized with toupper()
// at exactly one entry point per path (here, and in the program-line and
// IR handlers). Never compare raw lowercase anywhere else.
// ===========================================================================

void readParserSource(CommandParser &parser) {
  while (parser.port->available() > 0) {
    char incoming = (char)parser.port->read();
    lastByteReceivedMillis = millis();
    handleParsedCharacter(parser, incoming);
  }
  if (parser.capturingProgram && millis() - parser.captureStartedMillis > 10000UL) {
    parser.capturingProgram = false;
    parser.lineLength = 0;
    captureCount = 0;
    releaseControl();
    replyText(F("CAPTURE TIMEOUT discarded"));
  }
}

void handleParsedCharacter(CommandParser &parser, char incoming) {
  if (parser.capturingProgram) {
    if (incoming == ']') {
      parser.capturingProgram = false;
      programLength = captureCount;
      saveProgramToEeprom();
      replyValue(F("SLOT "), activeSlot);
      replyValue(F("SAVED steps="), programLength);
      releaseControl();
      return;
    }
    if (incoming == '\n' || incoming == '\r') {
      if (parser.lineLength > 0) {
        parser.lineBuffer[parser.lineLength] = '\0';
        handleProgramLine(parser);
        parser.lineLength = 0;
      }
      return;
    }
    if (parser.lineLength < sizeof(parser.lineBuffer) - 1) {
      parser.lineBuffer[parser.lineLength] = incoming;
      parser.lineLength = parser.lineLength + 1;
    }
    return;
  }

  if (parser.collectingNumericCommand) {
    if (incoming == '\n' || incoming == '\r') {
      parser.lineBuffer[parser.lineLength] = '\0';
      executeNumericCommand(parser.source, parser.lineBuffer);
      parser.collectingNumericCommand = false;
      parser.lineLength = 0;
    } else if (parser.lineLength < sizeof(parser.lineBuffer) - 1) {
      parser.lineBuffer[parser.lineLength] = incoming;
      parser.lineLength = parser.lineLength + 1;
    } else {
      parser.collectingNumericCommand = false;
      parser.lineLength = 0;
    }
    return;
  }

  char command = (char)toupper(incoming);

  if (command == 'M' || command == 'E' || command == '*' || command == 'H' ||
      command == 'J' || command == 'N' || command == 'O') {
    parser.collectingNumericCommand = true;
    parser.lineBuffer[0] = command;
    parser.lineLength = 1;
    return;
  }

  if (command == '[') {
    if (!claimControl(parser.source)) {
      replyText(F("BUSY"));
      return;
    }
    if (currentMode == MODE_PROGRAM) {
      stopProgram();
    }
    enterManualStop();
    parser.capturingProgram = true;
    parser.captureStartedMillis = millis();
    parser.lineLength = 0;
    captureCount = 0;
    replyText(F("CAPTURE"));
    return;
  }

  executeSingleCharacterCommand(parser.source, command);
}

void executeSingleCharacterCommand(byte source, char command) {
  if (command == 'S') { // STOP: always honored, from anyone
    stopProgram();
    enterManualStop();
    releaseControl();
    replyText(F("STOP"));
    return;
  }

  if (command == 'D') { // diagnostics: read-only, always allowed
    printDiagnostics();
    return;
  }

  if (command == 'K') { // list program: read-only
    listStoredProgram();
    return;
  }

  if (command == 'Z') { // re-zero servo center (asks for confirmation)
    handleZeroRequest();
    return;
  }

  bool isActionCommand =
    command == 'F' || command == 'B' || command == 'L' || command == 'R' ||
    command == 'T' || command == 'A' || command == 'W' || command == 'P' ||
    command == 'Y' || command == 'G' || (command >= '0' && command <= '9');

  if (!isActionCommand) {
    return;
  }

  if (!claimControl(source)) {
    replyText(F("BUSY"));
    return;
  }

  if (command >= '0' && command <= '9') {
    speedLevel = command - '0';
    replyValue(F("SPEED "), speedLevel);
    return;
  }

  switch (command) {
    case 'F':
      stopProgram();
      currentMode = MODE_MANUAL;
      markSerialDrive();
      driveMotors(currentDrivePwm(), currentDrivePwm());
      break;
    case 'B':
      stopProgram();
      currentMode = MODE_MANUAL;
      markSerialDrive();
      driveMotors(-currentDrivePwm(), -currentDrivePwm());
      break;
    case 'L':
      stopProgram();
      currentMode = MODE_MANUAL;
      markSerialDrive();
      driveMotors(-currentDrivePwm(), currentDrivePwm());
      break;
    case 'R':
      stopProgram();
      currentMode = MODE_MANUAL;
      markSerialDrive();
      driveMotors(currentDrivePwm(), -currentDrivePwm());
      break;
    case 'T':
      enterMode(MODE_LINE_TRACK, F("MODE LINE_TRACK"));
      break;
    case 'A':
      enterMode(MODE_AVOID, F("MODE AVOID"));
      break;
    case 'W':
      enterMode(MODE_FOLLOW, F("MODE FOLLOW"));
      break;
    case 'P':
      enterMode(MODE_LIGHT_SEEK, F("MODE LIGHT_SEEK"));
      break;
    case 'Y':
      enterMode(MODE_SEEK, F("MODE SEEK"));
      initializeSeekMode();
      break;
    case 'G':
      startProgramSlot(source, activeSlot);
      break;
  }
}

void executeNumericCommand(byte source, char *buffer) {
  if (buffer[0] == 'M') {
    char *separator = strchr(buffer, ',');
    if (separator == NULL) {
      return;
    }
    if (!claimControl(source)) {
      replyText(F("BUSY"));
      return;
    }
    *separator = '\0';
    stopProgram();
    currentMode = MODE_MANUAL;
    markSerialDrive();
    driveMotors(atoi(buffer + 1), atoi(separator + 1));
  } else if (buffer[0] == 'E') {
    if (!claimControl(source)) {
      replyText(F("BUSY"));
      return;
    }
    commandServoAngle(atoi(buffer + 1));
  } else if (buffer[0] == '*') {
    selectSlot((byte)constrain(atoi(buffer + 1), 1, (int)SLOT_COUNT));
  } else if (buffer[0] == 'H') {
    telemetryEnabled = atoi(buffer + 1) != 0;
    replyValue(F("TELEM "), telemetryEnabled ? 1 : 0);
  } else if (buffer[0] == 'N') {
    motorRampSeed = constrain(atoi(buffer + 1), 1, 200);
    replyValue(F("RAMPSEED "), motorRampSeed);
  } else if (buffer[0] == 'O') {
    motorRampGrowth = constrain(atoi(buffer + 1), 0, 100);
    replyValue(F("RAMPGROWTH "), motorRampGrowth);
  } else if (buffer[0] == 'J') {
    straightTrimPercent = constrain(atoi(buffer + 1), -15, 15);
    EEPROM.update(TRIM_MAGIC_ADDRESS, TRIM_MAGIC);
    EEPROM.update(TRIM_VALUE_ADDRESS, (byte)(straightTrimPercent + 50));
    replyValue(F("TRIM "), straightTrimPercent);
  }
}

void enterManualStop() {
  currentMode = MODE_MANUAL;
  serialDriveActive = false;
  stopMotors();
}

void enterMode(byte newMode, const __FlashStringHelper *announcement) {
  stopProgram();
  currentMode = newMode;
  avoidanceStage = STAGE_CRUISING;
  avoidScanRound = 0;
  avoidRotateSteps = 0;
  stageStartMillis = millis();
  stopMotors();
  commandServoAngle(90);
  replyText(announcement);
}

void printDiagnostics() {
  Serial.print(F("VER " KERNEL_VERSION " MODE "));
  Serial.print(currentMode);
  // (version first so a fresh flash is verifiable with a single 'd')
  Serial.print(F(" OWNER "));
  Serial.print(controlOwner);
  Serial.print(F(" SPEED "));
  Serial.print(speedLevel);
  Serial.print(F(" LINE "));
  Serial.print(digitalRead(lineSensorLeftPin));
  Serial.print(digitalRead(lineSensorRightPin));
  Serial.print(F(" PROX "));
  Serial.print(digitalRead(proximityLeftPin));
  Serial.print(digitalRead(proximityRightPin));
  Serial.print(F(" LIGHT "));
  Serial.print(analogRead(lightSensorLeftPin) / 10);
  Serial.print(F("/"));
  Serial.print(analogRead(lightSensorRightPin) / 10);
  Serial.print(F(" FRONT "));
  Serial.print(measureFrontDistance());
  Serial.print(F(" NOECHO "));
  Serial.print((int)sonarTimeoutStreak); // rising number = sonar dead/unplugged
  Serial.print(F(" SWAPTEST "));
  Serial.print(probeSonarWithRolesSwapped()); // nonzero = trig/echo crossed!
  Serial.print(F(" TRIM "));
  Serial.print(straightTrimPercent);
  Serial.print(F(" VCC "));
  Serial.println(readRailMillivolts());

  char diagnosticLine[120];
  snprintf(diagnosticLine, sizeof(diagnosticLine),
           "VER " KERNEL_VERSION " MODE %d SPD %d LN %d%d PX %d%d FR %d NOECHO %d SWAPTEST %d TRIM %d VCC %d PACK %d",
           (int)currentMode, speedLevel,
           digitalRead(lineSensorLeftPin), digitalRead(lineSensorRightPin),
           digitalRead(proximityLeftPin), digitalRead(proximityRightPin),
           measureFrontDistance(), (int)sonarTimeoutStreak,
           probeSonarWithRolesSwapped(), straightTrimPercent, readRailMillivolts(),
           packMillivolts);
  queueBluetoothLine(diagnosticLine);
}

// ===========================================================================
// Serial dead-man: a lost stop byte must not mean a runaway. Any manual
// drive commanded over serial (f/b/l/r or m) expires 400ms after the last
// such command; the joystick's 90ms stream refreshes it naturally, and the
// app's held buttons re-send while pressed.
// ===========================================================================

void serviceSerialDeadMan() {
  if (serialDriveActive && currentMode == MODE_MANUAL &&
      millis() - lastSerialDriveMillis > serialDriveTimeoutMillis) {
    serialDriveActive = false;
    stopMotors();
  }
}

void markSerialDrive() {
  serialDriveActive = true;
  lastSerialDriveMillis = millis();
}

// ===========================================================================
// Telemetry: compact CSV stream the Workshop app renders as live gauges.
// T,mode,lineL,lineR,proxL,proxR,ldrLraw,ldrRraw,frontCm,motorL,motorR
// Front distance is the cached value from the active mode's own pings, so
// the stream never adds extra sonar traffic.
// ===========================================================================

int readRailMillivolts() {
  // No spare analog pin for a battery divider (A0-A5 all spoken for), so we
  // measure the 5V rail against the AVR's internal 1.1V bandgap instead.
  // Tired batteries sag the regulator's output under load, so a falling
  // rail is the honest early warning even without touching the pack.
#if defined(__AVR__)
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1); // 1.1V vs AVcc
  delay(2);
  ADCSRA |= _BV(ADSC);
  while (ADCSRA & _BV(ADSC)) {
  }
  unsigned int reading = ADC;
  if (reading == 0) {
    return 0;
  }
  return (int)(1125300UL / reading); // 1.1V * 1023 * 1000
#else
  return 5000; // simulation stub
#endif
}

int measurePackMillivolts() {
  if (BATTERY_SENSE_PIN < 0) {
    return 0;
  }
  // Reference the reading against the TRUE rail (bandgap-measured), not an
  // assumed 5.00V: the rail itself sags as the pack drains, which would make
  // a naive fuel gauge read high exactly when accuracy matters most.
  // 100k/100k means a 50k source impedance — well above the ADC's preferred
  // 10k. The 0.1uF cap across R2 does the heavy lifting, but the first read
  // after switching channels can still carry charge from the previously
  // sampled pin (we read four other analog channels constantly), so throw
  // one away and keep the second.
  analogRead(BATTERY_SENSE_PIN);
  delayMicroseconds(150);
  long adcCounts = analogRead(BATTERY_SENSE_PIN);
  long railMv = readRailMillivolts();
  long pinMv = adcCounts * railMv / 1023L;
  long packMv = pinMv * (BATTERY_DIVIDER_R1_KOHMS + BATTERY_DIVIDER_R2_KOHMS) /
                BATTERY_DIVIDER_R2_KOHMS;
  return (int)(packMv * BATTERY_CALIBRATION_PERMILLE / 1000L);
}

void servicePackVoltage() {
  if (BATTERY_SENSE_PIN < 0) {
    return;
  }
  unsigned long now = millis();
  if (now - lastPackSampleMillis < 500) {
    return;
  }
  lastPackSampleMillis = now;

  // Only sample at rest: motor inrush drags the pack down hard, and cutting
  // out on a load dip would strand a robot with plenty of charge left.
  if (lastLeftMotorValue != 0 || lastRightMotorValue != 0) {
    return;
  }

  int reading = measurePackMillivolts();
  if (packSampleCount < 9) {
    packSamples[packSampleCount] = reading;
    packSampleCount = packSampleCount + 1;
  } else {
    for (byte i = 0; i < 8; i++) {
      packSamples[i] = packSamples[i + 1];
    }
    packSamples[8] = reading;
  }
  if (packSampleCount < 5) {
    return;
  }

  int sorted[9];
  for (byte i = 0; i < packSampleCount; i++) {
    sorted[i] = packSamples[i];
  }
  for (byte i = 1; i < packSampleCount; i++) { // insertion sort, tiny N
    int key = sorted[i];
    int j = i;
    while (j > 0 && sorted[j - 1] > key) {
      sorted[j] = sorted[j - 1];
      j = j - 1;
    }
    sorted[j] = key;
  }
  packMillivolts = sorted[packSampleCount / 2];

  if (packMillivolts < BATTERY_ABSENT_MILLIVOLTS) {
    // USB-powered bench session (or an unwired divider): no pack to protect.
    if (batteryCutoffActive) {
      batteryCutoffActive = false;
      replyText(F("BATTERY: running on USB, cutoff disarmed"));
    }
    return;
  }

  if (!batteryCutoffActive && packMillivolts < BATTERY_CUTOFF_MILLIVOLTS) {
    batteryCutoffActive = true;
    stopProgram();
    enterManualStop();
    releaseControl();
    replyValue(F("BATTERY CUTOFF mv="), packMillivolts);
  } else if (batteryCutoffActive && packMillivolts > BATTERY_CUTOFF_MILLIVOLTS + 300) {
    batteryCutoffActive = false; // fresh pack installed
    replyText(F("BATTERY OK, driving re-enabled"));
  }
}

int telemetryFullScalePwm() {
  // What "100%" means right now, so the app can draw honest motor bars.
  if (currentMode == MODE_MANUAL) {
    return currentDrivePwm();
  }
  if (currentMode == MODE_PROGRAM) {
    int drivePwm = currentDrivePwm();
    return drivePwm > autoTurnPwm ? drivePwm : autoTurnPwm;
  }
  if (currentMode == MODE_LIGHT_SEEK) {
    return autoCruisePwm;
  }
  if (currentMode == MODE_LINE_TRACK) {
    return autoCruisePwm - 30;
  }
  return autoTurnPwm; // avoid / follow / seek pivot at this ceiling
}

void serviceTelemetry() {
  if (!telemetryEnabled) {
    return;
  }
  // SoftwareSerial is half-duplex: transmitting corrupts reception. Stay
  // silent while bytes are arriving (or a program capture is open) so
  // telemetry can never garble an upload mid-flight.
  if (usbParser.capturingProgram || bluetoothParser.capturingProgram) {
    return;
  }
  unsigned long now = millis();
  if (now - lastByteReceivedMillis < receiveQuietWindowMillis) {
    return;
  }
  if (now - lastTelemetryMillis < 200) {
    return;
  }
  lastTelemetryMillis = now;

  char telemetryLine[96];
  snprintf(telemetryLine, sizeof(telemetryLine), "T,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
           (int)currentMode,
           digitalRead(lineSensorLeftPin), digitalRead(lineSensorRightPin),
           digitalRead(proximityLeftPin), digitalRead(proximityRightPin),
           analogRead(lightSensorLeftPin), analogRead(lightSensorRightPin),
           lastFrontDistanceCm,
           lastLeftMotorValue, lastRightMotorValue,
           telemetryFullScalePwm(), lastLogicalServoAngle,
           readRailMillivolts(), packMillivolts);
  Serial.println(telemetryLine);
  if (bluetoothReplyQueueLength > 0) {
    return; // let queued replies go out first; telemetry is never urgent
  }
  bluetoothSerial.println(telemetryLine);
}

// ===========================================================================
// Ultrasonic
// ===========================================================================

int probeSonarWithRolesSwapped() {
  // Answers "are Trig and Echo crossed somewhere in the harness?" without a
  // screwdriver: drive the trigger pulse out the pin we normally LISTEN on,
  // and listen on the pin we normally drive. A distance here + NOECHO on the
  // normal path = the two signal wires are swapped.
  pinMode(sonarEchoPin, OUTPUT);
  pinMode(sonarTriggerPin, INPUT);
  digitalWrite(sonarEchoPin, LOW);
  delayMicroseconds(2);
  digitalWrite(sonarEchoPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(sonarEchoPin, LOW);
  unsigned long echoMicros = pulseIn(sonarTriggerPin, HIGH, 20000UL);
  pinMode(sonarTriggerPin, OUTPUT);
  pinMode(sonarEchoPin, INPUT);
  return echoMicros == 0 ? 0 : (int)(echoMicros / 58UL);
}

int measureFrontDistance() {
  digitalWrite(sonarTriggerPin, LOW);
  delayMicroseconds(2);
  digitalWrite(sonarTriggerPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(sonarTriggerPin, LOW);

  unsigned long echoMicros = pulseIn(sonarEchoPin, HIGH, 20000UL);
  if (echoMicros == 0) {
    sonarTimeoutStreak = sonarTimeoutStreak + 1;
    lastFrontDistanceCm = 400;
    return 400; // no echo: treat as clear (also makes a missing sensor harmless)
  }
  sonarTimeoutStreak = 0;
  lastFrontDistanceCm = (int)(echoMicros / 58UL);
  return lastFrontDistanceCm;
}

// ===========================================================================
// Autonomous behaviors (unchanged logic from v1)
// ===========================================================================

void runLineTracking() {
  bool leftOnBlack = digitalRead(lineSensorLeftPin) == 1;
  bool rightOnBlack = digitalRead(lineSensorRightPin) == 1;

  int lineLookAngle = 90; // small lean into the correction; a bit twitchy is fine here
  if (!leftOnBlack && !rightOnBlack) {
    driveMotors(autoCruisePwm - 30, autoCruisePwm - 30);
  } else if (leftOnBlack && !rightOnBlack) {
    driveMotors(-autoTurnPwm + 60, autoTurnPwm - 60);
    lineLookAngle = 105;
  } else if (!leftOnBlack && rightOnBlack) {
    driveMotors(autoTurnPwm - 60, -autoTurnPwm + 60);
    lineLookAngle = 75;
  } else {
    stopMotors();
  }
  if (lineLookAngle != lastLogicalServoAngle) {
    commandServoAngle(lineLookAngle);
  }
}

void runObstacleAvoidance() {
  unsigned long now = millis();

  if (avoidanceStage == STAGE_CRUISING) {
    if (now - lastPingMillis < 60) {
      return;
    }
    lastPingMillis = now;

    int frontDistance = measureFrontDistance();
    bool leftBlocked = digitalRead(proximityLeftPin) == 0;
    bool rightBlocked = digitalRead(proximityRightPin) == 0;

    if (frontDistance <= obstacleStopCentimeters) {
      stopMotors();
      commandServoAngle(180);
      avoidanceStage = STAGE_LOOK_LEFT;
      stageStartMillis = now;
    } else if (leftBlocked && rightBlocked) {
      driveMotors(-autoTurnPwm, -autoTurnPwm); // boxed in: back out first
      avoidanceStage = STAGE_BACKING;
      stageStartMillis = now;
    } else if (leftBlocked) {
      // A prox hit means we're about to kiss the wall: stop pushing and
      // pivot AWAY decisively (spin right), then resume cruising.
      pendingTurnDirection = 1;
      driveMotors(pendingTurnDirection * autoTurnPwm, -pendingTurnDirection * autoTurnPwm);
      avoidanceStage = STAGE_TURNING;
      stageStartMillis = now;
    } else if (rightBlocked) {
      pendingTurnDirection = -1;
      driveMotors(pendingTurnDirection * autoTurnPwm, -pendingTurnDirection * autoTurnPwm);
      avoidanceStage = STAGE_TURNING;
      stageStartMillis = now;
    } else {
      driveMotors(autoCruisePwm, autoCruisePwm);
    }

  } else if (avoidanceStage == STAGE_LOOK_LEFT) {
    if (now - stageStartMillis >= 450) {
      scannedLeftDistance = measureFrontDistance();
      commandServoAngle(0);
      avoidanceStage = STAGE_LOOK_RIGHT;
      stageStartMillis = now;
    }

  } else if (avoidanceStage == STAGE_LOOK_RIGHT) {
    if (now - stageStartMillis >= 600) {
      scannedRightDistance = measureFrontDistance();
      commandServoAngle(90);
      avoidanceStage = STAGE_FRONT_RECHECK;
      stageStartMillis = now;
    }

  } else if (avoidanceStage == STAGE_FRONT_RECHECK) {
    if (now - stageStartMillis >= 350) { // servo settled back to center
      int frontNow = measureFrontDistance();

      if (frontNow > obstacleStopCentimeters + 10) {
        // Original direction is open (the obstacle moved, or a rotation
        // found daylight): just keep going the way we were pointed.
        avoidScanRound = 0;
        avoidRotateSteps = 0;
        avoidanceStage = STAGE_CRUISING;
        lastPingMillis = 0;
        replyText(F("AVOID forward clear, continuing"));
      } else if (scannedLeftDistance >= sideClearCentimeters ||
                 scannedRightDistance >= sideClearCentimeters) {
        avoidScanRound = 0;
        avoidRotateSteps = 0;
        pendingTurnDirection = (scannedRightDistance >= scannedLeftDistance) ? 1 : -1;
        driveMotors(pendingTurnDirection * autoTurnPwm, -pendingTurnDirection * autoTurnPwm);
        avoidanceStage = STAGE_TURNING;
        stageStartMillis = now;
      } else {
        avoidScanRound = avoidScanRound + 1;
        if (avoidScanRound < AVOID_SCAN_ROUNDS_PER_POSITION) {
          commandServoAngle(180); // look again from the same spot
          avoidanceStage = STAGE_LOOK_LEFT;
          stageStartMillis = now;
        } else {
          // Two full scans, nothing: widen the search — rotate the whole
          // chassis 45 degrees (same direction every time) and rescan.
          // Behind us is 4 steps away and SHOULD be clear.
          avoidScanRound = 0;
          avoidRotateSteps = avoidRotateSteps + 1;
          if (avoidRotateSteps >= AVOID_ROTATE_STEP_LIMIT) {
            enterManualStop();
            releaseControl();
            replyText(F("AVOID boxed in on all sides, giving up"));
            return;
          }
          driveMotors(autoTurnPwm, -autoTurnPwm);
          avoidanceStage = STAGE_ROTATE;
          stageStartMillis = now;
          replyValue(F("AVOID rotating 45, step "), avoidRotateSteps);
        }
      }
    }

  } else if (avoidanceStage == STAGE_ROTATE) {
    if (now - stageStartMillis >= AVOID_ROTATE_45_MS) {
      stopMotors();
      commandServoAngle(180);
      avoidanceStage = STAGE_LOOK_LEFT;
      stageStartMillis = now;
    }

  } else if (avoidanceStage == STAGE_TURNING) {
    if (now - stageStartMillis >= 320) {
      stopMotors();
      avoidanceStage = STAGE_CRUISING;
      lastPingMillis = 0;
    }

  } else if (avoidanceStage == STAGE_BACKING) {
    if (now - stageStartMillis >= 380) {
      driveMotors(autoTurnPwm, -autoTurnPwm);
      avoidanceStage = STAGE_TURNING;
      stageStartMillis = now;
    }
  }
}

void runFollow() {
  unsigned long now = millis();
  if (now - lastPingMillis < 60) {
    return;
  }
  lastPingMillis = now;

  int frontDistance = measureFrontDistance();
  bool leftSeen = digitalRead(proximityLeftPin) == 0;
  bool rightSeen = digitalRead(proximityRightPin) == 0;

  int followLookAngle = 90; // lean the sonar toward whichever side has the target
  if (frontDistance < 8) {
    driveMotors(-autoCruisePwm, -autoCruisePwm);
  } else if (frontDistance <= 15) {
    if (leftSeen && !rightSeen) {
      driveMotors(-autoTurnPwm + 40, autoTurnPwm - 40);
      followLookAngle = 115;
    } else if (rightSeen && !leftSeen) {
      driveMotors(autoTurnPwm - 40, -autoTurnPwm + 40);
      followLookAngle = 65;
    } else {
      stopMotors();
    }
  } else if (frontDistance <= 40) {
    driveMotors(autoCruisePwm, autoCruisePwm);
  } else if (leftSeen && !rightSeen) {
    driveMotors(-autoTurnPwm + 40, autoTurnPwm - 40);
    followLookAngle = 115;
  } else if (rightSeen && !leftSeen) {
    driveMotors(autoTurnPwm - 40, -autoTurnPwm + 40);
    followLookAngle = 65;
  } else {
    stopMotors();
  }
  if (followLookAngle != lastLogicalServoAngle) {
    commandServoAngle(followLookAngle);
  }
}

void runLightSeeking() {
  int leftLight = analogRead(lightSensorLeftPin) / 10;
  int rightLight = analogRead(lightSensorRightPin) / 10;

  if (leftLight > lightStopThreshold && rightLight > lightStopThreshold) {
    stopMotors();
    return;
  }

  int seekingDegree;
  if (leftLight > rightLight) {
    seekingDegree = (int)(((float)rightLight / (float)max(leftLight, 1)) * 90.0);
  } else {
    seekingDegree = 180 - (int)(((float)leftLight / (float)max(rightLight, 1)) * 90.0);
  }

  if (seekingDegree <= 90) {
    float balance = (float)seekingDegree / 90.0;
    driveMotors(autoCruisePwm, (int)(autoCruisePwm * balance));
  } else {
    float balance = (float)(180 - seekingDegree) / 90.0;
    driveMotors((int)(autoCruisePwm * balance), autoCruisePwm);
  }

  // Look where you're going: lean the sonar toward the light, harder curve =
  // bigger lean (max +/-40 deg). Rate-limited so the servo glides, not chatters.
  unsigned long now = millis();
  if (now - lastSteerLookMillis >= 150) {
    int steerLookAngle = 90 + ((seekingDegree - 90) * 40) / 90;
    if (abs(steerLookAngle - lastLogicalServoAngle) >= 3) {
      lastSteerLookMillis = now;
      commandServoAngle(steerLookAngle);
    }
  }
}

// ===========================================================================
// Stored program: capture, EEPROM persistence, interpreter
// ===========================================================================

void handleProgramLine(CommandParser &parser) {
  if (captureCount >= MAX_PROGRAM_STEPS) {
    return;
  }
  char op = (char)toupper(parser.lineBuffer[0]);
  bool validOp =
    op == 'F' || op == 'B' || op == 'L' || op == 'R' || op == 'W' ||
    op == 'V' || op == 'E' || op == 'P' || op == 'Q' || op == 'I' ||
    op == 'Z' || op == 'X';
  if (!validOp) {
    return;
  }
  programOps[captureCount] = op;
  programArgs[captureCount] = atoi(parser.lineBuffer + 1);
  captureCount = captureCount + 1;
}

int slotBaseAddress(byte slot) {
  return 1 + (int)(slot - 1) * SLOT_STRIDE; // byte 0 holds the magic
}

void saveProgramToEeprom() {
  EEPROM.update(0, EEPROM_MAGIC);
  int base = slotBaseAddress(activeSlot);
  EEPROM.update(base, programLength);
  int address = base + 1;
  for (int i = 0; i < programLength; i++) {
    EEPROM.update(address, (byte)programOps[i]);
    EEPROM.update(address + 1, (byte)(programArgs[i] & 0xFF));
    EEPROM.update(address + 2, (byte)((programArgs[i] >> 8) & 0xFF));
    address = address + 3;
  }
  loadedSlot = activeSlot;
}

bool loadProgramFromEeprom(byte slot) {
  if (EEPROM.read(0) != EEPROM_MAGIC) {
    programLength = 0;
    return false;
  }
  int base = slotBaseAddress(slot);
  byte storedLength = EEPROM.read(base);
  if (storedLength > MAX_PROGRAM_STEPS) {
    programLength = 0;
    return false;
  }
  programLength = storedLength;
  int address = base + 1;
  for (int i = 0; i < programLength; i++) {
    programOps[i] = (char)EEPROM.read(address);
    programArgs[i] = (int)EEPROM.read(address + 1) | ((int)EEPROM.read(address + 2) << 8);
    address = address + 3;
  }
  loadedSlot = slot;
  return programLength > 0;
}

void selectSlot(byte slot) {
  activeSlot = slot;
  loadProgramFromEeprom(slot);
  replyValue(F("SLOT "), activeSlot);
  replyValue(F("STEPS "), programLength);
}

void listStoredProgram() {
  if (loadedSlot != activeSlot) {
    loadProgramFromEeprom(activeSlot);
  }
  replyValue(F("SLOT "), activeSlot);
  replyValue(F("PROGRAM steps="), programLength);
  for (int i = 0; i < programLength; i++) {
    Serial.print(programOps[i]);
    Serial.print(F(" "));
    Serial.println(programArgs[i]);
    char listingLine[12];
    snprintf(listingLine, sizeof(listingLine), "%c %d", programOps[i], programArgs[i]);
    queueBluetoothLine(listingLine);
  }
}

void startProgramSlot(byte source, byte slot) {
  (void)source;
  if (loadedSlot != slot) {
    loadProgramFromEeprom(slot);
  }
  if (programLength == 0) {
    replyValue(F("EMPTY SLOT "), slot);
    return;
  }
  replyValue(F("SLOT "), slot);
  currentMode = MODE_PROGRAM;
  programRunning = true;
  programCounter = -1;
  loopStackDepth = 0;
  instructionEndMillis = 0;
  stopMotors();
  replyText(F("RUN"));
}

void stopProgram() {
  if (programRunning) {
    programRunning = false;
    stopMotors();
  }
}

void serviceProgramInterpreter() {
  if (!programRunning) {
    currentMode = MODE_MANUAL;
    return;
  }
  if (millis() < instructionEndMillis) {
    return;
  }
  advanceProgramCounter();
}

void advanceProgramCounter() {
  while (true) {
    programCounter = programCounter + 1;

    if (programCounter >= programLength) {
      programRunning = false;
      enterManualStop();
      releaseControl();
      replyText(F("DONE"));
      return;
    }

    char op = programOps[programCounter];
    int arg = programArgs[programCounter];

    // Ground truth for the phone: exactly which step starts, exactly when.
    // Unconditional: this is the instrument that settles "did it really run".
    char stepLine[24];
    snprintf(stepLine, sizeof(stepLine), "P,%d,%c,%d", programCounter, op, arg);
    Serial.println(stepLine);
    queueBluetoothLine(stepLine);

    if (op == 'F') {
      driveMotors(currentDrivePwm(), currentDrivePwm());
      instructionEndMillis = millis() + (unsigned long)constrain(arg, 0, 30000);
      return;
    } else if (op == 'B') {
      driveMotors(-currentDrivePwm(), -currentDrivePwm());
      instructionEndMillis = millis() + (unsigned long)constrain(arg, 0, 30000);
      return;
    } else if (op == 'L') {
      driveMotors(-autoTurnPwm, autoTurnPwm);
      instructionEndMillis = millis() + (unsigned long)constrain(arg, 0, 30000);
      return;
    } else if (op == 'R') {
      driveMotors(autoTurnPwm, -autoTurnPwm);
      instructionEndMillis = millis() + (unsigned long)constrain(arg, 0, 30000);
      return;
    } else if (op == 'W') {
      stopMotors();
      instructionEndMillis = millis() + (unsigned long)constrain(arg, 0, 30000);
      return;
    } else if (op == 'V') {
      speedLevel = constrain(arg, 0, 9);
    } else if (op == 'E') {
      commandServoAngle(arg);
    } else if (op == 'P') {
      if (loopStackDepth < LOOP_STACK_DEPTH) {
        loopStackStart[loopStackDepth] = programCounter;
        loopStackRemaining[loopStackDepth] = constrain(arg, 1, 1000);
        loopStackDepth = loopStackDepth + 1;
      }
    } else if (op == 'Q') {
      if (loopStackDepth > 0) {
        loopStackRemaining[loopStackDepth - 1] = loopStackRemaining[loopStackDepth - 1] - 1;
        if (loopStackRemaining[loopStackDepth - 1] > 0) {
          programCounter = loopStackStart[loopStackDepth - 1];
        } else {
          loopStackDepth = loopStackDepth - 1;
        }
      }
    } else if (op == 'I') {
      if (!readSensorCondition(arg)) {
        int endIndex = findMatchingEnd(programCounter, 'I', 'Z');
        if (endIndex >= 0) {
          programCounter = endIndex;
        }
      }
    } else if (op == 'Z') {
      // end-if marker: nothing to do
    } else if (op == 'X') {
      programCounter = programLength; // jump past the end; next pass finishes
    }
    // zero-duration ops fall through and execute the next instruction now
  }
}

int findMatchingEnd(int fromIndex, char openOp, char closeOp) {
  int depth = 0;
  for (int i = fromIndex + 1; i < programLength; i++) {
    if (programOps[i] == openOp) {
      depth = depth + 1;
    } else if (programOps[i] == closeOp) {
      if (depth == 0) {
        return i;
      }
      depth = depth - 1;
    }
  }
  return programLength - 1;
}

bool readSensorCondition(int sensorId) {
  if (sensorId == 1) {
    return digitalRead(lineSensorLeftPin) == 1;
  } else if (sensorId == 2) {
    return digitalRead(lineSensorRightPin) == 1;
  } else if (sensorId == 3) {
    return digitalRead(proximityLeftPin) == 0;
  } else if (sensorId == 4) {
    return digitalRead(proximityRightPin) == 0;
  } else if (sensorId == 5) {
    return measureFrontDistance() < 25;
  } else if (sensorId == 6) {
    return measureFrontDistance() >= 25; // front clear: the "else" of sensor 5
  } else if (sensorId == 7) {
    return digitalRead(proximityRightPin) == 1; // right clear: the "else" of 4
  } else if (sensorId == 8) {
    return digitalRead(proximityLeftPin) == 1; // left clear: the "else" of 3
  }
  return false;
}

// ===========================================================================
// Seek mode — SMOOTH PURSUIT with the servo parked straight ahead.
// (The servo rule "don't move while driving" applies to THIS MODE ONLY —
// it exists because seek is the one mode that ranges while chasing, and a
// swinging sensor on a moving chassis made distances useless.)
// Salute at mode start (wheels dead), then: chassis-only 360 survey ->
// aim -> smooth drive on center pings -> on loss, widening chassis search
// arcs (servo never leaves center) -> relock or give up.
// ===========================================================================

unsigned long searchArcDurationMs(unsigned int arcCount) {
  unsigned int index = arcCount < 3 ? arcCount : 3;
  return (unsigned long)SEARCH_ARC_DEGREES[index] * TURN_MS_PER_DEG_X100 / 100UL;
}

bool searchArcTurnsLeft(unsigned int arcCount) {
  return (arcCount % 2) == 0; // 25 L, 100 R, 200 L, 360 R, then keep alternating
}

bool distanceIsFollowable(int distanceCm) {
  return distanceCm >= SEEK_MIN_TARGET_CM && distanceCm <= SEEK_MAX_TARGET_CM;
}

void initializeSeekMode() {
  seekStage = SEEK_INTRO;
  seekIntroPhase = 0;
  seekStageStartMillis = millis();
  fruitlessScanCount = 0;
  trackLostPings = 0;
  stopMotors();
  commandServoAngle(115); // salute: left...
  replyText(F("SEEK hello"));
}

void beginSurveySpin() {
  seekStageStartMillis = millis();
  seekLastPingMillis = 0;
  seekBestDistanceCm = 9999;
  seekBestTimeMs = 0;
  driveMotors(-autoTurnPwm, autoTurnPwm); // chassis does ALL the rotating
  seekStage = SEEK_SCAN;
  replyText(F("SEEK survey 360"));
}

void beginSearchArc() {
  seekStageStartMillis = millis();
  seekLastPingMillis = 0;
  if (searchArcTurnsLeft(searchArcCount)) {
    driveMotors(-autoTurnPwm, autoTurnPwm);
  } else {
    driveMotors(autoTurnPwm, -autoTurnPwm);
  }
  unsigned int index = searchArcCount < 3 ? searchArcCount : 3;
  replyValue(F("SEEK widen deg="), (int)SEARCH_ARC_DEGREES[index]);
}

void runSeekFollow() {
  unsigned long now = millis();

  if (seekStage == SEEK_INTRO) {
    // ...right, center, then the servo's day is done; wheels take over.
    if (now - seekStageStartMillis >= 300) {
      seekStageStartMillis = now;
      seekIntroPhase = seekIntroPhase + 1;
      if (seekIntroPhase == 1) {
        commandServoAngle(65);
      } else if (seekIntroPhase == 2) {
        commandServoAngle(90);
      } else {
        beginSurveySpin();
      }
    }

  } else if (seekStage == SEEK_SCAN) {
    if (now - seekLastPingMillis >= 60) {
      seekLastPingMillis = now;
      int distance = measureFrontDistance();
      if (distanceIsFollowable(distance) && distance < seekBestDistanceCm) {
        seekBestDistanceCm = distance;
        seekBestTimeMs = now - seekStageStartMillis;
      }
    }
    if (now - seekStageStartMillis >= SCAN_FULL_TURN_MS) {
      if (seekBestDistanceCm < 9999) {
        replyValue(F("SEEK nearest cm="), seekBestDistanceCm);
        fruitlessScanCount = 0;
        seekStage = SEEK_AIM;
        seekStageStartMillis = now; // keep spinning left back to the bearing
      } else {
        fruitlessScanCount = fruitlessScanCount + 1;
        if (fruitlessScanCount >= FRUITLESS_SCAN_LIMIT) {
          enterManualStop();
          releaseControl();
          replyText(F("SEEK gave up: no echoes at all. Sonar plugged in?"));
          return;
        }
        replyText(F("SEEK nothing yet, rescanning"));
        beginSurveySpin();
      }
    }

  } else if (seekStage == SEEK_AIM) {
    if (now - seekStageStartMillis >= seekBestTimeMs) {
      stopMotors();
      seekStage = SEEK_TRACK;
      seekStageStartMillis = now;
      seekLastPingMillis = 0;
      trackLostPings = 0;
      replyText(F("SEEK LOCK"));
    }

  } else if (seekStage == SEEK_TRACK) {
    // Smooth pursuit: drive at the target on center pings, hold when close.
    if (now - seekLastPingMillis >= 100) {
      seekLastPingMillis = now;
      int distance = measureFrontDistance();
      if (distanceIsFollowable(distance)) {
        trackLostPings = 0;
        if (distance <= SEEK_HOLD_DISTANCE_CM) {
          stopMotors(); // close enough: hold here
        } else {
          driveMotors(autoCruisePwm, autoCruisePwm);
        }
        if (now - lastTrackStatusMillis >= 1000) {
          lastTrackStatusMillis = now;
          replyValue(F("TRK cm="), distance);
        }
      } else {
        trackLostPings = trackLostPings + 1;
        if (trackLostPings >= 3) {
          replyText(F("SEEK LOST, widening search"));
          searchArcCount = 0;
          beginSearchArc();
          seekStage = SEEK_SEARCH;
        }
      }
    }

  } else if (seekStage == SEEK_SEARCH) {
    if (now - seekLastPingMillis >= 60) {
      seekLastPingMillis = now;
      int distance = measureFrontDistance();
      if (distanceIsFollowable(distance)) { // acquisition: stop on first sighting
        stopMotors();
        seekStage = SEEK_TRACK;
        seekStageStartMillis = now;
        seekLastPingMillis = 0;
        trackLostPings = 0;
        replyValue(F("SEEK RELOCK cm="), distance);
        return;
      }
    }
    if (now - seekStageStartMillis >= searchArcDurationMs(searchArcCount)) {
      searchArcCount = searchArcCount + 1;
      if (searchArcCount >= SEARCH_ARC_LIMIT) {
        enterManualStop();
        releaseControl();
        replyText(F("SEEK gave up: lost it for good"));
        return;
      }
      beginSearchArc();
    }
  }
}

// ===========================================================================
// IR remote (NEC decoder on INT1) — the power-on default controller
// ===========================================================================

void handleInfraredEdge() {
  unsigned long now = micros();
  unsigned long gap = now - infraredLastFallMicros;
  infraredLastFallMicros = now;

  if (gap > 12000UL && gap < 15000UL) {
    infraredFrameInProgress = true;
    infraredBitCount = 0;
    infraredShiftRegister = 0;
  } else if (gap > 10000UL && gap <= 12000UL) {
    infraredLastActivityMillis = millis();
  } else if (infraredFrameInProgress) {
    if (gap > 900UL && gap < 1400UL) {
      infraredShiftRegister = infraredShiftRegister >> 1;
      infraredBitCount = infraredBitCount + 1;
    } else if (gap > 1900UL && gap < 2700UL) {
      infraredShiftRegister = (infraredShiftRegister >> 1) | 0x80000000UL;
      infraredBitCount = infraredBitCount + 1;
    } else {
      infraredFrameInProgress = false;
    }

    if (infraredBitCount == 32) {
      infraredFrameInProgress = false;
      byte command = (byte)(infraredShiftRegister >> 16);
      byte commandInverse = (byte)(infraredShiftRegister >> 24);
      if ((command ^ commandInverse) == 0xFF) {
        infraredLastCommand = command;
        infraredLastActivityMillis = millis();
      }
    }
  }
}

void serviceInfraredRemote() {
  noInterrupts();
  byte activeCommand = infraredLastCommand;
  unsigned long lastActivity = infraredLastActivityMillis;
  interrupts();

  bool keyIsHeld = (activeCommand != 0) && (millis() - lastActivity < infraredHoldTimeoutMillis);

  if (!keyIsHeld) {
    if (infraredWasDriving) {
      infraredWasDriving = false;
      stopMotors();
      lastOwnerActivityMillis = millis();
    }
    if (activeCommand != 0 && millis() - lastActivity >= infraredHoldTimeoutMillis) {
      clearInfraredCommand();
    }
    return;
  }

  if (activeCommand == IR_KEY_OK) { // STOP: always honored
    stopProgram();
    enterManualStop();
    releaseControl();
    clearInfraredCommand();
    return;
  }

  if (!claimControl(SOURCE_IR)) {
    clearInfraredCommand();
    return;
  }

  if (activeCommand == IR_KEY_UP) {
    stopProgram();
    currentMode = MODE_MANUAL;
    infraredWasDriving = true;
    driveMotors(currentDrivePwm(), currentDrivePwm());
  } else if (activeCommand == IR_KEY_DOWN) {
    stopProgram();
    currentMode = MODE_MANUAL;
    infraredWasDriving = true;
    driveMotors(-currentDrivePwm(), -currentDrivePwm());
  } else if (activeCommand == IR_KEY_LEFT) {
    stopProgram();
    currentMode = MODE_MANUAL;
    infraredWasDriving = true;
    driveMotors(-autoTurnPwm, autoTurnPwm);
  } else if (activeCommand == IR_KEY_RIGHT) {
    stopProgram();
    currentMode = MODE_MANUAL;
    infraredWasDriving = true;
    driveMotors(autoTurnPwm, -autoTurnPwm);
  } else if (activeCommand == IR_KEY_1) {
    startProgramSlot(SOURCE_IR, 1);
    clearInfraredCommand();
  } else if (activeCommand == IR_KEY_2) {
    startProgramSlot(SOURCE_IR, 2);
    clearInfraredCommand();
  } else if (activeCommand == IR_KEY_3) {
    startProgramSlot(SOURCE_IR, 3);
    clearInfraredCommand();
  } else if (activeCommand == IR_KEY_4) {
    enterMode(MODE_LINE_TRACK, F("MODE LINE_TRACK (IR)"));
    clearInfraredCommand();
  } else if (activeCommand == IR_KEY_5) {
    enterMode(MODE_AVOID, F("MODE AVOID (IR)"));
    clearInfraredCommand();
  } else if (activeCommand == IR_KEY_6) {
    enterMode(MODE_FOLLOW, F("MODE FOLLOW (IR)"));
    clearInfraredCommand();
  } else if (activeCommand == IR_KEY_7) {
    enterMode(MODE_LIGHT_SEEK, F("MODE LIGHT_SEEK (IR)"));
    clearInfraredCommand();
  } else if (activeCommand == IR_KEY_8) {
    enterMode(MODE_SEEK, F("MODE SEEK (IR)"));
    initializeSeekMode();
    clearInfraredCommand();
  } else if (activeCommand == IR_KEY_9) {
    speedLevel = speedLevel >= 9 ? 3 : speedLevel + 1; // cycle 3..9, never crawl
    replyValue(F("SPEED "), speedLevel);
    clearInfraredCommand();
  } else if (activeCommand == IR_KEY_0) {
    stopProgram();
    enterManualStop();
    releaseControl();
    replyText(F("FREE"));
    clearInfraredCommand();
  } else {
    replyValue(F("IR? 0x"), activeCommand);
    clearInfraredCommand();
  }
}

void clearInfraredCommand() {
  noInterrupts();
  infraredLastCommand = 0;
  interrupts();
  infraredWasDriving = false;
}
