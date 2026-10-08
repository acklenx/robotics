// mecanum_holonomic.ino   (v0.10.76 - L293D shield, Bluetooth on A0/A1 as shipped, IMU,
//                          ADS1115 inputs and VL53L0X laser found on I2C at boot, IR
//                          remote on D2, HC-SR04 sonar on A2/A3, line follower and light seeker set up at
//                          runtime, saved in EEPROM)

//
// Holonomic drive for a 4WD mecanum robot (LAFVIN 4WD kit, Arduino Uno class).
// Motor driver: the L293D motor shield (two L293D + one 74HC595), i.e. the
// Adafruit Motor Shield v1 layout. No encoders. Every preprogrammed move is
// TIMED and scaled by the calibration constants in the CONFIG section.
//
// IMU (optional, found at boot): a BNO055 or an MPU-6050 on I2C. When one
// answers, the motion engine holds heading against it, every move ends with
// a settle step that nulls the heading error, and the cr calibration run
// measures its own spin. Without one, everything is open loop.
//
// Line follower: an 8-channel IR line sensor array (analog outputs),
// per-channel min/max calibration and a weighted-centroid line position.
// Following uses the holonomic drive: it strafes back onto the line AND
// turns to follow it.
//
// Light seeker: up to eight LDR + resistor dividers, each with a bearing.
// Seeking sums them into a direction vector and drives along it, which with
// three or more LDRs means the robot slides toward the torch from any side.
//
// Both are set up WITHOUT re-flashing: which inputs, where each sensor sits,
// gains and thresholds come from the k commands (the Mecanum Workshop app's
// Sensors tab sends them) and kw keeps them in EEPROM. Sensors can be on Uno
// pins, a 74HC4051/4067 mux, or ADS1115  boards on I2C. There are two saved
// layouts, WIRED and ADC; at boot the sketch runs the ADC layout if its
// ADS1115 boards answer, else the wired one, so one robot can go either way.
//
// PIN PLAN. The L293D shield owns 3 4 5 6 7 8 11 12. Free: 2 9 10 13, A0-A5.
// As shipped: Bluetooth module on A0 (RX) / A1 (TX) through SoftwareSerial,
// HC-SR04 sonar on A2 (echo) / A3 (trig). This sketch keeps the Bluetooth
// wiring so the phone works right after flashing, and reads the sonar (ru)
// while no sensor is wired to A2/A3.
//
//   with ADS1115s          A0/A1 Bluetooth   A2/A3 sonar   A4/A5 I2C: IMU + ADS1115s
//                          + VL53L0X laser; line D1-D8 and the LDRs on the ADS1115s
//   IR remote receiver     D2 (INT0) in every layout; timed on Timer1
//   8-channel mux          A0/A1 Bluetooth   A2 mux common, selects 9 10 13
//                          A3 free (no sonar) A4/A5 I2C   6 line + 2 LDRs on the mux
//   no I2C, no mux         A0/A1 Bluetooth   A2-A5: any four line / LDR inputs
// setup() and every k change of a pin print a PIN CLASH warning when two
// features share a pin; k? lists the analog pins that are already taken.
//
// ---------------------------------------------------------------------------
// CONVENTIONS  (locked - everything else is derived from these)
//
//   Body frame:   +X = FORWARD    +Y = RIGHT
//   Rotation:     CLOCKWISE viewed from above is POSITIVE.
//   Bearings:     degrees clockwise from straight ahead. 0 = ahead, 90 = right,
//                 180 = behind, -90 (or 270) = left.
//   Pivot point:  expressed in the body frame AS IT IS AT THE START OF THE MOVE,
//                 in millimetres. (0,0) = spin in place. (1000,1000) = a point
//                 1 m ahead and 1 m to the right.
//   Polar pivot:  bearingDegrees + distanceMm, same frame.
//   Sweep:        degrees the robot's CENTRE travels around the pivot, signed.
//                 Positive = clockwise around the pivot as seen from above.
//   faceCenter:   true  -> the body rotates with the orbit, so the pivot stays
//                          at the same bearing from the robot the whole time
//                          (a pivot dead ahead stays dead ahead = "face it").
//                 false -> the body's heading changes by spinDegrees, spread
//                          evenly over the move. spinDegrees = 0 means the
//                          robot keeps its heading and pure-strafes the circle.
//   Rollers:      form an X when viewed from above.
//   Wheel order:  0 = FL   1 = FR   2 = RL   3 = RR   (everywhere).
//
// ---------------------------------------------------------------------------
// SERIAL COMMANDS (USB or Bluetooth, 9600 baud, single characters). Replies
// go to both. The kit's Bluetooth module is a classic SPP module wired to
// A0/A1 and read through SoftwareSerial, exactly as the factory sketch does.
//
//   w s a d   forward / back / strafe left / strafe right   (latched until x)
//   q e       spin counter-clockwise / clockwise            (latched until x)
//   x         stop everything, also aborts a running demo
//   m<bearing>,<mm>,<turn>;  a block-program step: mm on a bearing while turning
//             turn degrees (mm 0 = turn in place); move: start ... move: done
//   v<f>,<r>,<s>;  drive forward / right / clockwise at -100..100 % of full
//             speed, all at once (the two-stick controller); stops unless
//             repeated within VECTOR_TIMEOUT_MS. No reply.
//   1 - 9     manual speed level (fraction of MAX_WHEEL_MM_PER_SEC)
//   p<n>      run demo n (see DEMOS). 'p' plus anything else lists this build's demos.
//   z<n>      demo size: z1 .. z8 = 25 % .. 200 % of every demo distance (z4 = 100 %)
//   ch        calibration run: forward at half power (PWM 128), for HALF_POWER_MM_PER_SEC
//   cf cs cr  calibration runs: forward / strafe-right / spin-clockwise at
//             full power for CALIBRATION_RUN_MS, then stop. Measure, then
//             update the CONFIG constants.
//   t0 - t3   twitch shield channel M1 - M4 forward for 300 ms, ignoring the
//             wiring table. Use to fill in wheelWiring[].
//   i         IMU status (which one, enabled? heading, calibration / gyro offset)
//   iw        watch the heading: I,<degrees> (0..360, clockwise) at 5 Hz until x
//   ih        the heading once: I,<degrees>
//   in  if    IMU heading hold on / off (only if compiled in and detected)
//   ls        line sensor: one-shot reading (raw, normalised, position)
//   lw  bw    watch the line / light sensors: LR,raw... + L,... (or BR / B)
//             at 5 Hz with the robot standing still, until x
//   lc        line sensor: calibrate by strafing across the line for a few seconds
//   lf        line follow until x (streams L,... telemetry at 5 Hz)
//   bs        light sensors: one-shot reading (raw, normalised, direction)
//   bc        light sensors: 5 s calibration window: cover each LDR, then shine on each
//   bf        light seek until x (streams B,... telemetry at 5 Hz)
//   rs  rw    laser range: one reading / watch (with the sonar) until x
//   ru        sonar: one reading
//   k?;       sensor settings as K,<key>,<value> lines (see k COMMANDS below)
//   kp;       probe I2C for ADS1115 boards again and pick the layout
//   k<key>=<value>;  change one line / light setting, live; kw; saves to EEPROM
//   ku;  kr;  undo unsaved changes / back to the sketch's CONFIG defaults
//   ?         print this list
//
// DEMOS (always in)
//   pk  circle strafe IN: orbit a point 500 mm ahead, nose locked on it
// DEMOS (USE_EXTRA_DEMOS 1)
//   pu  turn around: spin 180 in place
//   pc  crab zigzag: diagonal strafes out and back, heading held
//   pd  J-turn: straight out while spinning 180, straight back while spinning 180
//   pj  parallel park: pull past the spot, strafe in, strafe out, reverse
//   p1  circle strafe: heading held, centre orbits a point 400 mm ahead
//   p2  square, body spins a full 360 on every side
//   p3  square, always facing the NEXT direction of travel at each corner
//   p4  square, always facing IN toward the square's centre at each corner
//   p5  square, always facing OUT from the square's centre at each corner
//   p6  figure eight, car-style (heading follows the curve)
//   p7  compass rose: strafe out and back on all 8 bearings, heading held
//   p8  orbit a point ahead while spinning twice (the party trick)
//   p9  spiral out: quarter arcs of growing radius, heading follows curve
//   pa  pendulum: swing +-60 around a point 500 mm ahead, always facing it
//   pb  pentagram: five straight legs, heading held (draws a star)
//   pe  figure eight, heading held (compare with p6)
//   pf  triangle, always facing IN at each corner
//   pg  hexagon, heading held (pure strafing around six sides)
//   ph  flower: five petal circles through the start point
//   pi  slalom: alternating quarter arcs out, spin, and back
// ---------------------------------------------------------------------------

#include <Arduino.h>

#define FIRMWARE_VERSION "0.10.76"   // banner and K,fw (bump the build number on every change)

// The robot. Calibration is its own program, ../calibration/calibration.ino:
// it measures the motion numbers and saves them in EEPROM for this one to
// read. Code both need (motor shield, speed curve, wheel math, EEPROM layout)
// must match: scripts/check-mecanum-shared.js compares them.
#define SKETCH_NAME "mecanum_holonomic"
#include <avr/eeprom.h>

// The Arduino build inserts function prototypes above the first function in
// the file, which is now readCommandByte() below; this lets those prototypes
// mention these structs before they are defined.
struct AnalogSource;
struct SensorSettings;
struct LineLayout;
struct LightLayout;
struct SettingKey;
struct DemoStep;

// ===========================================================================
// CONFIG - Bluetooth (classic SPP module on SoftwareSerial, as shipped)
// ===========================================================================
// 1 = read commands from, and echo replies to, a serial Bluetooth module on
// the pins below (the factory wiring is A0/A1). 0 = USB only. Move the module
// to 9/10 (the free servo headers) if you need A0/A1 for sensors.
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
const float WHEEL_DIAMETER_MM = 60.0;   // not used open loop; needed once encoders arrive
const float WHEEL_WIDTH_MM    = 20.0;   // roller contact width, so the outer edges span 150 mm
// Measured on the LAFVIN 4WD chassis. Only the SUM of wheelbase and track
// matters for the kinematics (see rotationLeverMm()).

// ===========================================================================
// CONFIG - motion calibration
// ===========================================================================
// calibration.ino measures these on each robot,
// with the Mecanum Workshop app's Calibrate tab, and saves them in the robot's
// EEPROM; this sketch reads them at power-on. Uploading a new version of
// either sketch keeps them: an upload does not touch EEPROM. The numbers below
// are the FLEET DEFAULTS, used when a robot's EEPROM holds no calibration:
// measured on the LAFVIN 4WD on 2026-10-01 (IMU spin curve, a 1 m drive on
// the stock sketch that went 1630 mm), so an uncalibrated robot of the same
// build starts close.
//
// CALIBRATION_IN_EEPROM 0 bakes them into the program instead: paste one
// robot's numbers here (the app's "As sketch code") and this build ignores
// EEPROM. The live-adjust code is left out, and with the numbers fixed at
// compile time the motion code is smaller too.
#define CALIBRATION_IN_EEPROM 1   // 1: numbers saved on the robot (the calibration page's Save) override the ones below

// The speed curve: PWM to wheel speed at the rim, through five points:
// 0 mm/s at PWM_DEADBAND, then the speeds at CURVE_LOW_PWM (the lowest power
// that reliably starts it turning in place), 128, 192 and 255. Gear motors
// are not straight lines: half power is much more than half speed.
const int PWM_DEADBAND = 84;                   // one under CURVE_LOW_PWM: anything below is "off"
const int CURVE_LOW_PWM = 85;                  // start power: the highest any robot needs (robot 1: 78 on tired batteries, robot 2: 85, 2026-10-07)
const float SPEED_AT_LOW_PWM = 335.0;         // at CURVE_LOW_PWM 85, robot 2 on fresh batteries, 2026-10-07: 650 mm in 2 s, 1320 in 4 s (robot 1 at 78, tired batteries: 249)
const float HALF_POWER_MM_PER_SEC = 480.0;     // speed at PWM 128 (robot 2, fresh batteries, 2026-10-07; robot 1 tired: 360)
const float SPEED_AT_PWM_192 = 600.0;          // (robot 1 tired: 505)
const float MAX_WHEEL_MM_PER_SEC = 690.0;      // speed at PWM 255 (robot 1 tired: 590)

// Spin degrees / ideal spin degrees (ideal = wheel distance / lever), and
// strafe distance / forward distance (the rollers slip: 615 / 1380 mm).
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

// Start and stop: every timed move cuts the motors this many ms early (at the
// speed it ends on) so the rolling on after the stop lands it on the target.
// Negative: it runs that much LATE instead, because the motors take longer
// to get going than the robot rolls on (measured on the LAFVIN: it ends
// short). The calibration page's "Start and stop" step sets it.
const long SPIN_COAST_MS = -46;               // robot 2, hard stop, 2026-10-07: a 1 m move went ~1070 with -255 (negative: the motors run late at the start speed)

// Per wheel (FL FR RL RR): PWM added where it starts turning, and a gain on
// the power above that, so a weak motor gets more and the robot runs straight.
constexpr float WHEEL_START[4] = { 0, 0, 0, 0 };
constexpr float WHEEL_GAIN[4] = { 1, 1, 1, 1 };
// The wheel speed where STRAFING starts to move. Sideways the rollers drag
// (on a rug most of all), so it takes more than driving forward; moves and
// manual driving start a strafe there. The calibration sketch measures it
// with the accelerometer; until then it is the forward start speed.
const float STRAFE_START_SPEED = SPEED_AT_LOW_PWM;
// How fast moves speed up from the start speed (where they jump to: below it
// the motors hum, and the weakest one would not turn) and slow down to it at
// the end, in mm/s per second at the wheel. Slippery floors need less, or the
// wheels spin and the robot twists. calibration.ino measures it per floor.
const float ACCEL_MM_PER_S2 = 1500;

// The numbers the robot runs on, in this order (K,cal reports them; the
// calibration sketch's km<i>=<value>; changes one). M_TAG: which surface the
// set was made on (1 carpet, 2 rug, 3 hardwood, 4 tile, 5 concrete; 0 none).
const byte SURFACE_TAG = 0;
enum { M_DEADBAND, M_LOW_PWM, M_LOW_SPEED, M_HALF, M_192, M_MAX, M_SPIN_EFF, M_STRAFE_EFF, M_COAST, M_TAG,
       M_GAIN0, M_START0 = M_GAIN0 + 4, M_STRAFE_START = M_START0 + 4, M_ACCEL, M_FRONT_BIAS, M_DIAG_BIAS, M_FWD_ROLL_BIAS, M_COUNT };
#define MOTION_DEFAULTS { PWM_DEADBAND, CURVE_LOW_PWM, SPEED_AT_LOW_PWM, HALF_POWER_MM_PER_SEC, SPEED_AT_PWM_192, \
  MAX_WHEEL_MM_PER_SEC, SPIN_EFFICIENCY, STRAFE_EFFICIENCY, SPIN_COAST_MS, SURFACE_TAG, \
  WHEEL_GAIN[0], WHEEL_GAIN[1], WHEEL_GAIN[2], WHEEL_GAIN[3], WHEEL_START[0], WHEEL_START[1], WHEEL_START[2], WHEEL_START[3], STRAFE_START_SPEED, \
  ACCEL_MM_PER_S2, STRAFE_FRONT_BIAS, STRAFE_DIAG_BIAS, STRAFE_FWD_ROLL_BIAS }
#if CALIBRATION_IN_EEPROM
float motion[M_COUNT] = MOTION_DEFAULTS;
#else
const float motion[M_COUNT] = MOTION_DEFAULTS;
#endif

const unsigned long CALIBRATION_RUN_MS = 2000;
const unsigned long CONTROL_TICK_MS    = 20;      // update rate for time-varying moves
const float DEFAULT_SPIN_DEG_PER_SEC   = 90.0;    // used when the pivot is (0,0)

// Demo tuning
const float DEMO_SPEED_MM_PER_SEC   = 480.0;   // programs and demos drive at this: half power, well off the stall edge
const float DEMO_SQUARE_SIDE_MM     = 600.0;
const unsigned long DEMO_PAUSE_MS   = 300;        // breath between segments

// Manual (w/a/s/d/q/e) dead-man timeout. 0 = latch until 'x'. Set to e.g. 800
// if the Bluetooth app sends a key repeatedly while held.
const unsigned long MANUAL_TIMEOUT_MS = 0;

// v<forward>,<right>,<spin>; (each -100..100 % of full speed, from the
// two-stick controller) is a dead-man command: the robot stops if the next
// one does not arrive within this long. The controller resends every 150 ms.
const unsigned long VECTOR_TIMEOUT_MS = 500;

// Heartbeat on the L LED (pin 13), off by default since v0.10.13 for flash
// (the app's status line shows the robot is alive): a 60 ms blip every 1.5 s while the sketch
// is idle and listening. The factory Blink that ships on a bare Uno is a
// steady 1 s on / 1 s off, so the two are easy to tell apart. A blocking
// move (demo, calibration run) pauses the blip until it finishes. The pin is
// needed by the mux selects, so the heartbeat goes away when the mux is on.
#define USE_HEARTBEAT 0
const byte HEARTBEAT_PIN = 13;
const unsigned long HEARTBEAT_PERIOD_MS = 1500;
const unsigned long HEARTBEAT_ON_MS = 60;

// ===========================================================================
// CONFIG - IMU: BNO055 or MPU-6050, found automatically on I2C
// ===========================================================================
// USE_IMU 1 (default): at boot the sketch looks for a BNO055 (0x28 / 0x29),
// then an MPU-6050 (0x68 / 0x69) on A4/A5, and uses the first that answers.
// Heading hold is ON when one is found; toggle at runtime with in / if. With
// it on, the motion engine holds heading against the gyro, every move ends
// with a settle step that nulls the heading error, and cr measures its own
// spin. Without one, everything is open loop.
//   BNO055: fuses gyro + accel on the chip; the heading is one register.
//   MPU-6050: only its gyro is used; the sketch integrates the Z rate. It
//     measures the gyro's offset for MPU6050_BIAS_MS at boot, so power on
//     with the robot sitting still. Drifts a degree or so a minute: fine for
//     holding heading through a move.
// Wiring either one: VIN/VCC 5V, GND, SDA A4, SCL A5. No library needed.
#define USE_IMU 1

// BNO055 fusion mode. IMUPLUS (gyro + accel, no magnetometer) gives a heading
// that is relative to power-on but immune to the DC motors' magnetic fields.
// NDOF (0x0C) adds the compass: absolute, but the motors pull it around.
const byte  BNO055_MODE = 0x08;
const float IMU_HEADING_SIGN = 1.0;      // -1.0 if the board is mounted upside down
const unsigned long MPU6050_BIAS_MS = 1000;
// Gyro range +-500 deg/s. A full-power spin (the cr run) is ~250 deg/s on the
// LAFVIN chassis, right at the +-250 range's limit, where the gyro clips and
// the measured spin (and SPIN_EFFICIENCY) comes out low without a warning.
const float MPU6050_COUNTS_PER_DPS = 65.5;
const float BUMP_G = 0.7;                // a sideways / end-on jolt this big = hit something: stop
const float LIFT_G = 0.4;                // straight down this far from 1 g = picked up: stop
const float HEADING_HOLD_KP = 3.0;       // rad/s of correction per rad of error
const float HEADING_HOLD_KI = 4.0;       // per second: pulls a steady lean back to 0
const float HEADING_HOLD_KD = 1.0;       // share of a wrong turn rate countered at once (catches the start)
const float HEADING_SETTLE_TOLERANCE_DEG = 1.0;
const unsigned long HEADING_SETTLE_MS = 1000;  // max time spent nulling the error after a move
const float SETTLE_MIN_DEG_PER_SEC = 25.0;     // slowest settle turn: slower stalls a few degrees short

// ===========================================================================
// CONFIG - ADS1115 analog inputs on I2C (optional, found automatically)
// ===========================================================================
// USE_ADS1115 1 (default): at boot the sketch asks addresses 0x48-0x4B and
// remembers which ADS1115 boards answered. Sensors can then be wired to
// ADS(chip, channel): chip 0..3 = address 0x48..0x4B (ADDR pin to GND, VDD,
// SDA, SCL), channel AIN0..AIN3 (printed A0-A3 on the breakout; those are
// the ADC's inputs, not the Uno's). Put them anywhere on the SDA / SCL
// lines, power them from 5V so a 0-5 V sensor stays in range. Readings are
// scaled to the Uno's own 0..1023, so calibration works the same.
#define USE_ADS1115 0

// ===========================================================================
// CONFIG - VL53L0X time-of-flight laser rangefinder (optional, found on I2C)
// ===========================================================================
// USE_TOF 1: at boot the sketch looks for a VL53L0X at 0x29 on A4/A5 (VIN 5V,
// GND, SDA A4, SCL A5; the common purple GY-VL53L0XV2 breakout has its own
// regulator). It measures 30-1200 mm straight ahead in a narrow cone, far
// more precisely than the sonar. A BNO055 must then stay at its default
// 0x28. With one found, r reads it, rw watches it, and forward moves stop
// short of a wall (TOF_STOP_MM, 0 = never).
#define USE_TOF 0
const int TOF_STOP_MM = 120;             // forward manual driving stops closer than this

// ===========================================================================
// CONFIG - HC-SR04 sonar (the kit's own, A2 echo / A3 trig as shipped)
// ===========================================================================
// USE_SONAR 1: ru reads it once (U,<mm>, -1 = no echo within ~4 m) and rw
// shows it next to the laser. It only drives A2/A3 while no line or light
// sensor (or the mux common) is wired to them directly: sensors win, and the
// sonar then answers "sonar: A2/A3 are wired to sensors". Each reading blocks
// until the echo returns (up to SONAR_TIMEOUT_US).
#define USE_SONAR 0
const byte SONAR_ECHO_PIN = A2;
const byte SONAR_TRIG_PIN = A3;
const unsigned long SONAR_TIMEOUT_US = 25000;   // round trip, ~4.3 m

// ===========================================================================
// CONFIG - IR remote (the kit's 17- or 21-key NEC remote) on D2
// ===========================================================================
// USE_IR_REMOTE 1: a 38 kHz IR receiver (VS1838B / HX1838: OUT to D2, VCC 5V,
// GND) decoded on the D2 interrupt, with no library and no timer (IRremote
// would take Timer2 from the M1/M2 motor PWM). Keys hold-to-drive: moves stop
// IR_RELEASE_MS after the last repeat. The 74HC4067 mux's S3 also wants D2,
// so use a 74HC4051 (or MUX_CHANNELS 8) with the IR receiver.
#define USE_IR_REMOTE 1
const byte IR_RECEIVER_PIN = 2;          // must be D2 (INT0)
const unsigned long IR_RELEASE_MS = 250; // the remote repeats every 108 ms while held

// ===========================================================================
// CONFIG - analog inputs, and the optional 74HC4067 / 74HC4051 multiplexer
// ===========================================================================
// Every analog sensor below is an AnalogSource: either a direct Uno pin
// (DIRECT(A2)) or a channel of one multiplexer whose common pin goes to an
// analog input (MUXED(5)). The select pins are only driven while the running
// layout actually uses a mux channel; until then D13 blinks the heartbeat and
// D9/D10 stay free. USE_ANALOG_MUX 0 (the default since v0.10.8: ADS1115
// boards are the recommended way to more inputs) leaves the mux code out
// (MUXED entries then read 0) and saves ~340 bytes of flash.
#define USE_ANALOG_MUX 0

const byte MUX_COMMON_PIN = A2;   // A0/A1 stay with the Bluetooth module
// 8 = a 74HC4051, or a 74HC4067 with S3 tied to GND: selects S0-S2 only, so
// D2 stays free (for an IR receiver). 16 = a 74HC4067 with S3 on D2.
const byte MUX_CHANNELS = 8;
const byte MUX_SELECT_PINS[4] = { 9, 10, 13, 2 };   // S0 S1 S2 S3
const byte MUX_SELECT_COUNT = MUX_CHANNELS > 8 ? 4 : 3;

struct AnalogSource {
  byte pin;          // Uno analog pin to read
  int8_t muxChannel; // -1 = direct, otherwise the multiplexer channel
};
#define DIRECT(p) { (p), -1 }
#define MUXED(ch) { MUX_COMMON_PIN, (ch) }

#define NO_SOURCE { 0xFF, -1 }   // channel not wired to anything; reads 0
#define ADS(chip, ch) { (byte)(0xF0 | (chip)), (ch) }   // ADS1115 at 0x48 + chip, input AINch

// ===========================================================================
// CONFIG - line follower and light seeker DEFAULTS (set up at runtime)
// ===========================================================================
// Both are always compiled in and configured WITHOUT re-flashing: the
// Mecanum Workshop app's Sensors tab (or the k commands, see SERIAL
// COMMANDS) picks the pins, sensor positions, gains and thresholds, and kw
// saves them in the Uno's EEPROM so they survive power-off. Calibration from
// lc / bc is saved automatically. A sensor count of 0 means "not wired".
//
// The values below are what a freshly flashed robot starts with and what kr
// goes back to. Edit one and re-flash, and the robot notices the defaults
// changed and ignores the settings the app saved, so an edit here always wins.

// --- Line sensor: 8-channel IR array ("8 bit hunt board"), analog outputs ---
// VCC 3.3-5 V, GND, D1..D8 outputs left to right, 8 mm pitch. Each wired
// channel needs a source (a0..a5 or a mux channel) and its offset: its
// position on the board relative to the centre in mm, left negative. D1..D8
// are -28 -20 -12 -4 4 12 20 28.
//   2 channels D3 + D6 (offsets -12 12): enough on 19 mm tape, no crosstalk
//   4 channels D2 D4 D5 D7 (-20 -4 4 20): the sweet spot without a mux
//   all 8 on the mux: MUXED(0)..MUXED(7)
// Tape polarity: whether the output goes UP or DOWN over the tape depends on
// the board and on dark-tape-on-light-floor vs the reverse. Hold tape under
// one channel, send ls, and see which way its raw number moved.
const byte  LINE_MAX_SENSORS = 8;
const byte  LINE_DEFAULT_COUNT = 0;
const AnalogSource LINE_DEFAULT_SOURCES[LINE_MAX_SENSORS] = {
  DIRECT(A2), DIRECT(A3), NO_SOURCE, NO_SOURCE, NO_SOURCE, NO_SOURCE, NO_SOURCE, NO_SOURCE };
const int8_t LINE_DEFAULT_OFFSET_MM[LINE_MAX_SENSORS] = { -12, 12, 0, 0, 0, 0, 0, 0 };
const bool  LINE_DEFAULT_TAPE_READS_HIGH = false;    // false = raw drops over the tape
const int   LINE_DEFAULT_SEEN_THRESHOLD = 250;       // normalised 0..1000; above = tape under that channel
const int   LINE_DEFAULT_SPEED_MM_PER_SEC = 150;
const float LINE_DEFAULT_STRAFE_GAIN = 3.0;          // mm/s of strafe per mm of line offset
const float LINE_DEFAULT_SPIN_GAIN = 0.02;           // rad/s of spin per mm of line offset
const unsigned int LINE_DEFAULT_LOST_STOP_MS = 600;  // no line for this long = stop
const unsigned long LINE_TELEMETRY_MS = 200;

// --- Light sensors: LDR + fixed resistor dividers ---
// Each LDR has a bearing: the direction it looks, degrees clockwise from
// straight ahead. Two at -45/+45 is the classic intro (turn toward the
// brighter side). Four at 0/90/180/270 (or the corners, 45/135/225/315) lets
// the vector sum point anywhere, and the holonomic drive just goes there.
// LDR to 5V and resistor to GND reads HIGHER in brighter light; the other
// way round reads lower.
const byte  LIGHT_MAX_SENSORS = 8;
const byte  LIGHT_DEFAULT_COUNT = 0;
const AnalogSource LIGHT_DEFAULT_SOURCES[LIGHT_MAX_SENSORS] = {
  DIRECT(A2), DIRECT(A3), NO_SOURCE, NO_SOURCE, NO_SOURCE, NO_SOURCE, NO_SOURCE, NO_SOURCE };
const int16_t LIGHT_DEFAULT_BEARING_DEG[LIGHT_MAX_SENSORS] = { -45, 45, 0, 0, 0, 0, 0, 0 };
const bool  LIGHT_DEFAULT_BRIGHT_READS_HIGH = true;
const int   LIGHT_DEFAULT_SEEN_THRESHOLD = 300;      // normalised; brightest LDR must beat this to move
const int   LIGHT_DEFAULT_ARRIVED_THRESHOLD = 950;   // brightest LDR above this = at the torch, hold still
const int   LIGHT_DEFAULT_SPEED_MM_PER_SEC = 150;
const bool  LIGHT_DEFAULT_FACE_THE_LIGHT = true;     // also spin so the nose points at the light
const float LIGHT_DEFAULT_SPIN_GAIN = 1.5;           // rad/s of spin per rad of light direction
const unsigned int LIGHT_DEFAULT_LOST_STOP_MS = 800;
const unsigned long LIGHT_CALIBRATE_MS = 5000;

// --- ADC layout: used instead of the above when ADS1115 boards answer ---
// The same sensors wired to ADS1115 inputs (see CONFIG - ADS1115). Default:
// all 8 line channels on the boards at 0x48 (D1-D4) and 0x49 (D5-D8), the
// two front-corner LDRs on 0x4A AIN0 / AIN1. Set ADC_LAYOUT_WHEN_FOUND false
// (or kau=0; at runtime) to always run the wired layout.
const bool  ADC_LAYOUT_WHEN_FOUND = true;
const byte  LINE_ADC_DEFAULT_COUNT = 8;
const AnalogSource LINE_ADC_DEFAULT_SOURCES[LINE_MAX_SENSORS] = {
  ADS(0, 0), ADS(0, 1), ADS(0, 2), ADS(0, 3), ADS(1, 0), ADS(1, 1), ADS(1, 2), ADS(1, 3) };
const int8_t LINE_ADC_DEFAULT_OFFSET_MM[LINE_MAX_SENSORS] = { -28, -20, -12, -4, 4, 12, 20, 28 };
const byte  LIGHT_ADC_DEFAULT_COUNT = 2;
const AnalogSource LIGHT_ADC_DEFAULT_SOURCES[LIGHT_MAX_SENSORS] = {
  ADS(2, 0), ADS(2, 1), NO_SOURCE, NO_SOURCE, NO_SOURCE, NO_SOURCE, NO_SOURCE, NO_SOURCE };
const int16_t LIGHT_ADC_DEFAULT_BEARING_DEG[LIGHT_MAX_SENSORS] = { -45, 45, 0, 0, 0, 0, 0, 0 };
const unsigned long LIGHT_TELEMETRY_MS = 200;

// ===========================================================================
// CONFIG - motor driver: L293D motor shield (two L293D + one 74HC595)
// ===========================================================================
// This is the Adafruit Motor Shield v1 layout, which every L293D clone
// copies: the four motor DIRECTIONS live in one 74HC595 shift register and
// the four SPEEDS are ordinary PWM pins. The pin numbers are fixed by the
// shield's traces, so nothing here is a placeholder any more.
//   shift register:  DATA 8   CLOCK 4   LATCH 12   ENABLE 7 (active LOW)
//   speed PWM:       M1 11    M2 3      M3 6       M4 5
//   servo headers:   9, 10        still free: 2, 13, A0-A5
// Serial (USB or a Bluetooth module on 0/1) is untouched by the shield.
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

// Which shield channel drives which corner, and whether that motor is wired
// backwards. Pre-filled by decoding the factory sketch's direction bytes
// (Move_Forward 39 = all four A bits; Right_Move 139 and Right_Rotate 149
// place M4 front-left, M3 front-right, M1 rear-left, M2 rear-right). Verify
// with t0..t3 (twitch channel M1..M4 forward for 300 ms) and fix anything
// that disagrees; the Mecanum Workshop app's calibrate tab writes the table.
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

// ===========================================================================
// DRIVER LAYER: the only code that touches hardware. Everything above this in
// the call graph works in wheel indices (FL FR RL RR) and signed power.
// ===========================================================================
int lastWheelPwm[4] = { 0, 0, 0, 0 };   // for telemetry / host tests
byte shiftRegisterState = 0;

void pushShiftRegister() {
  digitalWrite(SHIFT_LATCH_PIN, LOW);
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, MSBFIRST, shiftRegisterState);
  digitalWrite(SHIFT_LATCH_PIN, HIGH);
}

// PWM straight on the timer compare registers of the shield's four speed
// pins: M1 11 = OC2A, M2 3 = OC2B (Timer2), M3 6 = OC0A, M4 5 = OC0B
// (Timer0). The Arduino core's init() already runs both timers in PWM mode;
// analogWrite() does the same thing for every pin of every board and costs
// ~250 bytes more. Duty 0 disconnects the pin, which then sits at its LOW
// output level (a compare value of 0 would still leave a narrow spike on
// Timer0); 255 is a constant HIGH in both timers' modes.
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
// NOTE: M1/M2 (pins 11, 3) share Timer2 at ~490 Hz; M3/M4 (pins 6, 5) share
// Timer0 at ~980 Hz. Same duty, slightly different hum; PWM_DEADBAND is one
// number for all four, so pick it from the wheel that needs the most.
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
// I2C - IMU (BNO055 or MPU-6050) and ADS1115 inputs, all found at boot
// ===========================================================================
// BNO055 register map (page 0): CHIP_ID 0x00 (=0xA0), EUL_HEADING 0x1A/0x1B
// (1/16 degree, 0..360 increasing CLOCKWISE seen from above), CALIB_STAT
// 0x35, PAGE_ID 0x07, OPR_MODE 0x3D, PWR_MODE 0x3E.
// MPU-6050: PWR_MGMT_1 0x6B, CONFIG 0x1A, GYRO_CONFIG 0x1B, GYRO_ZOUT 0x47/48
// (big-endian, 65.5 counts per deg/s at +-500), WHO_AM_I 0x75.
// ADS1115: pointer 0x00 conversion, 0x01 config.
#if USE_IMU || USE_ADS1115 || USE_TOF
#define HAVE_I2C 1
#else
#define HAVE_I2C 0
#endif

const byte IMU_NONE = 0;
const byte IMU_BNO055 = 1;
const byte IMU_MPU6050 = 2;
byte imuKind = IMU_NONE;
byte imuAddress = 0;
bool imuPresent = false;
bool imuEnabled = false;
float imuUnwrappedRad = 0;       // running heading with no 0/360 jump
float imuLastRawRad = 0;
float mpuGyroBias = 0;           // raw gyro Z counts while still
unsigned long mpuLastMicros = 0;
byte adsFoundMask = 0;           // bit n set = an ADS1115 answered at 0x48 + n
bool tofPresent = false;
bool sonarPinsFree = false;      // no sensor uses A2/A3 (auditPins works it out)
bool i2cStarted = false;

#if HAVE_I2C
// I2C straight on the Uno's TWI hardware, polled. The Wire library costs about
// 1 KB of flash and 200 bytes of RAM in buffers the sketch never needs. Every
// wait gives up after ~20 ms, so a missing or stuck device cannot hang the
// robot; a transfer that fails just returns false (reads return 0).
bool twiWait() {
  // One byte time first (9 bits at 100 kHz = 90 us). On real hardware the
  // byte takes that long anyway, so this costs ~10 us; simavr 1.6 leaves
  // TWINT reading set and posts the new status only after the byte time.
  delayMicroseconds(100);
  unsigned int spins = 0;
  while (!(TWCR & _BV(TWINT))) {
    if (++spins == 0) {
      TWCR = 0;                         // reset the bus hardware
      TWCR = _BV(TWEN);
      return false;
    }
  }
  return true;
}

byte twiStatus() { return TWSR & 0xF8; }

void twiStop() {
  TWCR = _BV(TWINT) | _BV(TWSTO) | _BV(TWEN);
  unsigned int spins = 0;
  while ((TWCR & _BV(TWSTO)) && ++spins) {}
}

// START (or repeated START) plus the address byte; true if the device ACKed.
bool twiStart(byte address, bool reading) {
  TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
  if (!twiWait()) return false;
  TWDR = (address << 1) | (reading ? 1 : 0);
  TWCR = _BV(TWINT) | _BV(TWEN);
  if (!twiWait()) return false;
  // SLA+R / SLA+W acknowledged. 0x28 as well, like Wire: simavr 1.6 reports
  // an acknowledged SLA+W as a data byte; real hardware never gives it here.
  byte status = twiStatus();
  return reading ? status == 0x40 : (status == 0x18 || status == 0x28);
}

bool twiWrite(byte value) {
  TWDR = value;
  TWCR = _BV(TWINT) | _BV(TWEN);
  return twiWait() && twiStatus() == 0x28;
}

void i2cBegin() {
  if (i2cStarted) return;
  pinMode(A4, INPUT_PULLUP);
  pinMode(A5, INPUT_PULLUP);
  TWSR = 0;                                 // prescaler 1
  TWBR = ((F_CPU / 100000UL) - 16) / 2;     // 100 kHz
  TWCR = _BV(TWEN);
  i2cStarted = true;
}

// Releases A4/A5 (and their pull-ups) when nothing answered, so they can be
// read as plain analog pins.
void i2cEndIfUnused() {
  if (i2cStarted && !imuPresent && adsFoundMask == 0 && !tofPresent) {
    TWCR = 0;
    pinMode(A4, INPUT);
    pinMode(A5, INPUT);
    i2cStarted = false;
  }
}

bool i2cAnswers(byte address) {
  bool acked = twiStart(address, false);
  twiStop();
  return acked;
}

// Writes reg then count bytes from data. True if every byte was ACKed.
bool i2cWriteBytes(byte address, byte reg, const byte *data, byte count) {
  bool ok = twiStart(address, false) && twiWrite(reg);
  for (byte i = 0; ok && i < count; i++) ok = twiWrite(data[i]);
  twiStop();
  return ok;
}

// Reads count bytes starting at reg (repeated START in between).
bool i2cReadBytes(byte address, byte reg, byte *data, byte count) {
  bool ok = twiStart(address, false) && twiWrite(reg) && twiStart(address, true);
  for (byte i = 0; ok && i < count; i++) {
    TWCR = _BV(TWINT) | _BV(TWEN) | (i + 1 < count ? _BV(TWEA) : 0);   // NACK the last byte
    ok = twiWait();
    data[i] = TWDR;
  }
  twiStop();
  return ok;
}

byte i2cRead8(byte address, byte reg) {
  byte value = 0;
  return i2cReadBytes(address, reg, &value, 1) ? value : 0;
}

void i2cWrite8(byte address, byte reg, byte value) {
  i2cWriteBytes(address, reg, &value, 1);
}

// Two bytes from reg; ok false if the device did not deliver them.
int16_t i2cRead16(byte address, byte reg, bool bigEndian, bool &ok) {
  byte data[2];
  ok = i2cReadBytes(address, reg, data, 2);
  if (!ok) return 0;
  return bigEndian ? (int16_t)((data[0] << 8) | data[1]) : (int16_t)((data[1] << 8) | data[0]);
}
#endif

unsigned int accelPeak = 0;   // see bumped()

#if USE_IMU
// Raw BNO055 heading in radians, 0..2*PI, clockwise positive after IMU_HEADING_SIGN.
float bnoHeadingRad() {
  bool ok;
  int16_t sixteenths = i2cRead16(imuAddress, 0x1A, false, ok);
  if (!ok) return imuLastRawRad;
  float degrees = sixteenths / 16.0 * IMU_HEADING_SIGN;
  while (degrees < 0) degrees += 360;
  while (degrees >= 360) degrees -= 360;
  return degrees * PI / 180.0;
}

bool bnoBegin(byte address) {
  if (i2cRead8(address, 0x00) != 0xA0) return false;
  imuAddress = address;
  i2cWrite8(address, 0x3D, 0x00);  delay(25);   // CONFIG mode
  i2cWrite8(address, 0x07, 0x00);               // page 0
  i2cWrite8(address, 0x3E, 0x00);  delay(10);   // normal power
  i2cWrite8(address, 0x3F, 0x00);  delay(10);   // internal oscillator, no reset
  i2cWrite8(address, 0x3D, BNO055_MODE);  delay(20);
  imuLastRawRad = bnoHeadingRad();
  return true;
}

int16_t mpuGyroZ() {
  bool ok;
  int16_t value = i2cRead16(imuAddress, 0x47, true, ok);
  return ok ? value : (int16_t)mpuGyroBias;
}

bool mpuBegin(byte address) {
  byte who = i2cRead8(address, 0x75);
  if (who == 0x00 || who == 0xFF) return false;   // 0x68 on a real MPU-6050; clones differ
  imuAddress = address;
  i2cWrite8(address, 0x6B, 0x01);  delay(50);   // wake, clock from the X gyro
  i2cWrite8(address, 0x1A, 0x03);               // 44 Hz low-pass
  i2cWrite8(address, 0x1B, 0x08);  delay(50);   // +-500 deg/s (see MPU6050_COUNTS_PER_DPS)
  out.print(F("imu: MPU-6050 at 0x"));
  out.print(address, HEX);
  out.println(F(" - hold still"));
  long sum = 0;
  int samples = 0;
  unsigned long startedAt = millis();
  while (millis() - startedAt < MPU6050_BIAS_MS) {
    sum += mpuGyroZ();
    samples++;
    delay(5);
  }
  mpuGyroBias = samples ? (float)sum / samples : 0;
  mpuLastMicros = micros();
  return true;
}

// BNO055 first (it needs ~650 ms after power-on to answer), then MPU-6050.
bool imuBegin() {
  i2cBegin();
  while (millis() < 700) {}
  const byte bnoAddresses[2] = { 0x28, 0x29 };
  for (byte i = 0; i < 2; i++) {
    if (i2cAnswers(bnoAddresses[i]) && bnoBegin(bnoAddresses[i])) {
      imuKind = IMU_BNO055;
      return true;
    }
  }
  const byte mpuAddresses[2] = { 0x68, 0x69 };
  for (byte i = 0; i < 2; i++) {
    if (i2cAnswers(mpuAddresses[i]) && mpuBegin(mpuAddresses[i])) {
      imuKind = IMU_MPU6050;
      return true;
    }
  }
  return false;
}

byte imuCalibrationStatus() { return imuKind == IMU_BNO055 ? i2cRead8(imuAddress, 0x35) : 0; }

// The MPU-6050's accelerometer, X Y Z in counts (16384 = 1 g at power-on).
void readAccel(int a[3]) {
  byte d[6];
  i2cReadBytes(imuAddress, 0x3B, d, 6);
  for (byte i = 0; i < 3; i++) a[i] = d[2 * i] << 8 | d[2 * i + 1];
}

// Hit something (a jolt sideways or end-on) or picked up (a jolt up or down,
// or tipped): moves and hand driving stop rather than push on. Measured from
// how the board usually sits (a slow average, so any mounting works, upside
// down too), and only when two readings in a row say so: one rattle of the
// motors is not a bump.
// accelPeak: the biggest jolt since the move started, in % of the stop level
// (move: done jolt <n>, so the app can say how close it came).
int accelRest[3];
byte accelOver = 0;
bool bumped() {
  if (imuKind != IMU_MPU6050) return false;
  int a[3];
  readAccel(a);
  bool first = !accelRest[2];
  byte jolt = 0;
  for (byte i = 0; i < 3; i++) {
    if (first) accelRest[i] = a[i];
    int d = a[i] - accelRest[i];
    accelRest[i] += d >> 5;
    // % of the stop level (a 16-bit reading's 1 g is 16384 counts)
    unsigned int share = abs(d) / (i == 2 ? (int)(LIFT_G * 163.84) : (int)(BUMP_G * 163.84));
    if (share > 100) jolt = i + 1;
    if (share > accelPeak) accelPeak = share;
  }
  accelOver = jolt ? accelOver + 1 : 0;
  if (accelOver < 2) return false;
  out.print(jolt == 3 ? F("lift") : F("bump"));
  out.println(F(": stopped"));
  return true;
}
#else
float bnoHeadingRad() { return 0; }
bool imuBegin() { return false; }
byte imuCalibrationStatus() { return 0; }
void readAccel(int a[3]) { a[0] = a[1] = a[2] = 0; }
bool bumped() { return false; }
#endif

bool imuActive() { return imuPresent && imuEnabled; }

float wrapRadians(float angle) {
  while (angle > PI) angle -= 2 * PI;
  while (angle < -PI) angle += 2 * PI;
  return angle;
}

// Call before a move to start from "now" (the MPU-6050 integrates from here).
void imuRestart() {
  imuLastRawRad = bnoHeadingRad();
  mpuLastMicros = micros();
}

// Call often (loop() does, every 10 ms, so the heading is live even standing
// still). Keeps a heading that can exceed +-180 so a 360 spin per side still
// reads as 360, not 0. Tracked whenever an IMU is found; heading hold (in /
// if) only decides whether moves steer by it.
float imuUpdateHeading() {
  if (!imuPresent) return imuUnwrappedRad;
#if USE_IMU
  if (imuKind == IMU_MPU6050) {
    unsigned long now = micros();
    unsigned long elapsed = now - mpuLastMicros;
    mpuLastMicros = now;
    if (elapsed > 100000UL) return imuUnwrappedRad;   // a gap between moves: start fresh
    float degreesPerSecond = (mpuGyroZ() - mpuGyroBias) / MPU6050_COUNTS_PER_DPS;
    // The chip's Z points up, so counter-clockwise reads positive; ours is clockwise.
    imuUnwrappedRad -= IMU_HEADING_SIGN * degreesPerSecond * (PI / 180.0) * (elapsed / 1000000.0);
    return imuUnwrappedRad;
  }
  float raw = bnoHeadingRad();
  imuUnwrappedRad += wrapRadians(raw - imuLastRawRad);
  imuLastRawRad = raw;
#endif
  return imuUnwrappedRad;
}

// The heading for people: 0..360 degrees clockwise from where it pointed at
// power-on. (imuUnwrappedRad keeps counting past 360 for the motion engine.)
float headingDegrees() {
  float degrees = imuUnwrappedRad * 180.0 / PI;
  while (degrees >= 360) degrees -= 360;
  while (degrees < 0) degrees += 360;
  return degrees;
}

// I,<degrees>,<ms>: ih asks once (short, so the app can poll it), iw streams
// it. The robot's own millis() lets the app measure turn rates on the robot's
// clock, free of the Bluetooth link's uneven delays.
void printHeading() {
  out.print(F("I,"));
  out.print(headingDegrees(), 1);
  out.print(',');
  out.println(millis());
}

// "imu: on MPU-6050 0x68 heading ..." (prefix "K,imu," in the settings readout).
void printImuStatus(const __FlashStringHelper *prefix) {
  out.print(prefix);
#if !USE_IMU
  out.println(F("not compiled in"));
#else
  if (!imuPresent) {
    out.println(F("none found on A4/A5"));
    return;
  }
  out.print(imuEnabled ? F("on ") : F("present, off "));
  out.print(imuKind == IMU_BNO055 ? F("BNO055") : F("MPU-6050"));
  out.print(F(" 0x"));
  out.print(imuAddress, HEX);
  out.print(F(" heading "));
  if (imuKind == IMU_BNO055) {
    out.print(headingDegrees(), 1);
    out.print(F(" gyr"));                // gyro calibration 0..3 (trust it at 3)
    out.println((imuCalibrationStatus() >> 4) & 3);
  } else {
    out.print(headingDegrees(), 1);
    out.print(F(" gyro offset "));
    out.print(mpuGyroBias / MPU6050_COUNTS_PER_DPS, 2);
    out.println(F(" deg/s"));
  }
#endif
}

// ADS1115 single-shot read of one input, scaled to 0..1023 for 0..5 V.
int adsRead(byte chip, byte channel) {
#if USE_ADS1115
  if (!(adsFoundMask & (1 << chip)) || channel > 3) return 0;
  byte address = 0x48 + chip;
  // start, AINch vs GND, +-6.144 V, single shot, 860 samples/s, comparator off
  uint16_t config = 0x8000 | ((uint16_t)(4 + channel) << 12) | 0x0100 | 0x00E0 | 0x0003;
  byte configBytes[2] = { (byte)(config >> 8), (byte)(config & 0xFF) };
  if (!i2cWriteBytes(address, 0x01, configBytes, 2)) return 0;
  delayMicroseconds(1250);              // one conversion at 860 samples/s is 1.16 ms
  bool ok;
  int16_t counts = i2cRead16(address, 0x00, true, ok);
  if (!ok || counts < 0) return 0;
  long scaled = (long)counts * 1023 / 26667;   // 5 V = 26667 counts at +-6.144 V
  return (int)constrain(scaled, 0, 1023);
#else
  (void)chip;
  (void)channel;
  return 0;
#endif
}

void adsProbe() {
  adsFoundMask = 0;
#if USE_ADS1115
  i2cBegin();
  for (byte chip = 0; chip < 4; chip++) {
    if (i2cAnswers(0x48 + chip)) adsFoundMask |= 1 << chip;
  }
#endif
}

// ===========================================================================
// SENSOR SETTINGS - runtime line / light configuration, kept in EEPROM
// ===========================================================================
// There are two LAYOUTS (which sensor is on which input, and where it sits):
//   0 WIRED  direct Uno pins and / or mux channels
//   1 ADC    ADS1115 inputs (may mix in direct pins too)
// At boot the sketch probes I2C; if "ADC layout when found" is on and every
// ADS1115 the ADC layout uses answered, it runs the ADC layout, otherwise the
// wired one. Tuning (speeds, gains, thresholds, polarity) is shared.
//
// The CONFIG defaults are copied into `sensors` at boot, unless the EEPROM
// holds settings saved (kw) from a sketch with the SAME defaults. A hash of
// the defaults goes into the saved block, so re-flashing with edited
// defaults quietly drops the old app settings instead of fighting them.
// Calibration (per-channel raw min/max) is kept per layout, stamped with a
// hash of that layout's sources, and reused only while those match.
const byte WIRED_LAYOUT = 0;
const byte ADC_LAYOUT = 1;

struct LineLayout {
  byte count;
  AnalogSource source[LINE_MAX_SENSORS];
  int8_t offsetMm[LINE_MAX_SENSORS];
};

struct LightLayout {
  byte count;
  AnalogSource source[LIGHT_MAX_SENSORS];
  int16_t bearingDeg[LIGHT_MAX_SENSORS];
};

struct LineSettings {
  LineLayout layout[2];
  bool tapeReadsHigh;
  int16_t seenThreshold;
  int16_t speedMmPerSec;
  float strafeGain;
  float spinGain;
  uint16_t lostStopMs;
};

struct LightSettings {
  LightLayout layout[2];
  bool brightReadsHigh;
  int16_t seenThreshold;
  int16_t arrivedThreshold;
  int16_t speedMmPerSec;
  bool faceTheLight;
  float spinGain;
  uint16_t lostStopMs;
};

struct SensorSettings {
  LineSettings line;
  LightSettings light;
  bool adcLayoutWhenFound;
};

struct SavedSensorSettings {
  uint16_t magic;
  uint32_t defaultsHash;
  SensorSettings settings;
};

struct SavedSensorCalibration {
  uint16_t magic;
  uint32_t lineSourcesHash[2];
  uint32_t lightSourcesHash[2];
  bool lineCalibrated[2];
  bool lightCalibrated[2];
  int16_t lineMin[2][LINE_MAX_SENSORS];
  int16_t lineMax[2][LINE_MAX_SENSORS];
  int16_t lightMin[2][LIGHT_MAX_SENSORS];
  int16_t lightMax[2][LIGHT_MAX_SENSORS];
};

const uint16_t SETTINGS_MAGIC = 0x4D33;       // bump if the struct layout changes
const uint16_t CALIBRATION_MAGIC = 0x4D34;
const int SETTINGS_EEPROM_ADDRESS = 0;
const int CALIBRATION_EEPROM_ADDRESS = 400;

SensorSettings sensors;
SavedSensorCalibration calibration;           // raw min/max, before any polarity flip
byte activeLayout = WIRED_LAYOUT;

bool lineFollowing = false;
char sensorWatch = 0;          // 'l', 'b', 'r' or 'i': stream that sensor's readings, robot still
bool lineSeenSinceStart = false;
bool lightFollowing = false;
bool abortRequested = false;   // set by 'x' arriving during any blocking move (see checkForAbort)

LineLayout &lineLayout() { return sensors.line.layout[activeLayout]; }
LightLayout &lightLayout() { return sensors.light.layout[activeLayout]; }

// FNV-1a over raw bytes. Small, and good enough to spot a changed table.
__attribute__((noinline)) uint32_t hashBytes(const void *data, size_t length) {
  const byte *bytes = (const byte *)data;
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

__attribute__((noinline)) void sensorDefaults(SensorSettings &s) {
  s.line.layout[WIRED_LAYOUT].count = LINE_DEFAULT_COUNT;
  s.line.layout[ADC_LAYOUT].count = LINE_ADC_DEFAULT_COUNT;
  s.light.layout[WIRED_LAYOUT].count = LIGHT_DEFAULT_COUNT;
  s.light.layout[ADC_LAYOUT].count = LIGHT_ADC_DEFAULT_COUNT;
  for (byte i = 0; i < LINE_MAX_SENSORS; i++) {
    s.line.layout[WIRED_LAYOUT].source[i] = LINE_DEFAULT_SOURCES[i];
    s.line.layout[WIRED_LAYOUT].offsetMm[i] = LINE_DEFAULT_OFFSET_MM[i];
    s.line.layout[ADC_LAYOUT].source[i] = LINE_ADC_DEFAULT_SOURCES[i];
    s.line.layout[ADC_LAYOUT].offsetMm[i] = LINE_ADC_DEFAULT_OFFSET_MM[i];
  }
  s.line.tapeReadsHigh = LINE_DEFAULT_TAPE_READS_HIGH;
  s.line.seenThreshold = LINE_DEFAULT_SEEN_THRESHOLD;
  s.line.speedMmPerSec = LINE_DEFAULT_SPEED_MM_PER_SEC;
  s.line.strafeGain = LINE_DEFAULT_STRAFE_GAIN;
  s.line.spinGain = LINE_DEFAULT_SPIN_GAIN;
  s.line.lostStopMs = LINE_DEFAULT_LOST_STOP_MS;
  for (byte i = 0; i < LIGHT_MAX_SENSORS; i++) {
    s.light.layout[WIRED_LAYOUT].source[i] = LIGHT_DEFAULT_SOURCES[i];
    s.light.layout[WIRED_LAYOUT].bearingDeg[i] = LIGHT_DEFAULT_BEARING_DEG[i];
    s.light.layout[ADC_LAYOUT].source[i] = LIGHT_ADC_DEFAULT_SOURCES[i];
    s.light.layout[ADC_LAYOUT].bearingDeg[i] = LIGHT_ADC_DEFAULT_BEARING_DEG[i];
  }
  s.light.brightReadsHigh = LIGHT_DEFAULT_BRIGHT_READS_HIGH;
  s.light.seenThreshold = LIGHT_DEFAULT_SEEN_THRESHOLD;
  s.light.arrivedThreshold = LIGHT_DEFAULT_ARRIVED_THRESHOLD;
  s.light.speedMmPerSec = LIGHT_DEFAULT_SPEED_MM_PER_SEC;
  s.light.faceTheLight = LIGHT_DEFAULT_FACE_THE_LIGHT;
  s.light.spinGain = LIGHT_DEFAULT_SPIN_GAIN;
  s.light.lostStopMs = LIGHT_DEFAULT_LOST_STOP_MS;
  s.adcLayoutWhenFound = ADC_LAYOUT_WHEN_FOUND;
}

uint32_t defaultsHash() {
  SensorSettings defaults;
  sensorDefaults(defaults);
  return hashBytes(&defaults, sizeof(defaults));
}

__attribute__((noinline)) uint32_t lineSourcesHash(byte layout) {
  const LineLayout &l = sensors.line.layout[layout];
  return hashBytes(l.source, sizeof(AnalogSource) * l.count) ^ l.count;
}

__attribute__((noinline)) uint32_t lightSourcesHash(byte layout) {
  const LightLayout &l = sensors.light.layout[layout];
  return hashBytes(l.source, sizeof(AnalogSource) * l.count) ^ l.count;
}

__attribute__((noinline)) void lineResetCalibration(byte layout) {
  for (byte i = 0; i < LINE_MAX_SENSORS; i++) {
    calibration.lineMin[layout][i] = 1023;
    calibration.lineMax[layout][i] = 0;
  }
  calibration.lineCalibrated[layout] = false;
}

__attribute__((noinline)) void lightResetCalibration(byte layout) {
  for (byte i = 0; i < LIGHT_MAX_SENSORS; i++) {
    calibration.lightMin[layout][i] = 1023;
    calibration.lightMax[layout][i] = 0;
  }
  calibration.lightCalibrated[layout] = false;
}

void resetAllCalibration() {
  for (byte layout = 0; layout < 2; layout++) {
    lineResetCalibration(layout);
    lightResetCalibration(layout);
  }
}

void saveSensorSettings() {
  SavedSensorSettings saved;
  saved.magic = SETTINGS_MAGIC;
  saved.defaultsHash = defaultsHash();
  saved.settings = sensors;
  eeprom_update_block(&saved, (void *)SETTINGS_EEPROM_ADDRESS, sizeof(saved));   // rewrites only bytes that changed
}

void saveSensorCalibration() {
  calibration.magic = CALIBRATION_MAGIC;
  for (byte layout = 0; layout < 2; layout++) {
    calibration.lineSourcesHash[layout] = lineSourcesHash(layout);
    calibration.lightSourcesHash[layout] = lightSourcesHash(layout);
  }
  eeprom_update_block(&calibration, (void *)CALIBRATION_EEPROM_ADDRESS, sizeof(calibration));
}

// Motion calibration in EEPROM: a 2-byte mark, a layout number, then the
// numbers. Layout 1 (sketches 0.10.19-0.10.22) lacks the strafe start at the
// end: it is read, and the strafe start stays the forward one.
const int MOTION_EEPROM_ADDRESS = 920;
const uint16_t MOTION_MAGIC = 0x4D43;
const byte MOTION_LAYOUT = 4;   // 4 (0.10.72): the three slide shares after the acceleration
bool motionFromEeprom = false;
const float MOTION_DEFAULT_VALUES[M_COUNT] PROGMEM = MOTION_DEFAULTS;   // for kd: back to the code's numbers

#if CALIBRATION_IN_EEPROM
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
#endif

// K,cal,<the M_ numbers in order>,<lever mm>,<e = EEPROM, d = defaults, c = compiled in>
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
  out.println(!CALIBRATION_IN_EEPROM ? F(",c") : motionFromEeprom ? F(",e") : F(",d"));
}

// Loads saved settings if they belong to these defaults, else the defaults.
// Then each layout's calibration, if it was taken with today's wiring.
void loadSensorSettings(bool announce) {
  SavedSensorSettings saved;
  eeprom_read_block(&saved, (const void *)SETTINGS_EEPROM_ADDRESS, sizeof(saved));
  sensorDefaults(sensors);
  if (saved.magic == SETTINGS_MAGIC) {
    if (saved.defaultsHash == defaultsHash()) {
      sensors = saved.settings;
      if (announce) out.println(F("config: loaded"));
    } else if (announce) {
      out.println(F("config: sketch defaults changed"));
    }
  }
  SavedSensorCalibration stored;
  eeprom_read_block(&stored, (const void *)CALIBRATION_EEPROM_ADDRESS, sizeof(stored));
  resetAllCalibration();
  if (stored.magic != CALIBRATION_MAGIC) return;
  for (byte layout = 0; layout < 2; layout++) {
    if (stored.lineCalibrated[layout] && stored.lineSourcesHash[layout] == lineSourcesHash(layout)) {
      memcpy(calibration.lineMin[layout], stored.lineMin[layout], sizeof(stored.lineMin[layout]));
      memcpy(calibration.lineMax[layout], stored.lineMax[layout], sizeof(stored.lineMax[layout]));
      calibration.lineCalibrated[layout] = true;
    }
    if (stored.lightCalibrated[layout] && stored.lightSourcesHash[layout] == lightSourcesHash(layout)) {
      memcpy(calibration.lightMin[layout], stored.lightMin[layout], sizeof(stored.lightMin[layout]));
      memcpy(calibration.lightMax[layout], stored.lightMax[layout], sizeof(stored.lightMax[layout]));
      calibration.lightCalibrated[layout] = true;
    }
  }
}

// ---------------------------------------------------------------------------
// Analog sources: direct pin, multiplexer channel or ADS1115 input.
// ---------------------------------------------------------------------------
bool isAdsSource(const AnalogSource &source) {
  return source.pin >= 0xF0 && source.pin <= 0xF3;
}

// Bit n set = the layout reads the ADS1115 at 0x48 + n.
byte adsChipsUsed(byte layout) {
  byte mask = 0;
  const LineLayout &line = sensors.line.layout[layout];
  const LightLayout &light = sensors.light.layout[layout];
  for (byte i = 0; i < line.count; i++) {
    if (isAdsSource(line.source[i])) mask |= 1 << (line.source[i].pin & 0x03);
  }
  for (byte i = 0; i < light.count; i++) {
    if (isAdsSource(light.source[i])) mask |= 1 << (light.source[i].pin & 0x03);
  }
  return mask;
}

// Picks the layout from what answered on I2C. Called at boot, on kp, and
// when the "ADC layout when found" setting changes.
void chooseSensorLayout(bool announce) {
  byte needed = adsChipsUsed(ADC_LAYOUT);
  bool useAdc = sensors.adcLayoutWhenFound && adsFoundMask != 0 && (needed & ~adsFoundMask) == 0;
  byte layout = useAdc ? ADC_LAYOUT : WIRED_LAYOUT;
  if (layout != activeLayout) {
    lineFollowing = false;
    lightFollowing = false;
    stopAllWheels();
  }
  activeLayout = layout;
  if (!announce) return;
  out.print(F("adc: ADS1115 found:"));
  if (adsFoundMask == 0) out.print(F(" none"));
  for (byte chip = 0; chip < 4; chip++) {
    if (adsFoundMask & (1 << chip)) {
      out.print(F(" 0x"));
      out.print(0x48 + chip, HEX);
    }
  }
  out.print(F(" - using the "));
  out.print(activeLayout == ADC_LAYOUT ? F("ADC") : F("wired"));
  out.print(F(" layout"));
  if (!useAdc && sensors.adcLayoutWhenFound && adsFoundMask != 0) {
    out.print(F(" (board missing)"));
  } else if (!sensors.adcLayoutWhenFound && adsFoundMask != 0) {
    out.print(F(" (kau=0)"));
  }
  out.println();
}

bool muxInUse = false;          // the running layout reads a mux channel

// Drives the select pins only while the running layout needs them.
void initAnalogMux() {
#if USE_ANALOG_MUX
  bool used = false;
  for (byte i = 0; i < lineLayout().count; i++) {
    const AnalogSource &source = lineLayout().source[i];
    if (source.muxChannel >= 0 && source.pin == MUX_COMMON_PIN) used = true;
  }
  for (byte i = 0; i < lightLayout().count; i++) {
    const AnalogSource &source = lightLayout().source[i];
    if (source.muxChannel >= 0 && source.pin == MUX_COMMON_PIN) used = true;
  }
  muxInUse = used;
  for (byte i = 0; i < MUX_SELECT_COUNT; i++) {
    pinMode(MUX_SELECT_PINS[i], used ? OUTPUT : INPUT);
    digitalWrite(MUX_SELECT_PINS[i], LOW);
  }
#if USE_HEARTBEAT
  if (!used) pinMode(HEARTBEAT_PIN, OUTPUT);
#endif
#endif
}

int readAnalogSource(const AnalogSource &source) {
  if (source.pin == 0xFF) return 0;     // not wired
  if (isAdsSource(source)) return adsRead(source.pin & 0x03, source.muxChannel);
  byte pin = source.pin;
  if (source.muxChannel >= 0) {
#if USE_ANALOG_MUX
    for (byte i = 0; i < MUX_SELECT_COUNT; i++) {
      digitalWrite(MUX_SELECT_PINS[i], (source.muxChannel >> i) & 1);
    }
    delayMicroseconds(10);              // mux switch + RC settle
    pin = MUX_COMMON_PIN;
#else
    return 0;                           // mux not compiled in: this sensor reads dark
#endif
  }
  analogRead(pin);                      // first read settles the ADC's own mux
  return analogRead(pin);
}

// "a0".."a5" = a Uno analog pin, "m0".."m15" = a mux channel,
// "i00".."i33" = ADS1115 chip 0-3 (0x48-0x4B) input 0-3, "-" = none.
void printSource(const AnalogSource &source) {
  if (source.pin == 0xFF) {
    out.print('-');
  } else if (isAdsSource(source)) {
    out.print('i');
    out.print(source.pin & 0x03);
    out.print(source.muxChannel);
  } else if (source.muxChannel >= 0) {
    out.print('m');
    out.print(source.muxChannel);
  } else {
    out.print('a');
    out.print(source.pin - A0);
  }
}

bool parseSource(const char *text, AnalogSource &source) {
  if (text[0] == '-' && text[1] == 0) {
    source.pin = 0xFF;
    source.muxChannel = -1;
    return true;
  }
  if (text[1] < '0' || text[1] > '9') return false;
  if (text[2] != 0 && (text[2] < '0' || text[2] > '9' || text[3] != 0)) return false;
  if (text[0] == 'i') {
    if (text[2] == 0 || text[1] > '3' || text[2] > '3') return false;
    source.pin = 0xF0 | (text[1] - '0');
    source.muxChannel = text[2] - '0';
    return true;
  }
  int number = atoi(text + 1);
  if (text[0] == 'a' && number <= 5) {
    source.pin = A0 + number;
    source.muxChannel = -1;
    return true;
  }
#if USE_ANALOG_MUX
  if (text[0] == 'm' && number < MUX_CHANNELS) {
    source.pin = MUX_COMMON_PIN;
    source.muxChannel = number;
    return true;
  }
#endif
  return false;
}

// Normalise a raw reading into 0..1000 against a calibrated min/max, with a
// floor on the span so an uncalibrated or dead channel cannot explode.
int normaliseReading(int raw, int low, int high) {
  long span = (long)high - low;
  if (span < 8) span = 8;
  long value = (long)(raw - low) * 1000 / span;
  return (int)constrain(value, 0, 1000);
}

void printLayoutName() {
  out.print(activeLayout == ADC_LAYOUT ? F("ADC") : F("wired"));
}

bool sensorsConfigured(const __FlashStringHelper *name, byte count) {
  if (count > 0) return true;
  out.print(name);
  out.print(F(": none in the "));
  printLayoutName();
  out.println(F(" layout"));
  return false;
}

void noteMinMax(const int *raw, int16_t *low, int16_t *high, byte count) {
  for (byte i = 0; i < count; i++) {
    if (raw[i] < low[i]) low[i] = raw[i];
    if (raw[i] > high[i]) high[i] = raw[i];
  }
}

// "line raw: 620 930  min/max: 620/930 620/930"
void printRawReadings(const __FlashStringHelper *name, const int *raw, const int16_t *low,
                      const int16_t *high, byte count, bool calibrated, char calibrateLetter) {
  out.print(name);
  out.print(F(" raw:"));
  for (byte i = 0; i < count; i++) {
    out.print(' ');
    out.print(raw[i]);
  }
  out.print(F("  min/max:"));
  for (byte i = 0; i < count; i++) {
    out.print(' ');
    out.print(low[i]);
    out.print('/');
    out.print(high[i]);
  }
  if (!calibrated) {
    out.print(F("  (run "));
    out.print(calibrateLetter);
    out.print(F("c)"));
  }
  out.println();
}

// Watch mode: "LR,512,498" - the raw 0..1023 readings, before calibration.
void printRawTelemetry(const __FlashStringHelper *tag, const int *raw, byte count) {
  out.print(tag);
  for (byte i = 0; i < count; i++) {
    out.print(',');
    out.print(raw[i]);
  }
  out.println();
}

// After a calibration run: flag channels whose reading hardly moved.
void reportFlatChannels(const __FlashStringHelper *what, const int16_t *low, const int16_t *high,
                        byte count, int minimumSpan) {
  for (byte i = 0; i < count; i++) {
    if (high[i] - low[i] < minimumSpan) {
      out.print(what);
      out.print(i);
      out.println(F(" barely changed"));
    }
  }
}

// ===========================================================================
// LINE SENSOR
// ===========================================================================
// Raw reading per channel, per-channel min/max from calibration, a normalised
// 0..1000 "how much tape" value (after the polarity setting), and a
// weighted-centroid position in mm. Crosstalk between neighbouring emitters
// shows up as a raised floor on the raw readings; the per-channel minimum
// absorbs it, and the centroid treats what is left as blur.
int lineRaw[LINE_MAX_SENSORS];
int lineValue[LINE_MAX_SENSORS];      // normalised 0 (floor) .. 1000 (tape)
float linePositionMm = 0;
bool lineSeen = false;
unsigned long lineLastSeenAt = 0;
unsigned long lineLastTelemetryAt = 0;

bool lineConfigured() { return sensorsConfigured(F("line"), lineLayout().count); }

void lineReadRaw() {
  const LineLayout &layout = lineLayout();
  for (byte i = 0; i < layout.count; i++) {
    lineRaw[i] = readAnalogSource(layout.source[i]);
  }
}

void lineNoteCalibrationSample() {
  lineReadRaw();
  noteMinMax(lineRaw, calibration.lineMin[activeLayout], calibration.lineMax[activeLayout], lineLayout().count);
}

// Reads the array and updates lineValue[], lineSeen and linePositionMm.
// Uncalibrated, every channel is compared against the fixed 0..1023 range,
// which "works" but with a 1 V swing gives numbers around 0..200; run lc.
void lineUpdate() {
  lineReadRaw();
  const LineLayout &layout = lineLayout();
  float weighted = 0;
  long total = 0;
  bool calibrated = calibration.lineCalibrated[activeLayout];
  for (byte i = 0; i < layout.count; i++) {
    int value = normaliseReading(lineRaw[i], calibrated ? calibration.lineMin[activeLayout][i] : 0,
                                 calibrated ? calibration.lineMax[activeLayout][i] : 1023);
    lineValue[i] = sensors.line.tapeReadsHigh ? value : 1000 - value;
    if (lineValue[i] > sensors.line.seenThreshold) {
      weighted += (float)lineValue[i] * layout.offsetMm[i];
      total += lineValue[i];
    }
  }
  lineSeen = total > 0;
  if (lineSeen) {
    linePositionMm = weighted / total;
    lineLastSeenAt = millis();
  }
  // When the line is lost, keep the last position so the follower keeps
  // steering the way it was going, which is usually where the line went.
}

void printLineTelemetry() {
  out.print(F("L,"));
  out.print(lineSeen ? 1 : 0);
  out.print(',');
  out.print(linePositionMm, 1);
  for (byte i = 0; i < lineLayout().count; i++) {
    out.print(',');
    out.print(lineValue[i]);
  }
  out.println();
}

void printLineReading() {
  if (!lineConfigured()) return;
  lineUpdate();
  printRawReadings(F("line"), lineRaw, calibration.lineMin[activeLayout], calibration.lineMax[activeLayout],
                   lineLayout().count, calibration.lineCalibrated[activeLayout], 'l');
  printLineTelemetry();
}

// Calibrate by sweeping the array across the line: strafe right 60 mm,
// left 120 mm, right 60 mm, sampling min/max all the way. Start with the
// line under the middle of the array. Saved to EEPROM when it completes.
void lineCalibrate() {
  if (!lineConfigured()) return;
  lineResetCalibration(activeLayout);
  out.println(F("line calibrate: strafing"));
  float moves[3] = { 60, -120, 60 };
  for (byte leg = 0; leg < 3 && !abortRequested; leg++) {
    float speed = 80;
    unsigned long durationMs = (unsigned long)(fabs(moves[leg]) / speed * 1000);
    unsigned long startedAt = millis();
    driveBody(0, moves[leg] > 0 ? speed : -speed, 0);
    while (millis() - startedAt < durationMs) {
      if (checkForAbort()) break;
      lineNoteCalibrationSample();
      delay(5);
    }
    stopAllWheels();
  }
  if (abortRequested) {
    lineResetCalibration(activeLayout);
    out.println(F("line calibrate: aborted, not saved"));
    return;
  }
  calibration.lineCalibrated[activeLayout] = true;
  saveSensorCalibration();
  reportFlatChannels(F("line calibrate: channel "), calibration.lineMin[activeLayout],
                     calibration.lineMax[activeLayout], lineLayout().count, 30);
  out.println(F("line calibrate: saved"));
  printLineReading();
}

void lineBeginFollow() {
  if (!lineConfigured()) return;
  if (!calibration.lineCalibrated[activeLayout]) {
    out.println(F("line follow: uncalibrated"));
  }
  lineFollowing = true;
  lineSeenSinceStart = false;
  lineLastSeenAt = millis();
  out.println(F("line follow: start"));
}

// Called from loop() while following. Holonomic correction: strafe toward
// the line so the body stays over it, and turn toward it so the heading
// follows curves. Both terms are proportional to the offset in mm.
void lineFollowTick() {
  lineUpdate();
  if (lineSeen) lineSeenSinceStart = true;
  if (!lineSeen && millis() - lineLastSeenAt > sensors.line.lostStopMs) {
    lineFollowing = false;
    stopAllWheels();
    out.print(F("line follow: lost the line, stopped"));
    if (!lineSeenSinceStart) out.print(F(" - never saw it (lw to watch)"));
    out.println();
    return;
  }
  float strafe = sensors.line.strafeGain * linePositionMm;
  float spin = sensors.line.spinGain * linePositionMm;
  driveBody(sensors.line.speedMmPerSec, strafe, spin);
  if (millis() - lineLastTelemetryAt >= LINE_TELEMETRY_MS) {
    lineLastTelemetryAt = millis();
    printLineTelemetry();
  }
}

// ===========================================================================
// LIGHT SENSORS
// ===========================================================================
// Each LDR's normalised brightness (0 dark .. 1000 bright) weights a unit
// vector along its bearing. The sum points at the light in the body frame.
int lightRaw[LIGHT_MAX_SENSORS];
int lightValue[LIGHT_MAX_SENSORS];
float lightDirectionRad = 0;
int lightBrightest = 0;
unsigned long lightLastSeenAt = 0;
unsigned long lightLastTelemetryAt = 0;

bool lightConfigured() { return sensorsConfigured(F("light"), lightLayout().count); }

void lightReadRaw() {
  const LightLayout &layout = lightLayout();
  for (byte i = 0; i < layout.count; i++) {
    lightRaw[i] = readAnalogSource(layout.source[i]);
  }
}

void lightUpdate() {
  lightReadRaw();
  const LightLayout &layout = lightLayout();
  float x = 0, y = 0;
  long total = 0;
  bool calibrated = calibration.lightCalibrated[activeLayout];
  lightBrightest = 0;
  for (byte i = 0; i < layout.count; i++) {
    int value = normaliseReading(lightRaw[i], calibrated ? calibration.lightMin[activeLayout][i] : 0,
                                 calibrated ? calibration.lightMax[activeLayout][i] : 1023);
    lightValue[i] = sensors.light.brightReadsHigh ? value : 1000 - value;
    if (lightValue[i] > lightBrightest) lightBrightest = lightValue[i];
    float bearing = layout.bearingDeg[i] * PI / 180.0;
    x += lightValue[i] * cos(bearing);
    y += lightValue[i] * sin(bearing);
    total += lightValue[i];
  }
  if (total > 0) {
    lightDirectionRad = atan2(y, x);
  }
  if (lightBrightest > sensors.light.seenThreshold) {
    lightLastSeenAt = millis();
  }
}

void printLightTelemetry() {
  out.print(F("B,"));
  out.print(lightDirectionRad * 180.0 / PI, 0);
  out.print(',');
  out.print(lightBrightest);
  for (byte i = 0; i < lightLayout().count; i++) {
    out.print(',');
    out.print(lightValue[i]);
  }
  out.println();
}

void printLightReading() {
  if (!lightConfigured()) return;
  lightUpdate();
  printRawReadings(F("light"), lightRaw, calibration.lightMin[activeLayout], calibration.lightMax[activeLayout],
                   lightLayout().count, calibration.lightCalibrated[activeLayout], 'b');
  printLightTelemetry();
}

// Five-second window: cover each LDR with a thumb, then shine the torch on
// each. Whatever extremes it sees become that channel's dark and bright.
// Saved to EEPROM when it completes.
void lightCalibrate() {
  if (!lightConfigured()) return;
  lightResetCalibration(activeLayout);
  out.print(F("light calibrate: "));
  out.print(LIGHT_CALIBRATE_MS / 1000);
  out.println(F(" s: cover, then light"));
  int16_t *low = calibration.lightMin[activeLayout];
  int16_t *high = calibration.lightMax[activeLayout];
  unsigned long startedAt = millis();
  while (millis() - startedAt < LIGHT_CALIBRATE_MS) {
    if (checkForAbort()) break;
    lightReadRaw();
    noteMinMax(lightRaw, low, high, lightLayout().count);
    delay(10);
  }
  if (abortRequested) {
    lightResetCalibration(activeLayout);
    out.println(F("light calibrate: aborted, not saved"));
    return;
  }
  calibration.lightCalibrated[activeLayout] = true;
  saveSensorCalibration();
  reportFlatChannels(F("light calibrate: LDR "), low, high, lightLayout().count, 50);
  out.println(F("light calibrate: saved"));
  printLightReading();
}

void lightBeginFollow() {
  if (!lightConfigured()) return;
  if (!calibration.lightCalibrated[activeLayout]) {
    out.println(F("light seek: uncalibrated"));
  }
  lightFollowing = true;
  lightLastSeenAt = millis();
  out.println(F("light seek: start"));
}

// Called from loop() while seeking. Drive along the light vector, hold still
// once the brightest LDR saturates (we are at the torch), stop if nothing
// bright has been seen for a while.
void lightFollowTick() {
  lightUpdate();
  if (millis() - lightLastSeenAt > sensors.light.lostStopMs) {
    lightFollowing = false;
    stopAllWheels();
    out.println(F("light seek: no light, stopped"));
    return;
  }
  float speed = lightBrightest >= sensors.light.arrivedThreshold ? 0 : sensors.light.speedMmPerSec;
  float spin = sensors.light.faceTheLight ? sensors.light.spinGain * lightDirectionRad : 0;
  if (lightBrightest <= sensors.light.seenThreshold) {
    speed = 0;
    spin = 0;
  }
  driveBody(speed * cos(lightDirectionRad), speed * sin(lightDirectionRad), spin);
  if (millis() - lightLastTelemetryAt >= LIGHT_TELEMETRY_MS) {
    lightLastTelemetryAt = millis();
    printLightTelemetry();
  }
}

// ===========================================================================
// k COMMANDS - read and change the sensor settings at runtime
// ===========================================================================
//   k?;            dump every setting as K,<key>,<value> lines, then K,end
//   k<key>=<v>;    change one setting (live, not saved); echoes K,<key>,<v>
//   kw;            save settings (and calibration) to EEPROM
//   ku;            undo: reload what is saved
//   kr;            back to the sketch's CONFIG defaults (kw to keep them)
//   kp;            probe I2C again for ADS1115 boards and pick the layout
// A newline also ends a k command. Keys (i = channel 0..7). Layout keys are
// for the WIRED layout; put an e after the l or b for the ADC layout
// (len, lep0, leo0, ben, bep0, beb0).
//   line   ln count  lp<i> source  lo<i> offset mm  lh tape reads high 0/1
//          lt seen threshold  lv speed mm/s  lg strafe gain  lk spin gain
//          ll lost-line stop ms
//   light  bn count  bp<i> source  bb<i> bearing deg  bh bright reads high 0/1
//          bt seen threshold  ba arrived threshold  bv speed mm/s
//          bf face the light 0/1  bk spin gain  bl lost-light stop ms
//   au     1 = run the ADC layout when its ADS1115 boards answer at boot
// Sources: a0..a5, m0..m7 (mux; m0..m15 with 16 channels), i00..i33
// (ADS1115 chip 0-3 = 0x48-0x4B, input 0-3), or - for none.

struct SettingKey {
  char group;     // l line, b light, a global
  byte layout;    // WIRED_LAYOUT or ADC_LAYOUT (layout fields only)
  char field;
  int8_t index;   // channel, or -1
};

bool isLayoutField(char group, char field) {
  return field == 'n' || field == 'p' || (group == 'l' && field == 'o') || (group == 'b' && field == 'b');
}

bool fieldTakesIndex(char group, char field) {
  return field == 'p' || (group == 'l' && field == 'o') || (group == 'b' && field == 'b');
}

bool parseKey(const char *text, SettingKey &key) {
  key.group = text[0];
  key.layout = WIRED_LAYOUT;
  key.index = -1;
  if (key.group == 'a') {
    key.field = text[1];
    return key.field == 'u' && text[2] == 0;
  }
  if (key.group != 'l' && key.group != 'b') return false;
  const char *rest = text + 1;
  if (rest[0] == 'e' && rest[1] != 0) {
    key.layout = ADC_LAYOUT;
    rest++;
  }
  key.field = rest[0];
  if (key.field == 0) return false;
  if (rest[1] != 0) {
    if (rest[1] < '0' || rest[1] > '7' || rest[2] != 0) return false;
    key.index = rest[1] - '0';
  }
  if (fieldTakesIndex(key.group, key.field) != (key.index >= 0)) return false;
  if (key.layout == ADC_LAYOUT && !isLayoutField(key.group, key.field)) return false;
  return true;
}

bool isNumberText(const char *text) {
  if (*text == '-') text++;
  bool digits = false;
  for (; *text; text++) {
    if (*text >= '0' && *text <= '9') digits = true;
    else if (*text != '.') return false;
  }
  return digits;
}

// "-12", "0.02": digits, one optional minus, dots. Much smaller than atof().
float parseDecimal(const char *text) {
  bool negative = *text == '-';
  if (negative) text++;
  float value = 0;
  float scale = 0;
  for (; *text; text++) {
    if (*text == '.') {
      scale = 1;
      continue;
    }
    value = value * 10 + (*text - '0');
    if (scale) scale *= 10;
  }
  if (scale) value /= scale;
  return negative ? -value : value;
}

void layoutChanged(char group, byte layout) {
  if (group == 'l') lineResetCalibration(layout);
  else lightResetCalibration(layout);
  if (layout == activeLayout) {
    if (group == 'l') lineFollowing = false;
    else lightFollowing = false;
  }
}

// Returns false (changing nothing) for an out-of-range value.
bool applySetting(const SettingKey &key, const char *value) {
  if (key.field == 'p') {
    AnalogSource source;
    if (!parseSource(value, source)) return false;
    if (key.group == 'l') sensors.line.layout[key.layout].source[key.index] = source;
    else sensors.light.layout[key.layout].source[key.index] = source;
    layoutChanged(key.group, key.layout);
    return true;
  }
  if (!isNumberText(value)) return false;
  float real = parseDecimal(value);
  long number = (long)real;
  if (key.group == 'a') {
    sensors.adcLayoutWhenFound = number != 0;
    chooseSensorLayout(true);
    return true;
  }
  if (key.group == 'l') {
    LineSettings &s = sensors.line;
    switch (key.field) {
      case 'n': if (number < 0 || number > LINE_MAX_SENSORS) return false;
                s.layout[key.layout].count = number; layoutChanged('l', key.layout); break;
      case 'o': if (number < -100 || number > 100) return false; s.layout[key.layout].offsetMm[key.index] = number; break;
      case 'h': s.tapeReadsHigh = number != 0; break;
      case 't': if (number < 1 || number > 999) return false; s.seenThreshold = number; break;
      case 'v': if (number < 0 || number > MAX_WHEEL_MM_PER_SEC) return false; s.speedMmPerSec = number; break;
      case 'g': if (real < 0 || real > 50) return false; s.strafeGain = real; break;
      case 'k': if (real < 0 || real > 1) return false; s.spinGain = real; break;
      case 'l': if (number < 50 || number > 30000) return false; s.lostStopMs = number; break;
      default: return false;
    }
  } else {
    LightSettings &s = sensors.light;
    switch (key.field) {
      case 'n': if (number < 0 || number > LIGHT_MAX_SENSORS) return false;
                s.layout[key.layout].count = number; layoutChanged('b', key.layout); break;
      case 'b': if (number < -360 || number > 360) return false; s.layout[key.layout].bearingDeg[key.index] = number; break;
      case 'h': s.brightReadsHigh = number != 0; break;
      case 't': if (number < 1 || number > 999) return false; s.seenThreshold = number; break;
      case 'a': if (number < 1 || number > 1001) return false; s.arrivedThreshold = number; break;
      case 'v': if (number < 0 || number > MAX_WHEEL_MM_PER_SEC) return false; s.speedMmPerSec = number; break;
      case 'f': s.faceTheLight = number != 0; break;
      case 'k': if (real < 0 || real > 20) return false; s.spinGain = real; break;
      case 'l': if (number < 50 || number > 30000) return false; s.lostStopMs = number; break;
      default: return false;
    }
  }
  return true;
}

void printSettingLine(const SettingKey &key) {
  out.print(F("K,"));
  out.print(key.group);
  if (key.layout == ADC_LAYOUT) out.print('e');
  out.print(key.field);
  if (key.index >= 0) out.print(key.index);
  out.print(',');
  if (key.group == 'a') {
    out.print(sensors.adcLayoutWhenFound ? 1 : 0);
  } else if (key.group == 'l') {
    const LineSettings &s = sensors.line;
    const LineLayout &l = s.layout[key.layout];
    switch (key.field) {
      case 'n': out.print(l.count); break;
      case 'p': printSource(l.source[key.index]); break;
      case 'o': out.print(l.offsetMm[key.index]); break;
      case 'h': out.print(s.tapeReadsHigh ? 1 : 0); break;
      case 't': out.print(s.seenThreshold); break;
      case 'v': out.print(s.speedMmPerSec); break;
      case 'g': out.print(s.strafeGain, 3); break;
      case 'k': out.print(s.spinGain, 4); break;
      case 'l': out.print(s.lostStopMs); break;
    }
  } else {
    const LightSettings &s = sensors.light;
    const LightLayout &l = s.layout[key.layout];
    switch (key.field) {
      case 'n': out.print(l.count); break;
      case 'p': printSource(l.source[key.index]); break;
      case 'b': out.print(l.bearingDeg[key.index]); break;
      case 'h': out.print(s.brightReadsHigh ? 1 : 0); break;
      case 't': out.print(s.seenThreshold); break;
      case 'a': out.print(s.arrivedThreshold); break;
      case 'v': out.print(s.speedMmPerSec); break;
      case 'f': out.print(s.faceTheLight ? 1 : 0); break;
      case 'k': out.print(s.spinGain, 3); break;
      case 'l': out.print(s.lostStopMs); break;
    }
  }
  out.println();
}

void printSettingFields(char group, const char *fields) {
  SettingKey key = { group, WIRED_LAYOUT, 0, -1 };
  for (byte f = 0; fields[f]; f++) {
    key.field = fields[f];
    printSettingLine(key);
  }
}

void printLayoutSettings(char group, byte layout) {
  SettingKey key = { group, layout, 'n', -1 };
  printSettingLine(key);
  for (int8_t i = 0; i < 8; i++) {
    key.index = i;
    key.field = 'p';
    printSettingLine(key);
    key.field = group == 'l' ? 'o' : 'b';
    printSettingLine(key);
  }
}

void printSensorConfig() {
  printTakenPins();
  out.print(F("K,mux,"));
  out.println(USE_ANALOG_MUX ? MUX_CHANNELS : 0);
  out.print(F("K,adc,"));
  out.println(adsFoundMask);
  out.print(F("K,layout,"));
  out.println(activeLayout == ADC_LAYOUT ? F("adc") : F("wired"));
  out.println(F("K,fw," FIRMWARE_VERSION));
  out.println(F("K,role," SKETCH_NAME));
  printImuStatus(F("K,imu,"));
  printMotion();
  out.print(F("K,tof,"));
  out.println(tofPresent ? 1 : 0);
  out.print(F("K,sonar,"));
  out.println(sonarPinsFree ? 1 : 0);
  out.print(F("K,demos,"));
  printDemoKeys();
  printSettingFields('a', "u");
  printSettingFields('l', "htvgkl");
  printSettingFields('b', "htavfkl");
  for (byte layout = 0; layout < 2; layout++) {
    printLayoutSettings('l', layout);
    printLayoutSettings('b', layout);
  }
  out.print(F("K,lcal,"));
  out.println(calibration.lineCalibrated[activeLayout] ? 1 : 0);
  out.print(F("K,bcal,"));
  out.println(calibration.lightCalibrated[activeLayout] ? 1 : 0);
  out.println(F("K,end"));
}

void handleConfigText(char *text) {
  if (strcmp(text, "?") == 0) {
    printSensorConfig();
    return;
  }
  char *equals = strchr(text, '=');
  if (equals != NULL) *equals = 0;
#if CALIBRATION_IN_EEPROM
  if (text[0] == 'm') {   // km<i>=<value>; one motion number, live (kw keeps it)
    byte index = parseDecimal(text + 1);
    if (equals && index < M_COUNT && isNumberText(equals + 1)) motion[index] = parseDecimal(equals + 1);
    printMotion();
    return;
  }
  if (strcmp(text, "d") == 0) {   // kd; back to the code's numbers, and forget the saved ones
    memcpy_P(motion, MOTION_DEFAULT_VALUES, sizeof(motion));
    eeprom_update_word((uint16_t *)MOTION_EEPROM_ADDRESS, 0);
    motionFromEeprom = false;
    printMotion();
    return;
  }
#endif
  if (strcmp(text, "w") == 0) {
    saveSensorSettings();
    saveSensorCalibration();
#if CALIBRATION_IN_EEPROM
    saveMotion();
#endif
    out.println(F("config: saved"));
    return;
  }
  if (strcmp(text, "p") == 0) {
    adsProbe();
#if HAVE_I2C
    i2cEndIfUnused();
#endif
    chooseSensorLayout(true);
    auditPins();
    printSensorConfig();
    return;
  }
  if (strcmp(text, "u") == 0 || strcmp(text, "r") == 0) {
    lineFollowing = false;
    lightFollowing = false;
    stopAllWheels();
    if (text[0] == 'u') {
      loadSensorSettings(false);
#if CALIBRATION_IN_EEPROM
      loadMotion();
#endif
      out.println(F("config: back to the saved settings"));
    } else {
      sensorDefaults(sensors);
      resetAllCalibration();
      out.println(F("config: sketch defaults (kw saves them)"));
    }
    chooseSensorLayout(false);
    auditPins();
    printSensorConfig();
    return;
  }
  SettingKey key;
  if (equals == NULL || equals == text || !parseKey(text, key)) {
    out.print(F("config: bad "));
    out.println(text);
    return;
  }
  const char *value = equals + 1;
  if (!applySetting(key, value)) {
    out.print(F("config: bad "));
    out.print(text);
    out.print('=');
    out.println(value);
    return;
  }
  // Warnings first, echo last: the app sends its next setting as soon as the
  // echo arrives, and anything we still sent after it would make the
  // Bluetooth port (SoftwareSerial) drop that setting.
  if (key.field == 'p' || key.field == 'n' || key.group == 'a') auditPins();
  printSettingLine(key);
}

// ===========================================================================
// KINEMATICS - body velocity -> four wheel speeds (mm/s at the wheel rim)
// ===========================================================================
float rotationLeverMm() {
  return (WHEELBASE_MM + TRACK_MM) / 2.0;
}

// Standard X-roller mecanum inverse kinematics, in THIS sketch's frame
// (+X forward, +Y right, clockwise positive). Efficiency factors boost the
// strafe and spin components so the measured motion matches the request.
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

// ===========================================================================
// TIME-OF-FLIGHT - VL53L0X at 0x29
// ===========================================================================
// ST's init sequence, as ST's API and Pololu's library run it, cut to what a
// robot needs: data init, reference SPADs, ST's default tuning, VHV and
// phase calibration, then back-to-back continuous ranging (~30 ms a
// reading, default timing budget). tofPoll() picks up each new reading.
const byte TOF_ADDRESS = 0x29;
int tofRangeMm = -1;              // last reading; -1 = nothing in range yet
unsigned long tofReadAt = 0;      // millis() of that reading
unsigned long tofPolledAt = 0;

#if USE_TOF
byte tofStopVariable = 0;

// ST's DefaultTuningSettings: register, value pairs (0xFF selects the page).
const byte TOF_TUNING[] PROGMEM = {
  0xFF, 0x01, 0x00, 0x00, 0xFF, 0x00, 0x09, 0x00, 0x10, 0x00, 0x11, 0x00,
  0x24, 0x01, 0x25, 0xFF, 0x75, 0x00, 0xFF, 0x01, 0x4E, 0x2C, 0x48, 0x00,
  0x30, 0x20, 0xFF, 0x00, 0x30, 0x09, 0x54, 0x00, 0x31, 0x04, 0x32, 0x03,
  0x40, 0x83, 0x46, 0x25, 0x60, 0x00, 0x27, 0x00, 0x50, 0x06, 0x51, 0x00,
  0x52, 0x96, 0x56, 0x08, 0x57, 0x30, 0x61, 0x00, 0x62, 0x00, 0x64, 0x00,
  0x65, 0x00, 0x66, 0xA0, 0xFF, 0x01, 0x22, 0x32, 0x47, 0x14, 0x49, 0xFF,
  0x4A, 0x00, 0xFF, 0x00, 0x7A, 0x0A, 0x7B, 0x00, 0x78, 0x21, 0xFF, 0x01,
  0x23, 0x34, 0x42, 0x00, 0x44, 0xFF, 0x45, 0x26, 0x46, 0x05, 0x40, 0x40,
  0x0E, 0x06, 0x20, 0x1A, 0x43, 0x40, 0xFF, 0x00, 0x34, 0x03, 0x35, 0x44,
  0xFF, 0x01, 0x31, 0x04, 0x4B, 0x09, 0x4C, 0x05, 0x4D, 0x04, 0xFF, 0x00,
  0x44, 0x00, 0x45, 0x20, 0x47, 0x08, 0x48, 0x28, 0x67, 0x00, 0x70, 0x04,
  0x71, 0x01, 0x72, 0xFE, 0x76, 0x00, 0x77, 0x00, 0xFF, 0x01, 0x0D, 0x01,
  0xFF, 0x00, 0x80, 0x01, 0x01, 0xF8, 0xFF, 0x01, 0x8E, 0x01, 0x00, 0x01,
  0xFF, 0x00, 0x80, 0x00,
};

void tofWrite(byte reg, byte value) { i2cWrite8(TOF_ADDRESS, reg, value); }
byte tofRead(byte reg) { return i2cRead8(TOF_ADDRESS, reg); }

// Waits up to 100 ms for (register & mask) to become nonzero.
bool tofWaitFor(byte reg, byte mask) {
  unsigned long startedAt = millis();
  while (!(tofRead(reg) & mask)) {
    if (millis() - startedAt > 100) return false;
  }
  return true;
}

// The 0x80 / 0xFF / 0x00 dance that opens (or closes) ST's private page.
void tofPrivatePage(bool open) {
  tofWrite(0x80, open ? 0x01 : 0x00);
  tofWrite(0xFF, open ? 0x01 : 0x00);
  tofWrite(0x00, open ? 0x00 : 0x01);
}

bool tofCalibrate(byte vhvInit) {
  tofWrite(0x00, 0x01 | vhvInit);          // SYSRANGE_START
  if (!tofWaitFor(0x13, 0x07)) return false;
  tofWrite(0x0B, 0x01);                    // clear the interrupt
  tofWrite(0x00, 0x00);
  return true;
}

bool tofBegin() {
  i2cBegin();
  if (tofRead(0xC0) != 0xEE) return false;  // IDENTIFICATION_MODEL_ID
  tofWrite(0x89, tofRead(0x89) | 0x01);     // 2.8 V I/O
  tofWrite(0x88, 0x00);                     // I2C standard mode
  tofPrivatePage(true);
  tofStopVariable = tofRead(0x91);
  tofPrivatePage(false);
  tofWrite(0x60, tofRead(0x60) | 0x12);     // no MSRC / pre-range signal limit checks
  byte limit[2] = { 0x00, 0x20 };           // final range signal rate limit 0.25 MCPS (9.7 fixed)
  i2cWriteBytes(TOF_ADDRESS, 0x44, limit, 2);
  tofWrite(0x01, 0xFF);                     // SYSTEM_SEQUENCE_CONFIG

  // Reference SPAD count and type from the chip's NVM.
  tofPrivatePage(true);
  tofWrite(0xFF, 0x06);
  tofWrite(0x83, tofRead(0x83) | 0x04);
  tofWrite(0xFF, 0x07);
  tofWrite(0x81, 0x01);
  tofWrite(0x80, 0x01);
  tofWrite(0x94, 0x6B);
  tofWrite(0x83, 0x00);
  if (!tofWaitFor(0x83, 0xFF)) return false;
  tofWrite(0x83, 0x01);
  byte spadInfo = tofRead(0x92);
  tofWrite(0x81, 0x00);
  tofWrite(0xFF, 0x06);
  tofWrite(0x83, tofRead(0x83) & ~0x04);
  tofWrite(0xFF, 0x01);
  tofWrite(0x00, 0x01);
  tofWrite(0xFF, 0x00);
  tofWrite(0x80, 0x00);
  byte spadCount = spadInfo & 0x7F;
  byte firstSpad = (spadInfo & 0x80) ? 12 : 0;   // aperture SPADs start at 12
  byte spadMap[6];
  i2cReadBytes(TOF_ADDRESS, 0xB0, spadMap, 6);
  tofWrite(0xFF, 0x01);
  tofWrite(0x4F, 0x00);
  tofWrite(0x4E, 0x2C);
  tofWrite(0xFF, 0x00);
  tofWrite(0xB6, 0xB4);
  byte enabled = 0;
  for (byte i = 0; i < 48; i++) {
    byte bit = 1 << (i % 8);
    if (i < firstSpad || enabled == spadCount) spadMap[i / 8] &= ~bit;
    else if (spadMap[i / 8] & bit) enabled++;
  }
  i2cWriteBytes(TOF_ADDRESS, 0xB0, spadMap, 6);

  for (byte i = 0; i < sizeof(TOF_TUNING); i += 2) {
    tofWrite(pgm_read_byte(&TOF_TUNING[i]), pgm_read_byte(&TOF_TUNING[i + 1]));
  }
  tofWrite(0x0A, 0x04);                     // interrupt on new sample ready
  tofWrite(0x84, tofRead(0x84) & ~0x10);    // active low
  tofWrite(0x0B, 0x01);
  tofWrite(0x01, 0x01);                     // VHV calibration
  if (!tofCalibrate(0x40)) return false;
  tofWrite(0x01, 0x02);                     // phase calibration
  if (!tofCalibrate(0x00)) return false;
  tofWrite(0x01, 0xE8);                     // final sequence: DSS, pre-range, final range

  tofPrivatePage(true);                     // start continuous back-to-back ranging
  tofWrite(0x91, tofStopVariable);
  tofPrivatePage(false);
  tofWrite(0x00, 0x02);
  return true;
}

// Picks up a finished reading, at most every 10 ms. 8190+ = nothing in range.
void tofPoll() {
  if (!tofPresent || millis() - tofPolledAt < 10) return;
  tofPolledAt = millis();
  if (!(tofRead(0x13) & 0x07)) return;
  bool ok;
  int16_t range = i2cRead16(TOF_ADDRESS, 0x1E, true, ok);
  tofWrite(0x0B, 0x01);
  if (!ok) return;
  tofRangeMm = (range <= 0 || range >= 8190) ? -1 : range;
  tofReadAt = millis();
}
#else
void tofPoll() {}
#endif

// R,<mm>  (-1 = nothing in range)
void printRange() {
  if (!tofPresent) {
    out.println(F("tof: none found"));
    return;
  }
  out.print(F("R,"));
  out.println(tofRangeMm);
}

// U,<mm> (-1 = no echo). Trig goes back to an input afterwards, so a sensor
// set up on A3 later still reads. quiet: say nothing when the pins are taken.
void printSonar(bool quiet) {
#if USE_SONAR
  if (!sonarPinsFree) {
    if (!quiet) out.println(F("sonar: A2/A3 are wired to sensors"));
    return;
  }
  pinMode(SONAR_TRIG_PIN, OUTPUT);
  digitalWrite(SONAR_TRIG_PIN, HIGH);                // a 10 us pulse starts a ping
  delayMicroseconds(10);
  digitalWrite(SONAR_TRIG_PIN, LOW);
  pinMode(SONAR_TRIG_PIN, INPUT);
  // Echo rises within ~0.5 ms and stays high for the round trip.
  long mm = -1;
  volatile uint8_t *echoPort = portInputRegister(digitalPinToPort(SONAR_ECHO_PIN));
  byte echoMask = digitalPinToBitMask(SONAR_ECHO_PIN);
  unsigned long startedAt = micros();
  while (!(*echoPort & echoMask)) {
    if (micros() - startedAt > 2000) goto report;
  }
  startedAt = micros();
  while (*echoPort & echoMask) {
    if (micros() - startedAt > SONAR_TIMEOUT_US) goto report;
  }
  mm = (micros() - startedAt) * 343UL / 2000;        // 343 m/s, there and back
report:
  out.print(F("U,"));
  out.println(mm);
#else
  if (!quiet) out.println(F("sonar: not compiled in"));
#endif
}

// True (after saying so) when a forward move would run into what the laser sees.
bool tofWallAhead() {
  static bool said = false;            // once per wall, not on every stick refresh
  if (TOF_STOP_MM <= 0 || !tofPresent || tofRangeMm < 0 || tofRangeMm >= TOF_STOP_MM ||
      millis() - tofReadAt > 250) {
    said = false;
    return false;
  }
  if (!said) {
    said = true;
    out.print(F("tof: wall at "));
    out.print(tofRangeMm);
    out.println(F(" mm"));
  }
  return true;
}

// ===========================================================================
// IR REMOTE - NEC decoder on the D2 interrupt
// ===========================================================================
// The receiver's output idles high and pulls low for each burst of 38 kHz.
// NEC is decoded from the time between FALLING edges alone:
//   13.5 ms  leader (9 ms burst + 4.5 ms space): a new frame, 32 bits follow
//   11.25 ms repeat (9 ms + 2.25 ms): the key is still held, every 108 ms
//   1.125 ms a 0 bit, 2.25 ms a 1 bit, least significant first:
//            address, ~address, command, ~command
// Only the command byte and its inverse are checked, so any address (and
// extended-address remotes) work. The interrupt only fires on IR edges, so it
// costs nothing while nobody presses a key. Edges are timed on Timer1, set
// free-running at 4 us a tick (the servo headers 9/10 lose their hardware
// PWM, which nothing here uses; Timer2 keeps driving M1/M2).
#if USE_IR_REMOTE
byte irBitCount = 0xFF;                   // 0xFF = waiting for a leader (these three:
uint16_t irBits = 0;                      // only the interrupt touches them; the last
                                          // 16 bits are enough: command, ~command)
unsigned int irLastEdgeTicks = 0;
volatile byte irKey = 0;                  // command byte of the last full frame
volatile bool irKeyFresh = false;         // a new press arrived
volatile bool irRepeatSeen = false;       // a repeat frame arrived

ISR(INT0_vect) {
  unsigned int now = TCNT1;
  unsigned int gap = now - irLastEdgeTicks;  // 4 us ticks; wraps after 262 ms, harmless
  irLastEdgeTicks = now;
  if (gap > 2500 && gap < 3750) {          // 10 - 15 ms
    if (gap > 3125) {                      // 12.5 ms
      irBitCount = 0;                      // leader: 32 bits follow
    } else {
      irRepeatSeen = true;                 // repeat: the key is still held
      irBitCount = 0xFF;
    }
    return;
  }
  if (irBitCount >= 32) return;
  if (gap < 200 || gap > 700) {            // not 0.8 - 2.8 ms: wait for the next leader
    irBitCount = 0xFF;
    return;
  }
  irBits >>= 1;
  if (gap > 425) irBits |= 0x8000;         // over 1.7 ms: a 1
  if (++irBitCount == 32) {
    byte command = irBits;
    if ((byte)(command ^ (irBits >> 8)) == 0xFF) {
      irKey = command;
      irKeyFresh = true;
    }
    irBitCount = 0xFF;
  }
}

void irBegin() {
  TCCR1A = 0;                               // Timer1: normal mode, clk/64
  TCCR1B = _BV(CS11) | _BV(CS10);
  pinMode(IR_RECEIVER_PIN, INPUT_PULLUP);
  EICRA = (EICRA & ~(_BV(ISC00) | _BV(ISC01))) | _BV(ISC01);   // INT0 on the falling edge
  EIFR = _BV(INTF0);
  EIMSK |= _BV(INT0);
}

// The command byte of a new key press since the last call, or -1.
int irTakeKey() {
  int key = -1;
  noInterrupts();
  if (irKeyFresh) key = irKey;
  irKeyFresh = false;
  interrupts();
  return key;
}
#endif

// ===========================================================================
// MOTION CORE (blocking, abortable with 'x' over serial)
// ===========================================================================
bool checkForAbort() {
  int incoming;
  while ((incoming = readCommandByte()) >= 0) {
    if (incoming == 'x' || incoming == 'X') {
      abortRequested = true;
    }
  }
#if USE_IR_REMOTE
  if (irTakeKey() >= 0) abortRequested = true;   // any remote key stops a move
#endif
  return abortRequested;
}

float degreesToRadians(float degrees) { return degrees * PI / 180.0; }
float radiansToDegrees(float radians) { return radians * 180.0 / PI; }

// ---------------------------------------------------------------------------
// One engine for every timed move.
//
// The robot centre's velocity in the WORLD frame (= body frame at t = 0) is a
// vector of constant length that rotates at worldRotationRadPerSec (0 for a
// straight line, Omega for an orbit). The body itself rotates at
// spinRadPerSec. So in the body frame the commanded velocity is
//
//     v_body(t) = R( (worldRotation - spinRate) * t ) * v0
//
// where R(a) is a clockwise rotation: x' = x cos a - y sin a, y' = x sin a + y cos a.
//
// Before running, the move is sampled to find the fastest any wheel must turn.
// If that exceeds MAX_WHEEL_MM_PER_SEC the WHOLE move is slowed (time
// stretches, path unchanged) and a note is printed.
// ---------------------------------------------------------------------------
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

  // Ramp. k is the share of the move's speed, s how far along the move is
  // (in seconds at full speed), so every shape keeps its path and length.
  // Jumping straight to full power knocks the heading off (the wheels break
  // away and grip unevenly), so k climbs at ACCEL_MM_PER_S2 (motion[M_ACCEL]); it
  // starts where the fastest wheel is at the motors' start speed (below that
  // they hum and do not turn). The end is a hard stop: full speed, then the
  // motors cut and the robot rolls on (SPIN_COAST_MS allows for it).
  float rampPerSec = motion[M_ACCEL] / (peak * scale);   // share of the move's speed per second
  float k0 = constrain(least / (peak * scale), 0.05, 1.0);
  float k = k0, s = 0;
  float endAt = durationSec - k0 * motion[M_COAST] / 1000.0;
  unsigned long coastMs = motion[M_COAST] > 0 ? (long)motion[M_COAST] : 0;   // (negative = cut late: nothing to wait out)
  unsigned long nextTickAt = millis();
  float headingAtStart = imuUpdateHeading();   // 0 when there is no IMU
  headingCorrectionReset();

  while (s < endAt) {
    if (checkForAbort()) break;
    if (millis() >= nextTickAt) {
      nextTickAt += CONTROL_TICK_MS;
      if (imuPresent && bumped()) {
        abortRequested = true;
        break;
      }
      // How far the body has actually turned since the start: the commanded
      // profile open loop, the measured heading with the IMU. Using the
      // measured value keeps the WORLD path straight even if the spin lags.
      float bodyTurned = spinRadPerSec * s;
      float spinCommand = k * spinRadPerSec;
      if (imuActive()) {
        bodyTurned = imuUpdateHeading() - headingAtStart;
        spinCommand += headingCorrection(spinRadPerSec * s - bodyTurned);
      }
      float angle = worldRotationRadPerSec * s - bodyTurned;
      float forwardNow = v0Forward * cos(angle) - v0Right * sin(angle);
      float rightNow   = v0Forward * sin(angle) + v0Right * cos(angle);
      driveBody(k * forwardNow, k * rightNow, spinCommand);
      s += k * (CONTROL_TICK_MS / 1000.0);
      // Up at the ramp rate to full; no ramp-down: the move runs at full
      // speed to its end and the motors cut (a hard stop).
      k = min(k + rampPerSec * (CONTROL_TICK_MS / 1000.0), 1.0);
    }
    delay(1);
  }
  stopAllWheels();
  if (imuActive() && !abortRequested) {
    // Let it coast to a stop first (reading the gyro all the while), or the
    // settle step would push on while momentum is still turning it.
    for (unsigned long coastStart = millis(); millis() - coastStart < 2 * coastMs; delay(5)) imuUpdateHeading();
    settleHeading(headingAtStart + spinRadPerSec * durationSec);
  }
}

// Heading hold, every CONTROL_TICK_MS: error (rad) in, turn rate (rad/s) out.
// The wheels break away and grip unevenly at the start of a move, so a wrong
// turn RATE is countered from the first tick, before it becomes an angle (the
// error's change over one tick is that rate x tick), and the error is summed
// so a steady lean is pulled back to 0 instead of held.
float holdErrorSum = 0, holdLastError = 0;

void headingCorrectionReset() {
  holdErrorSum = holdLastError = 0;
}

float headingCorrection(float error) {
  // (capped at 1 rad/s of correction, so a robot held or stuck cannot wind it up)
  holdErrorSum = constrain(holdErrorSum + error, -1.0 / (HEADING_HOLD_KI * (CONTROL_TICK_MS / 1000.0)),
                           1.0 / (HEADING_HOLD_KI * (CONTROL_TICK_MS / 1000.0)));
  float rate = HEADING_HOLD_KP * error + HEADING_HOLD_KI * (CONTROL_TICK_MS / 1000.0) * holdErrorSum +
               HEADING_HOLD_KD / (CONTROL_TICK_MS / 1000.0) * (error - holdLastError);
  holdLastError = error;
  return rate;
}

// After a move: turn in place until the measured heading matches the target,
// within tolerance, or give up after HEADING_SETTLE_MS. P control clamped to
// the default spin rate; the PWM deadband guarantees even a small command
// actually moves the wheels.
void settleHeading(float targetRad) {
  float tolerance = HEADING_SETTLE_TOLERANCE_DEG * PI / 180.0;
  float maxRate = DEFAULT_SPIN_DEG_PER_SEC * PI / 180.0;
  unsigned long startedAt = millis();
  float lastError = targetRad - imuUpdateHeading();
  while (millis() - startedAt < HEADING_SETTLE_MS) {
    if (checkForAbort()) break;
    float error = targetRad - imuUpdateHeading();
    // Stop early by the coast: at the rate it is turning now, it slides on
    // about SPIN_COAST_MS worth (the slowest turn is the motors' start speed).
    float coast = (lastError - error) * max(0.0, motion[M_COAST]) / CONTROL_TICK_MS;
    lastError = error;
    if (fabs(error) < tolerance || (error > 0) == (coast > 0) && fabs(error) <= fabs(coast)) break;
    float rate = constrain(HEADING_HOLD_KP * error, -maxRate, maxRate);
    // A few degrees off, P control asks for a turn too slow to overcome the
    // rollers' friction and the robot stopped short: never ask for less.
    float minRate = SETTLE_MIN_DEG_PER_SEC * PI / 180.0;
    if (fabs(rate) < minRate) rate = rate < 0 ? -minRate : minRate;
    driveBody(0, 0, rate);
    delay(CONTROL_TICK_MS);
  }
  stopAllWheels();
}

// ===========================================================================
// MOTION PRIMITIVES
// ===========================================================================

// Straight line on a bearing (0 = forward, 90 = right) while the body turns
// spinDegrees over the move. spinDegrees = 0 is a plain straight line.
// The bearing is in the body frame at the START of the move; the path in the
// world stays straight even though the body is turning.
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

// Orbit the robot's centre around an arbitrary pivot.
//   pivot P = (pf, pr) in the start body frame. Rotating about P at Omega
//   (clockwise +) gives centre velocity v0 = Omega * (pr, -pf), and the
//   velocity vector itself rotates at Omega. faceCenter => spin = Omega.
void executeOrbit(float pivotForwardMm, float pivotRightMm, float sweepDegrees,
                  float speedMmPerSec, bool faceCenter, float spinDegrees) {
  if (speedMmPerSec <= 0 || fabs(sweepDegrees) < 0.01) return;

  float radiusMm = sqrt(pivotForwardMm * pivotForwardMm + pivotRightMm * pivotRightMm);
  if (radiusMm < 1.0) {
    float totalDegrees = faceCenter ? sweepDegrees : spinDegrees;
    spinInPlace(totalDegrees, DEFAULT_SPIN_DEG_PER_SEC);
    return;
  }

  float sweepSign = (sweepDegrees < 0) ? -1.0 : 1.0;
  float orbitRadPerSec = sweepSign * speedMmPerSec / radiusMm;
  float durationSec = fabs(degreesToRadians(sweepDegrees)) * radiusMm / speedMmPerSec;
  float spinRadPerSec = faceCenter ? orbitRadPerSec
                                   : degreesToRadians(spinDegrees) / durationSec;
  executeTimedBodyMotion(orbitRadPerSec * pivotRightMm, -orbitRadPerSec * pivotForwardMm,
                         orbitRadPerSec, spinRadPerSec, durationSec);
}

void orbitAboutPointXY(float pivotForwardMm, float pivotRightMm, float sweepDegrees,
                       float speedMmPerSec, bool faceCenter, float spinDegrees) {
  executeOrbit(pivotForwardMm, pivotRightMm, sweepDegrees, speedMmPerSec, faceCenter, spinDegrees);
}

// ===========================================================================
// DEMOS
// ===========================================================================
// Every demo distance and orbit radius goes through these, scaled by
// demoSizeScale (z1..z8 = 25 %..200 %) so one demo fits a small room or
// fills a gym. Speed stays DEMO_SPEED_MM_PER_SEC; smaller runs take less time.
float demoSizeScale = 1.0;

void demoStraightSpin(float bearingDegrees, float distanceMm, float spinDegrees) {
  driveStraightWhileSpinning(bearingDegrees, distanceMm * demoSizeScale, DEMO_SPEED_MM_PER_SEC, spinDegrees);
}
void demoOrbitXY(float pivotForwardMm, float pivotRightMm, float sweepDegrees, bool faceCenter, float spinDegrees) {
  orbitAboutPointXY(pivotForwardMm * demoSizeScale, pivotRightMm * demoSizeScale, sweepDegrees,
                    DEMO_SPEED_MM_PER_SEC, faceCenter, spinDegrees);
}

void demoPause() {
  unsigned long startedAt = millis();
  while (millis() - startedAt < DEMO_PAUSE_MS) {
    if (checkForAbort()) return;
    imuUpdateHeading();
    delay(5);
  }
}

// Drive a CLOCKWISE regular polygon (first side straight ahead) while the
// body turns spinPerSideDegrees on every side.
//
// cornerHeadingOffsetDegrees is the body heading relative to the NEXT side's
// direction of travel at each corner:
//     0                     -> facing the next direction of travel
//     (180 - exterior) / 2  -> facing IN toward the centre (45 for a square,
//                              30 for a triangle, 60 for a hexagon)
//     that minus 180        -> facing OUT
// The offset is the same at every corner, so the drive bearing in the body
// frame starts at -offset and the per-side spin is +exterior angle. The robot
// spins in place to take up the offset first and undoes it at the end, so it
// finishes in its starting pose.
void demoPolygon(int sideCount, float sideMm, float spinPerSideDegrees,
                 float cornerHeadingOffsetDegrees) {
  float exteriorAngleDegrees = 360.0 / sideCount;
  float bearingDegrees = -cornerHeadingOffsetDegrees;
  spinInPlace(cornerHeadingOffsetDegrees, DEFAULT_SPIN_DEG_PER_SEC);
  if (abortRequested) return;
  demoPause();
  for (int side = 0; side < sideCount; side++) {
    if (abortRequested) return;
    demoStraightSpin(bearingDegrees, sideMm, spinPerSideDegrees);
    // After the side the body has turned spinPerSideDegrees; the next side's
    // world direction is one exterior angle clockwise. Bearing in the new
    // body frame:
    bearingDegrees = bearingDegrees + exteriorAngleDegrees - spinPerSideDegrees;
    demoPause();
  }
  spinInPlace(-cornerHeadingOffsetDegrees, DEFAULT_SPIN_DEG_PER_SEC);
}

// ---------------------------------------------------------------------------
// The demos themselves are data: one table of steps in flash, read by
// runDemoSteps(). Each demo starts with DEMO(key). Distances and pivots are
// scaled by the demo size; angles are not. Steps:
//   LINE(bearing, mm, spin)        straight on a bearing while turning spin degrees
//   ORBIT(fwd, right, sweep, spin) centre orbits a pivot, body turns spin degrees
//   FACE(fwd, right, sweep)        orbit with the nose locked on the pivot
//   SPIN(deg)                      turn in place
//   PAUSE                          DEMO_PAUSE_MS breath
//   POLYGON(sides, mm, spinPerSide, cornerOffset)   see demoPolygon()
//   REPEAT(n, bearingStep) ... NEXT   run the steps between n times, turning
//                                  every LINE bearing by bearingStep more each pass
// Circle strafe facing in (pk) is always in; the rest (the Program tab does
// moves like these now) need USE_EXTRA_DEMOS 1, about 750 bytes of flash:
// turn another feature off with it (USE_IR_REMOTE 0, or USE_SONAR 0 and
// USE_TOF 0). The app greys out the demos the robot does not list.
#define USE_EXTRA_DEMOS 0

struct DemoStep {
  char op;
  int16_t a, b, c, d;
};
#define DEMO(key)             { 'K', key, 0, 0, 0 }
#define LINE(bearing, mm, spin) { 'L', bearing, mm, spin, 0 }
#define ORBIT(f, r, sweep, spin) { 'O', f, r, sweep, spin }
#define FACE(f, r, sweep)     { 'F', f, r, sweep, 0 }
#define SPIN(deg)             { 'S', deg, 0, 0, 0 }
#define PAUSE                 { 'P', 0, 0, 0, 0 }
#define POLYGON(n, mm, spin, offset) { 'G', n, mm, spin, offset }
#define REPEAT(n, step)       { 'R', n, step, 0, 0 }
#define NEXT                  { 'N', 0, 0, 0, 0 }
const int16_t SQUARE = (int16_t)DEMO_SQUARE_SIDE_MM;

const DemoStep DEMO_STEPS[] PROGMEM = {
  // pk - circle strafe IN: orbit a point 500 mm ahead with the nose locked on
  // it, one lap each way. Stand something at the pivot and the robot stares at it.
  DEMO('k'), FACE(500, 0, 360), PAUSE, FACE(500, 0, -360),
#if USE_EXTRA_DEMOS
  // pc - crab zigzag: three forward diagonals, then three back home. Heading held.
  DEMO('c'), REPEAT(3, 0), LINE(45, 300, 0), LINE(-45, 300, 0), NEXT, PAUSE,
             REPEAT(3, 0), LINE(135, 300, 0), LINE(-135, 300, 0), NEXT,
  // pd - J-turn: 800 mm out while turning 180, so it arrives facing home;
  // then 800 mm "forward" (back the way it came) turning another 180.
  DEMO('d'), LINE(0, 800, 180), PAUSE, LINE(0, 800, 180),
  // pj - parallel park: pull 500 mm past the space, strafe 250 mm in, wait,
  // strafe out, reverse home.
  DEMO('j'), LINE(0, 500, 0), PAUSE, LINE(90, 250, 0), PAUSE, PAUSE, PAUSE,
             LINE(-90, 250, 0), PAUSE, LINE(180, 500, 0),
  // pu - turn around: 180 in place.
  DEMO('u'), SPIN(180),
  // p1 - circle strafe: heading held, centre orbits a point 400 mm ahead.
  DEMO('1'), ORBIT(400, 0, 360, 0), PAUSE, ORBIT(400, 0, -360, 0),
  // p2-p5 - squares: full 360 per side / face the next side / face in / face out.
  DEMO('2'), POLYGON(4, SQUARE, 360, 0),
  DEMO('3'), POLYGON(4, SQUARE, 90, 0),
  DEMO('4'), POLYGON(4, SQUARE, 90, 45),
  DEMO('5'), POLYGON(4, SQUARE, 90, -135),
  // p6 - figure eight, car-style: nose follows the curve.
  DEMO('6'), FACE(0, 300, 360), FACE(0, -300, -360),
  // p7 - compass rose: out and back on all eight bearings, heading held.
  DEMO('7'), REPEAT(8, 45), LINE(0, 300, 0), LINE(180, 300, 0), PAUSE, NEXT,
  // p8 - orbit a point 400 mm ahead while the body spins twice.
  DEMO('8'), ORBIT(400, 0, 360, 720),
  // p9 - spiral out: quarter arcs of growing radius, then unwinds.
  DEMO('9'), FACE(0, 100, 90), FACE(0, 200, 90), FACE(0, 300, 90),
             FACE(0, 400, 90), FACE(0, 500, 90), FACE(0, 600, 90), PAUSE,
             FACE(0, 600, -90), FACE(0, 500, -90), FACE(0, 400, -90),
             FACE(0, 300, -90), FACE(0, 200, -90), FACE(0, 100, -90),
  // pa - pendulum: swing +-60 around a point 500 mm ahead, always facing it.
  DEMO('a'), FACE(500, 0, 60), FACE(500, 0, -120), FACE(500, 0, 60),
  // pb - pentagram: five 500 mm legs turning 144 each, heading held.
  DEMO('b'), REPEAT(5, 144), LINE(0, 500, 0), PAUSE, NEXT,
  // pe - figure eight with heading held: pure strafing.
  DEMO('e'), ORBIT(0, 300, 360, 0), ORBIT(0, -300, -360, 0),
  // pf - triangle facing in; pg - hexagon, heading held.
  DEMO('f'), POLYGON(3, SQUARE, 120, 30),
  DEMO('g'), POLYGON(6, 400, 0, 0),
  // ph - flower: five circles around a point 250 mm ahead, each with a 72
  // degree body turn, so every petal passes through the start point.
  DEMO('h'), REPEAT(5, 0), ORBIT(250, 0, 360, 72), NEXT,
  // pi - slalom: three S-bumps out, spin 180, three S-bumps home, spin 180.
  DEMO('i'), REPEAT(3, 0), FACE(0, 300, 90), FACE(0, -300, -90), NEXT, PAUSE, SPIN(180), PAUSE,
             REPEAT(3, 0), FACE(0, 300, 90), FACE(0, -300, -90), NEXT, PAUSE, SPIN(180),
#endif
  { 0, 0, 0, 0, 0 }
};

DemoStep demoStepAt(const DemoStep *step) {
  DemoStep copy;
  memcpy_P(&copy, step, sizeof copy);
  return copy;
}

const DemoStep *findDemo(char key) {
  for (const DemoStep *step = DEMO_STEPS; demoStepAt(step).op; step++) {
    DemoStep s = demoStepAt(step);
    if (s.op == 'K' && s.a == key) return step + 1;
  }
  return NULL;
}

void runDemoSteps(const DemoStep *step) {
  const DemoStep *loopStart = step;
  int loopsLeft = 0, bearingStep = 0, bearingOffset = 0;
  for (;; step++) {
    if (abortRequested) return;
    DemoStep s = demoStepAt(step);
    switch (s.op) {
      case 'L': demoStraightSpin(s.a + bearingOffset, s.b, s.c); break;
      case 'O':
      case 'F': demoOrbitXY(s.a, s.b, s.c, s.op == 'F', s.d); break;
      case 'S': spinInPlace(s.a, DEFAULT_SPIN_DEG_PER_SEC); break;
      case 'P': demoPause(); break;
      case 'G': demoPolygon(s.a, s.b, s.c, s.d); break;
      case 'R': loopStart = step; loopsLeft = s.a; bearingStep = s.b; bearingOffset = 0; break;
      case 'N':
        if (--loopsLeft > 0) {
          bearingOffset += bearingStep;
          step = loopStart;
        }
        break;
      default: return;   // the next DEMO, or the end of the table
    }
  }
}

// "kcdj": the demo keys this build has, in table order (the app reads them).
void printDemoKeys() {
  for (const DemoStep *step = DEMO_STEPS; demoStepAt(step).op; step++) {
    DemoStep s = demoStepAt(step);
    if (s.op == 'K') out.print((char)s.a);
  }
  out.println();
}

void printDemoList() {
  out.print(F("demos "));
  printDemoKeys();
  out.print(F("demo size "));
  out.print((int)(demoSizeScale * 100 + 0.5));
  out.println(F(" %"));
}

void runDemo(char which) {
  const DemoStep *steps = findDemo(which);
  if (!steps) {
    printDemoList();
    return;
  }
  abortRequested = false;
  sensorWatch = 0;
  out.print(F("demo p"));
  out.print(which);
  out.println(F(": start"));
  runDemoSteps(steps);
  stopAllWheels();
  out.println(abortRequested ? F("demo: ABORTED") : F("demo: done"));
}


// ===========================================================================
// MANUAL CONTROL + COMMAND LOOP
// ===========================================================================
float manualSpeedFraction = 0.6;
float manualForward = 0, manualRight = 0, manualSpin = 0;
unsigned long manualLastCommandAt = 0;
bool manualActive = false;

void printHelp() {
  out.println(F("wsad qe x 1-9 p z i l b r k ?"));
  printDemoList();
}

bool manualVector = false;     // driving from a v command (full-scale fractions)

void applyManual() {
  manualLastCommandAt = millis();
  manualActive = true;
  sensorWatch = 0;
  lineFollowing = false;
  lightFollowing = false;
  if (manualForward > 0 && tofWallAhead()) {
    if (!manualVector) {
      manualActive = false;
      stopAllWheels();
      return;
    }
    manualForward = 0;          // the sticks may still strafe and turn
  }
  manualSteer();
}

// Heading hold while driving by hand (pad, sticks, IR remote). Whenever no
// turn is asked for, the robot keeps the heading it had when the turning
// stopped: loop() calls this every CONTROL_TICK_MS and the gyro error becomes
// a turn command, the same correction the demos use. Mecanum wheels slip
// unevenly when strafing, so without this a sideways slide also rotates.
bool manualHolding = false;
float manualHoldRad = 0;

void manualSteer() {
  if (imuPresent && bumped()) {
    manualActive = false;
    stopAllWheels();
    return;
  }
  float speed = (manualVector ? 1.0 : manualSpeedFraction) * motion[M_MAX];
  float spinRadPerSec = manualSpin * speed / rotationLeverMm();
  manualHolding = manualHolding && manualSpin == 0 && imuActive();
  if (manualSpin == 0 && imuActive()) {
    if (!manualHolding) {
      manualHoldRad = imuUnwrappedRad;   // hold the heading we have now
      headingCorrectionReset();
    }
    manualHolding = true;
    float maxRate = DEFAULT_SPIN_DEG_PER_SEC * PI / 180.0;
    spinRadPerSec = constrain(headingCorrection(manualHoldRad - imuUnwrappedRad), -maxRate, maxRate);
  }
  driveBody(manualForward * speed, manualRight * speed, spinRadPerSec);
}

// Every manual move: forward / right / clockwise as -100..100 %. vector =
// from a v command (full-scale, dead-man timeout) rather than a w/a/s/d/q/e
// key (a fraction of the 1-9 speed, latched until x).
void manualDrive(int forward, int right, int spin, bool vector) {
  manualForward = constrain(forward, -100, 100) * 0.01;
  manualRight = constrain(right, -100, 100) * 0.01;
  manualSpin = constrain(spin, -100, 100) * 0.01;
  manualVector = vector;
  applyManual();
}

// v<forward>,<right>,<spin>; from the two-stick controller. No reply: the
// controller streams these, and a reply would block the Bluetooth port while
// the next one arrives.
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

// m<bearing>,<mm>,<turn>; one step of a block program: drive mm on a bearing
// (degrees clockwise from the nose, at DEMO_SPEED_MM_PER_SEC) while the body
// turns turn degrees; mm 0 = turn in place. Same engine as the demos: with
// heading hold on (in) the IMU steers it and squares it up at the end, off
// (if) it runs on timing alone. Blocks like a demo; x aborts.
void handleMove(const char *text) {
  int value[3];
  parseThreeInts(text, value);
  abortRequested = false;
  manualActive = false;
  sensorWatch = 0;
  lineFollowing = false;
  lightFollowing = false;
  out.println(F("move: start"));
  accelPeak = 0;
  if (value[1] > 0) driveStraightWhileSpinning(value[0], value[1], DEMO_SPEED_MM_PER_SEC, value[2]);
  else spinInPlace(value[2], DEFAULT_SPIN_DEG_PER_SEC);
  stopAllWheels();
  out.print(abortRequested ? F("move: ABORTED") : F("move: done"));
  out.print(F(" jolt "));
  out.println(accelPeak);
}

void handleDriveVector(const char *text) {
  int value[3];
  parseThreeInts(text, value);
  if (value[0] == 0 && value[1] == 0 && value[2] == 0) {
    manualActive = false;
    stopAllWheels();
    return;
  }
  manualDrive(value[0], value[1], value[2], true);
}

// A k or v command collects text up to ';' (or a newline) before it runs.
char textCommand = 'k';
bool awaitingConfigText = false;
char configText[20];
byte configTextLength = 0;

void handleCommand(char command) {
  if (awaitingConfigText) {
    if (command == 'x') {
      awaitingConfigText = false;   // no key or value contains x: it is a stop, handle it below
    } else if (command == ';') {
      awaitingConfigText = false;
      configText[configTextLength] = 0;
      if (textCommand == 'v') handleDriveVector(configText);
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
  static bool awaitingDemoDigit = false;
  static bool awaitingSizeDigit = false;
  static bool awaitingImuLetter = false;
  static char awaitingSensorGroup = 0;
  // l / b / r plus a letter: line sensor, light sensors, range (laser).
  if (awaitingSensorGroup) {
    char group = awaitingSensorGroup;
    awaitingSensorGroup = 0;
    const __FlashStringHelper *name = group == 'l' ? F("line") : group == 'b' ? F("light") : F("range");
    manualActive = false;
    if (group != 'l') lineFollowing = false;
    if (group != 'b') lightFollowing = false;
    sensorWatch = 0;
    abortRequested = false;
    if (group == 'r') {
      if (command == 'u') {
        printSonar(false);
      } else if (command == 'w' && (tofPresent || sonarPinsFree)) {
        sensorWatch = 'r';
        out.print(name);
        out.println(F(" watch: start"));
      } else {
        printRange();
      }
    } else if (command == 'w') {
      if (group == 'l' ? lineConfigured() : lightConfigured()) {
        sensorWatch = group;
        out.print(name);
        out.println(F(" watch: start"));
      }
    } else if (command == 's') {
      if (group == 'l') printLineReading(); else printLightReading();
    } else if (command == 'c') {
      if (group == 'l') lineCalibrate(); else lightCalibrate();
    } else if (command == 'f') {
      if (group == 'l') lineBeginFollow(); else lightBeginFollow();
    } else {
      out.print(name);
      out.print(F(": "));
      out.print(group);
      out.println(F("s w c f"));
    }
    return;
  }
  if (awaitingImuLetter) {
    awaitingImuLetter = false;
    if (command == 'h') {
      printHeading();
      return;
    }
    if (command == 'w' && imuPresent) {
      sensorWatch = 'i';
      out.println(F("imu watch: start"));
      return;
    }
    if (command == 'n' || command == 'f') {
      imuEnabled = (command == 'n') && imuPresent;
      if (command == 'n' && !imuPresent) {
        out.println(F("imu: not present"));
      }
      if (imuEnabled) {
        imuRestart();
      }
      printImuStatus(F("imu: "));
      return;
    }
    printImuStatus(F("imu: "));   // plain 'i': report, then treat this char normally
  }
  if (awaitingSizeDigit) {
    awaitingSizeDigit = false;
    if (command >= '1' && command <= '8') {
      demoSizeScale = (command - '0') * 0.25;
    }
    out.print(F("demo size "));
    out.print((int)(demoSizeScale * 100 + 0.5));
    out.println(F(" %"));
    return;
  }
  if (awaitingDemoDigit) {
    awaitingDemoDigit = false;
    manualActive = false;
    runDemo(command);
    return;
  }
  switch (command) {
    case 'w': manualDrive(100, 0, 0, false);  break;
    case 's': manualDrive(-100, 0, 0, false); break;
    case 'a': manualDrive(0, -100, 0, false); break;
    case 'd': manualDrive(0, 100, 0, false);  break;
    case 'q': manualDrive(0, 0, -100, false); break;
    case 'e': manualDrive(0, 0, 100, false);  break;
    case 'x': manualActive = false; lineFollowing = false; lightFollowing = false; sensorWatch = 0; stopAllWheels(); out.println(F("stop")); break;
    case 'p': awaitingDemoDigit = true; break;
    case 'z': awaitingSizeDigit = true; break;
    case 'c':
    case 't': out.println(F("in calibration")); break;
    case 'i': awaitingImuLetter = true; break;
    case 'l':
    case 'b':
    case 'r': awaitingSensorGroup = command; break;
    case 'k':
    case 'm':
    case 'v': textCommand = command; awaitingConfigText = true; configTextLength = 0; break;
    case '?': printHelp(); break;
    default:
      if (command >= '1' && command <= '9') {
        manualSpeedFraction = (command - '0') / 9.0;
        out.print(F("speed "));
        out.println(manualSpeedFraction);
        if (manualActive) applyManual();
      }
      break;
  }
}

#if USE_IR_REMOTE
// NEC command byte -> the serial command it types. Both common kit remotes
// work: the 17-key (arrows, OK, 0-9, * #) and the 21-key "Car MP3" one, whose
// 2 / 4 / 5 / 6 / 8 send the 17-key's arrow and OK codes. '<' '>' = speed
// down / up one level. Unlisted keys print their code, so another remote
// can be added here.
struct IrKey {
  byte code;
  char command[3];
};
const IrKey IR_KEYS[] PROGMEM = {
  { 0x18, "w" },  { 0x52, "s" },  { 0x08, "a" },  { 0x5A, "d" },   // arrows / 2 8 4 6
  { 0x1C, "x" },  { 0x40, "x" },                                    // OK, 5 / >>|
  { 0x45, "q" },  { 0x47, "e" },  { 0x0C, "q" },  { 0x5E, "e" },   // 1 3 / CH- CH+, 1 3
  { 0x44, "<" },  { 0x43, ">" },                                    // 4 6 / |<< >||
  { 0x09, "pk" },                                                   // 9 / EQ: circle strafe facing in
  { 0x16, "lf" }, { 0x0D, "bf" },                                   // * # / 0 200+
};

bool irDriving = false;           // a w/s/a/d/q/e from the remote is being held
unsigned long irHeldAt = 0;

void irPoll() {
  bool repeated;
  noInterrupts();
  repeated = irRepeatSeen;
  irRepeatSeen = false;
  interrupts();
  if (repeated && irDriving) irHeldAt = millis();
  int key = irTakeKey();
  if (key >= 0) {
    IrKey entry;
    byte i = 0;
    for (; i < sizeof(IR_KEYS) / sizeof(IR_KEYS[0]); i++) {
      memcpy_P(&entry, &IR_KEYS[i], sizeof entry);
      if (entry.code == key) break;
    }
    if (i == sizeof(IR_KEYS) / sizeof(IR_KEYS[0])) {
      out.print(F("ir: key 0x"));
      out.print(key, HEX);
      out.println(F(" not mapped"));
      return;
    }
    char first = entry.command[0];
    irDriving = strchr("wsadqe", first) != NULL;
    irHeldAt = millis();
    if (first == '<' || first == '>') {
      int level = (int)(manualSpeedFraction * 9 + 0.5) + (first == '>' ? 1 : -1);
      handleCommand('0' + constrain(level, 1, 9));
      return;
    }
    handleCommand(first);
    if (entry.command[1]) handleCommand(entry.command[1]);
  }
  if (irDriving && millis() - irHeldAt > IR_RELEASE_MS) {
    irDriving = false;
    handleCommand('x');
  }
}
#endif

// Every pin an enabled feature uses, so clashes are announced instead of
// silently reading a Bluetooth line as a light sensor. The fixed claims come
// from the compile-time features; the sensor claims from the live settings.
const byte MAX_PIN_CLAIMS = 32;
byte claimedPin[MAX_PIN_CLAIMS];
const __FlashStringHelper *claimedBy[MAX_PIN_CLAIMS];
byte pinClaims = 0;

void claimPin(byte pin, const __FlashStringHelper *who) {
  if (pinClaims < MAX_PIN_CLAIMS) {
    claimedPin[pinClaims] = pin;
    claimedBy[pinClaims] = who;
    pinClaims++;
  }
}

void claimFixedPins() {
  pinClaims = 0;
#if USE_BLUETOOTH_SOFTSERIAL
  claimPin(BLUETOOTH_RX_PIN, F("bluetooth"));
  claimPin(BLUETOOTH_TX_PIN, F("bluetooth"));
#endif
  if (muxInUse) {
    claimPin(MUX_COMMON_PIN, F("mux common"));
    for (byte i = 0; i < MUX_SELECT_COUNT; i++) {
      claimPin(MUX_SELECT_PINS[i], F("mux select"));
    }
  }
  if (i2cStarted) {
    claimPin(A4, F("i2c"));
    claimPin(A5, F("i2c"));
  }
#if USE_HEARTBEAT
  if (!muxInUse) claimPin(HEARTBEAT_PIN, F("heartbeat LED"));
#endif
#if USE_IR_REMOTE
  claimPin(IR_RECEIVER_PIN, F("ir remote"));
#endif
}

// K,taken,a0:bluetooth,a1:bluetooth,... : analog pins the sensors cannot use.
void printTakenPins() {
  claimFixedPins();
  out.print(F("K,taken"));
  for (byte i = 0; i < pinClaims; i++) {
    if (claimedPin[i] < A0 || claimedPin[i] > A5) continue;
    out.print(F(",a"));
    out.print(claimedPin[i] - A0);
    out.print(':');
    out.print(claimedBy[i]);
  }
  out.println();
}

// Also (re)claims the mux select pins for the running layout first.
void auditPins() {
  initAnalogMux();
  claimFixedPins();
  for (byte i = 0; i < lineLayout().count; i++) {
    const AnalogSource &source = lineLayout().source[i];
    if (source.pin != 0xFF && source.muxChannel < 0 && !isAdsSource(source)) claimPin(source.pin, F("line sensor"));
  }
  for (byte i = 0; i < lightLayout().count; i++) {
    const AnalogSource &source = lightLayout().source[i];
    if (source.pin != 0xFF && source.muxChannel < 0 && !isAdsSource(source)) claimPin(source.pin, F("light sensor"));
  }
  sonarPinsFree = USE_SONAR;
  for (byte a = 0; a < pinClaims; a++) {
    if (claimedPin[a] == SONAR_ECHO_PIN || claimedPin[a] == SONAR_TRIG_PIN) sonarPinsFree = false;
    for (byte b = a + 1; b < pinClaims; b++) {
      if (claimedPin[a] == claimedPin[b]) {
        out.print(F("PIN CLASH: "));
        out.print(claimedBy[a]);
        out.print(F(" and "));
        out.print(claimedBy[b]);
        out.print(F(" both on pin "));
        if (claimedPin[a] >= A0 && claimedPin[a] <= A5) {
          out.print('a');
          out.println(claimedPin[a] - A0);
        } else {
          out.println(claimedPin[a]);
        }
      }
    }
  }
}


void setup() {
  Serial.begin(9600);
#if USE_BLUETOOTH_SOFTSERIAL
  bluetoothSerial.begin(9600);
#endif
  initMotorPins();
  stopAllWheels();
  out.println(F(SKETCH_NAME " v" FIRMWARE_VERSION));
#if USE_HEARTBEAT
  pinMode(HEARTBEAT_PIN, OUTPUT);
  digitalWrite(HEARTBEAT_PIN, LOW);
#endif
  loadSensorSettings(true);
#if CALIBRATION_IN_EEPROM
  loadMotion();
#endif
#if USE_IMU
  imuPresent = imuBegin();
  imuEnabled = imuPresent;      // compiled in + detected = heading hold on
  printImuStatus(F("imu: "));
#endif
  adsProbe();
#if USE_TOF
  tofPresent = tofBegin();
  if (tofPresent) out.println(F("tof: VL53L0X found"));
#endif
#if USE_IR_REMOTE
  irBegin();
#endif
#if HAVE_I2C
  i2cEndIfUnused();
#endif
  chooseSensorLayout(true);
  auditPins();
  printHelp();
}

void loop() {
  int incomingByte;
#if USE_BLUETOOTH_SOFTSERIAL
  // SoftwareSerial cannot receive while it transmits, and every reply goes
  // out on it. Let a burst from the module finish arriving (3 ms of quiet)
  // before handling any of it, or the reply to its first command cuts off
  // the rest: "z4p1" set the size and lost the p1.
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
    if (incoming >= 'A' && incoming <= 'Z') incoming = incoming - 'A' + 'a';
    if (incoming == '\r' || incoming == '\n') {
      handleCommand(';');   // ends a k / v command, and finishes a lone i, p, l, r...
      continue;
    }
    if (incoming == ' ') continue;
    handleCommand(incoming);
  }
#if USE_IR_REMOTE
  irPoll();
#endif
  tofPoll();
#if USE_IMU
  static unsigned long imuReadAt = 0;
  if (imuPresent && millis() - imuReadAt >= 10) {
    imuReadAt = millis();
    imuUpdateHeading();
  }
  static unsigned long steerAt = 0;
  if (manualActive && imuActive() && millis() - steerAt >= CONTROL_TICK_MS) {
    steerAt = millis();
    manualSteer();
  }
#endif
  if (((manualActive && manualForward > 0) || lineFollowing) && tofWallAhead()) {
    handleCommand('x');
  }
  if (manualActive && (manualVector || MANUAL_TIMEOUT_MS > 0) &&
      millis() - manualLastCommandAt > (manualVector ? VECTOR_TIMEOUT_MS : MANUAL_TIMEOUT_MS)) {
    manualActive = false;
    stopAllWheels();
  }
  if (lineFollowing) {
    lineFollowTick();
  }
  if (lightFollowing) {
    lightFollowTick();
  }
  if (sensorWatch && millis() - lineLastTelemetryAt >= LINE_TELEMETRY_MS) {
    lineLastTelemetryAt = millis();
    if (sensorWatch == 'i') {
      printHeading();
    } else if (sensorWatch == 'r') {
      if (tofPresent) printRange();
      printSonar(true);
    } else if (sensorWatch == 'l') {
      lineUpdate();
      printRawTelemetry(F("LR"), lineRaw, lineLayout().count);
      printLineTelemetry();
    } else {
      lightUpdate();
      printRawTelemetry(F("BR"), lightRaw, lightLayout().count);
      printLightTelemetry();
    }
  }
#if USE_HEARTBEAT
  if (!muxInUse) digitalWrite(HEARTBEAT_PIN, (millis() % HEARTBEAT_PERIOD_MS) < HEARTBEAT_ON_MS ? HIGH : LOW);
#endif
}
