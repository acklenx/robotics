// Runs an Arduino Uno ELF in simavr and drives Serial from a script on stdin.
//   send <text>     bytes to UART0 at 9600-baud pacing (\n and \; escapes understood)
//   btsend <text>   the same bytes bit-banged into A0, where the kit's Bluetooth
//                   module talks to SoftwareSerial (back to back, like the module)
//   wait <ms>       run the simulated clock
//   adc <ch> <mv>   set analog input ch (0-5) to mv millivolts
//   reboot          fresh MCU with the same firmware, EEPROM carried over
//   flash <elf>     upload another sketch: fresh MCU with that firmware, EEPROM carried
//                   over (as an upload through the bootloader leaves it)
//   dev ads <n>     attach an ADS1115 at 0x48+n      dev mpu / dev bno   an IMU
//   dev clear       remove every I2C device (takes effect for the next probe)
//   ads <n> <ch> <mv>   voltage on that ADS1115 input
//   gyro <dps>      MPU-6050 Z rate (counter-clockwise positive, like the chip)
//   heading <deg>   BNO055 heading
//   dev tof         attach a VL53L0X at 0x29      tof <mm>   its range (8190 = nothing)
//   ir <hex> [ms]   an NEC remote key on the IR pin (address 0), held for ms with repeats
//   irpin <port> <bit>   where the IR receiver is: D 2 (the default, D2) or B 2 (D10)
//   sonar <mm>      HC-SR04 on A3 trig / A2 echo: echo for that distance (-1 = none)
//   pwm             print the four motor PWM duties (M1-M4, 0 when disconnected)
//   spinmodel <maxdps> <taums> [stall]   a robot whose gyro answers its motors: while
//                   all four wheels run, it turns clockwise at maxdps * sqrt((duty-40)/215)
//                   (dead below duty 40, like real gear motors), reaching that speed
//                   and coasting down with time constant taums. 0 0 = off. stall: below
//                   that duty it does not start at all (static friction, like the real
//                   robot turning in place: nothing at 64, 46 deg/s at 72).
//                   Each wheel counts for a quarter, signed by its direction (the
//                   shield's 74HC595, decoded here): the robot turns at
//                   maxdps * (FL - FR + RL - RR) / 4, so straight driving with one
//                   side weaker turns it too.
//   wheels <sFL> <sFR> <sRL> <sRR> [<oFL> <oFR> <oRL> <oRR>]   per-wheel strength
//                   (1 = nominal) and stall offset (PWM added to the stall)
//   strafemodel <stall> <mm/s> <taums>   the MPU-6050's accelerometer answers the
//                   motors: the robot slides sideways (Y) at mm/s x the wheels' mean share
//                   when strafing, and drives forward (X) the same way, with time constant
//                   taums. stall: the duty every wheel needs before a strafe moves at all
//                   (rollers drag sideways; below it the motors hum: vibration only).
//                   Forward uses spinmodel's stall. 0 0 0 = off.
//   jolt <g>        a 60 ms knock on the accelerometer's X (hitting a wall)
//   spike <g>       a 20 ms knock on X (a rattle: one control tick sees it, never two)
//   gravity <g>     what Z reads standing still (1 = flat; -1 = board upside down)
//   yaw             print the spin model's true heading (deg, clockwise) and the
//                   largest |heading| since the last yaw, then reset that peak
//                   (yaw 0: also zero the heading)
// Everything the sketch prints on UART0 goes to stdout.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/avr_uart.h>
#include <simavr/avr_adc.h>
#include <simavr/avr_eeprom.h>
#include <simavr/avr_twi.h>
#include <simavr/avr_ioport.h>
#include <simavr/sim_cycle_timers.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

static elf_firmware_t firmware;
static avr_t *avr;
static uint32_t adcMillivolts[6];
static uint8_t eepromCopy[1024];
static int haveEeprom = 0;

// --- simulated I2C devices ---------------------------------------------------
enum { DEV_ADS1115, DEV_MPU6050, DEV_BNO055, DEV_VL53L0X };
typedef struct {
  int kind;
  uint8_t address;          // 7-bit
  uint8_t pointer;
  int index;                // bytes written since START (0 = register pointer)
  int readIndex;
  uint8_t regs[256];
  uint16_t config;          // ADS1115
  int16_t conversion;       // ADS1115
  double inputMv[4];        // ADS1115
} i2c_device_t;
static i2c_device_t devices[8];
static int deviceCount = 0;
static i2c_device_t *selectedDevice = NULL;
static double gyroDps = 0;
static double spinModelMaxDps = 0, spinModelTauMs = 0, spinModelRate = 0, spinModelStall = 0;
static double simYawDeg = 0, simYawPeakDeg = 0;   // the model's true heading (yaw command)
static double strafeStall = 0, slideMaxMmps = 0, slideTauMs = 100;   // strafemodel
static double slideRight = 0, slideForward = 0;    // the model robot's velocity (mm/s)
static double accelRight = 0, accelForward = 0;    // its acceleration (mm/s^2), with vibration
static double joltMms2 = 0, joltLeftMs = 0;        // jolt command
static double gravityG = 1;                        // gravity command
static double wheelStrength[4] = { 1, 1, 1, 1 }, wheelStallOffset[4] = { 0, 0, 0, 0 };
static uint8_t shiftBits = 0, shiftLatched = 0;
static double headingDeg = 0;
static int tofRangeMm = 8190;
static int sonarMm = -1;
static char irPort = 'D';   // the IR receiver's port and bit (irpin)
static int irBit = 2;

static void refreshDeviceRegisters(i2c_device_t *d) {
  if (d->kind == DEV_MPU6050) {
    // GYRO_CONFIG (0x1B) FS_SEL picks +-250/500/1000/2000 deg/s; clips at full scale like the chip.
    double counts = gyroDps * (131.0 / (1 << ((d->regs[0x1B] >> 3) & 3))) + 37;   // +37 counts of offset, like a real gyro
    int16_t raw = (int16_t)(counts > 32767 ? 32767 : counts < -32768 ? -32768 : counts);
    d->regs[0x75] = 0x68;
    d->regs[0x47] = (uint8_t)(raw >> 8);
    d->regs[0x48] = (uint8_t)(raw & 0xFF);
    // Accelerometer, ACCEL_CONFIG (0x1C) AFS_SEL +-2/4/8/16 g: X forward, Y right, Z up (+1 g).
    double perG = 16384.0 / (1 << ((d->regs[0x1C] >> 3) & 3));
    double axes[3] = { accelForward + (joltLeftMs > 0 ? joltMms2 : 0), accelRight, 9806.65 * gravityG };
    for (int i = 0; i < 3; i++) {
      double c = axes[i] / 9806.65 * perG + (i == 0 ? 120 : i == 1 ? -80 : 0);   // offsets, like a real chip
      int16_t a = (int16_t)(c > 32767 ? 32767 : c < -32768 ? -32768 : c);
      d->regs[0x3B + 2 * i] = (uint8_t)(a >> 8);
      d->regs[0x3C + 2 * i] = (uint8_t)(a & 0xFF);
    }
  } else if (d->kind == DEV_BNO055) {
    int16_t sixteenths = (int16_t)(headingDeg * 16.0);
    d->regs[0x00] = 0xA0;
    d->regs[0x1A] = (uint8_t)(sixteenths & 0xFF);
    d->regs[0x1B] = (uint8_t)(sixteenths >> 8);
    d->regs[0x35] = 0xFF;
  } else if (d->kind == DEV_VL53L0X) {
    d->regs[0xC0] = 0xEE;                 // model id
    d->regs[0x83] |= 0x10;                // SPAD info ready at once
    d->regs[0x92] = 0x85;                 // 5 aperture SPADs
    d->regs[0x13] = 0x07;                 // a reading (or calibration) is always ready
    d->regs[0x1E] = (uint8_t)(tofRangeMm >> 8);
    d->regs[0x1F] = (uint8_t)(tofRangeMm & 0xFF);
  }
}

static void deviceWrite(i2c_device_t *d, uint8_t byte) {
  if (d->index == 0) {
    d->pointer = byte;
  } else if (d->kind == DEV_ADS1115) {
    if (d->pointer == 0x01 && d->index == 1) d->config = (uint16_t)(byte << 8);
    if (d->pointer == 0x01 && d->index == 2) {
      d->config |= byte;
      int mux = (d->config >> 12) & 7;
      if ((d->config & 0x8000) && mux >= 4) {
        double counts = d->inputMv[mux - 4] / 0.1875;   // +-6.144 V range
        d->conversion = (int16_t)(counts > 32767 ? 32767 : counts);
      }
    }
  } else {
    d->regs[d->pointer++] = byte;
  }
  d->index++;
}

static uint8_t deviceRead(i2c_device_t *d) {
  if (d->kind == DEV_ADS1115) {
    uint16_t value = d->pointer == 0x00 ? (uint16_t)d->conversion : (uint16_t)(d->config | 0x8000);
    return d->readIndex++ == 0 ? (uint8_t)(value >> 8) : (uint8_t)(value & 0xFF);
  }
  refreshDeviceRegisters(d);
  return d->regs[d->pointer++];
}

static void onTwiOutput(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)irq; (void)param;
  avr_twi_msg_irq_t v;
  v.u.v = value;
  avr_irq_t *reply = avr_io_getirq(avr, AVR_IOCTL_TWI_GETIRQ(0), TWI_IRQ_INPUT);
  if (v.u.twi.msg & TWI_COND_STOP) {
    selectedDevice = NULL;
  }
  if (v.u.twi.msg & TWI_COND_START) {
    selectedDevice = NULL;
    for (int i = 0; i < deviceCount; i++) {
      if (devices[i].address == (v.u.twi.addr >> 1)) {
        selectedDevice = &devices[i];
        selectedDevice->index = 0;
        selectedDevice->readIndex = 0;
        avr_raise_irq(reply, avr_twi_irq_msg(TWI_COND_ACK, v.u.twi.addr, 1));
      }
    }
  }
  if (selectedDevice) {
    if (v.u.twi.msg & TWI_COND_WRITE) {
      avr_raise_irq(reply, avr_twi_irq_msg(TWI_COND_ACK, v.u.twi.addr, 1));
      deviceWrite(selectedDevice, v.u.twi.data);
    }
    if (v.u.twi.msg & TWI_COND_READ) {
      avr_raise_irq(reply, avr_twi_irq_msg(TWI_COND_READ, v.u.twi.addr, deviceRead(selectedDevice)));
    }
  }
}

static void addDevice(int kind, uint8_t address) {
  if (deviceCount >= 8) return;
  memset(&devices[deviceCount], 0, sizeof(i2c_device_t));
  devices[deviceCount].kind = kind;
  devices[deviceCount].address = address;
  refreshDeviceRegisters(&devices[deviceCount]);
  deviceCount++;
}

// Control lines shared by script and live mode. Returns 1 if handled.
static int handleDeviceCommand(const char *line) {
  int n, ch;
  double mv;
  if (sscanf(line, "dev ads %d", &n) == 1) { addDevice(DEV_ADS1115, (uint8_t)(0x48 + n)); return 1; }
  if (strcmp(line, "dev mpu") == 0) { addDevice(DEV_MPU6050, 0x68); return 1; }
  if (strcmp(line, "dev bno") == 0) { addDevice(DEV_BNO055, 0x28); return 1; }
  if (sscanf(line, "spinmodel %lf %lf", &mv, &spinModelTauMs) == 2) {
    spinModelMaxDps = mv; spinModelRate = 0; spinModelStall = 0;
    sscanf(line, "spinmodel %*f %*f %lf", &spinModelStall);
    if (mv <= 0) gyroDps = 0;
    return 1;
  }
  if (sscanf(line, "strafemodel %lf %lf %lf", &strafeStall, &slideMaxMmps, &slideTauMs) == 3) {
    slideRight = slideForward = accelRight = accelForward = 0;
    return 1;
  }
  if (sscanf(line, "jolt %lf", &mv) == 1) { joltMms2 = mv * 9806.65; joltLeftMs = 60; return 1; }
  if (sscanf(line, "spike %lf", &mv) == 1) { joltMms2 = mv * 9806.65; joltLeftMs = 20; return 1; }
  if (sscanf(line, "gravity %lf", &mv) == 1) { gravityG = mv; return 1; }
  if (sscanf(line, "sonar %d", &n) == 1) { sonarMm = n; return 1; }
  char port;
  if (sscanf(line, "irpin %c %d", &port, &n) == 2 && port >= 'A' && port <= 'D' && n >= 0 && n < 8) {
    irPort = port;
    irBit = n;
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(irPort), irBit), 1);
    return 1;
  }
  if (strncmp(line, "wheels ", 7) == 0) {
    double v[8] = { 1, 1, 1, 1, 0, 0, 0, 0 };
    sscanf(line + 7, "%lf %lf %lf %lf %lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    for (int i = 0; i < 4; i++) { wheelStrength[i] = v[i]; wheelStallOffset[i] = v[4 + i]; }
    return 1;
  }
  if (strcmp(line, "dev tof") == 0) { addDevice(DEV_VL53L0X, 0x29); return 1; }
  if (sscanf(line, "tof %d", &n) == 1) { tofRangeMm = n; return 1; }
  if (strcmp(line, "dev clear") == 0) { deviceCount = 0; selectedDevice = NULL; return 1; }
  if (sscanf(line, "ads %d %d %lf", &n, &ch, &mv) == 3) {
    for (int i = 0; i < deviceCount; i++) {
      if (devices[i].kind == DEV_ADS1115 && devices[i].address == 0x48 + n && ch >= 0 && ch < 4) devices[i].inputMv[ch] = mv;
    }
    return 1;
  }
  if (strncmp(line, "yaw", 3) == 0) {
    if (strcmp(line, "yaw 0") == 0) simYawDeg = 0;
    printf("sim yaw %.2f peak %.2f\n", simYawDeg, simYawPeakDeg);
    fflush(stdout);
    simYawPeakDeg = fabs(simYawDeg);
    return 1;
  }
  if (sscanf(line, "gyro %lf", &mv) == 1) { gyroDps = mv; return 1; }
  if (sscanf(line, "heading %lf", &mv) == 1) { headingDeg = mv; return 1; }
  return 0;
}

// M1-M4 duty as the shield sees it: OCR2A OCR2B OCR0A OCR0B, 0 while that
// compare output is disconnected from its pin.
static void printPwm(void) {
  uint8_t t2 = avr->data[0xB0], t0 = avr->data[0x44];
  printf("\n[pwm %d %d %d %d]\n", (t2 & 0x80) ? avr->data[0xB3] : 0, (t2 & 0x20) ? avr->data[0xB4] : 0,
         (t0 & 0x80) ? avr->data[0x47] : 0, (t0 & 0x20) ? avr->data[0x48] : 0);
  fflush(stdout);
}

static void onUartOutput(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)irq; (void)param;
  putchar((int)value);
  fflush(stdout);
}

static void runMs(double ms);

// One 9600-baud 8N1 frame on A0 (PC0): start bit, 8 data bits LSB first, stop.
static void sendSoftSerialByte(uint8_t byte) {
  avr_irq_t *pin = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 0);
  double bitMs = 1000.0 / 9600.0;
  avr_raise_irq(pin, 0);
  runMs(bitMs);
  for (int bit = 0; bit < 8; bit++) {
    avr_raise_irq(pin, (byte >> bit) & 1);
    runMs(bitMs);
  }
  avr_raise_irq(pin, 1);
  runMs(bitMs);
}

// The IR receiver's output (D2 = PD2 unless irpin says otherwise): low during
// each 38 kHz burst.
static double irLevel(int level, double us) {
  avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(irPort), irBit), level);
  runMs(us / 1000.0);
  return us / 1000.0;
}

// One NEC frame for command byte (address 0), then a repeat frame every
// 108 ms until holdMs has passed. Returns the simulated ms it took.
static double sendNec(int command, double holdMs) {
  uint32_t frame = 0x00u | (0xFFu << 8) | ((uint32_t)command << 16) | ((uint32_t)(~command & 0xFF) << 24);
  double spent = irLevel(0, 9000) + irLevel(1, 4500);
  for (int bit = 0; bit < 32; bit++) {
    spent += irLevel(0, 562);
    spent += irLevel(1, (frame >> bit) & 1 ? 1687 : 562);
  }
  spent += irLevel(0, 562);
  spent += irLevel(1, 108000 - spent * 1000 > 0 ? 108000 - spent * 1000 : 0);
  while (spent + 108 <= holdMs + 1) {
    spent += irLevel(0, 9000) + irLevel(1, 2250) + irLevel(0, 562);
    spent += irLevel(1, 108000 - 11812);
  }
  return spent;
}

// HC-SR04: when the trig pin (A3 = PC3) falls, raise echo (A2 = PC2) after
// 0.5 ms for as long as the sound takes there and back.
static avr_cycle_count_t sonarEchoLevel(avr_t *mcu, avr_cycle_count_t when, void *param) {
  (void)when;
  avr_raise_irq(avr_io_getirq(mcu, AVR_IOCTL_IOPORT_GETIRQ('C'), 2), param != NULL);
  return 0;
}

static void onSonarTrig(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)param;
  if (irq->value && !value && sonarMm > 0) {
    avr_cycle_timer_register_usec(avr, 500, sonarEchoLevel, (void *)1);
    avr_cycle_timer_register_usec(avr, 500 + (uint32_t)(sonarMm * 2000.0 / 343.0), sonarEchoLevel, NULL);
  }
}

// The 74HC595 on the shield: DATA D8 (PB0), CLOCK D4 (PD4), LATCH D12 (PB4).
static void onShiftClock(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)param;
  if (!irq->value && value) shiftBits = (uint8_t)((shiftBits << 1) | (avr->data[0x25] & 1));   // PORTB bit 0
}
static void onShiftLatch(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)param;
  if (!irq->value && value) shiftLatched = shiftBits;
}

// Shield channel M1..M4: duty and the 74HC595 forward / reverse bits.
static const int channelForwardBit[4] = { 2, 1, 5, 0 }, channelReverseBit[4] = { 3, 4, 7, 6 };
// The sketch's default wiring: FL <- M4, FR <- M3, RL <- M1, RR <- M2.
static const int wheelChannel[4] = { 3, 2, 0, 1 };

static double channelDuty(int c) {
  uint8_t t2 = avr->data[0xB0], t0 = avr->data[0x44];
  switch (c) {
    case 0: return (t2 & 0x80) ? avr->data[0xB3] : 0;
    case 1: return (t2 & 0x20) ? avr->data[0xB4] : 0;
    case 2: return (t0 & 0x80) ? avr->data[0x47] : 0;
    default: return (t0 & 0x20) ? avr->data[0x48] : 0;
  }
}

// One wheel's speed as a share of full, signed (forward +), if its duty
// beats stall (plus its own offset).
static double wheelShareAt(int w, double stall) {
  int c = wheelChannel[w];
  double duty = channelDuty(c);
  int forward = (shiftLatched >> channelForwardBit[c]) & 1, reverse = (shiftLatched >> channelReverseBit[c]) & 1;
  if (forward == reverse || duty <= 40 || duty < stall + wheelStallOffset[w]) return 0;
  return (forward ? 1 : -1) * wheelStrength[w] * sqrt((duty - 40) / 215.0);
}
static double wheelShare(int w) { return wheelShareAt(w, spinModelStall); }

static int anyMotorOn(void) {
  for (int c = 0; c < 4; c++) if (channelDuty(c) > 0) return 1;
  return 0;
}

static void stepSlideModel(double ms) {
  if (slideMaxMmps <= 0) return;
  // Strafe right = FL+ FR- RL- RR+. It slides only if every wheel beats the strafe stall.
  double s[4], targetRight = 0, targetForward = 0;
  int all = 1;
  for (int w = 0; w < 4; w++) { s[w] = wheelShareAt(w, strafeStall); if (s[w] == 0) all = 0; }
  double strafe = (s[0] - s[1] - s[2] + s[3]) / 4;
  if (all && fabs(strafe) > fabs(s[0] + s[1] + s[2] + s[3]) / 4) targetRight = slideMaxMmps * strafe;
  else targetForward = slideMaxMmps * (wheelShare(0) + wheelShare(1) + wheelShare(2) + wheelShare(3)) / 4;
  double k = 1 - exp(-ms / slideTauMs);
  double dRight = (targetRight - slideRight) * k, dForward = (targetForward - slideForward) * k;
  slideRight += dRight;
  slideForward += dForward;
  // Motors on: vibration (+-0.08 g, zero mean), whether it moves or only hums.
  double shake = anyMotorOn() ? 0.08 * 9806.65 : 0.003 * 9806.65;
  accelRight = dRight * 1000.0 / ms + shake * (2.0 * rand() / RAND_MAX - 1);
  accelForward = dForward * 1000.0 / ms + shake * (2.0 * rand() / RAND_MAX - 1);
}

static void stepSpinModel(double ms) {
  if (spinModelMaxDps <= 0) return;
  double target = spinModelMaxDps * (wheelShare(0) - wheelShare(1) + wheelShare(2) - wheelShare(3)) / 4;
  spinModelRate += (target - spinModelRate) * (1 - exp(-ms / spinModelTauMs));
  simYawDeg += spinModelRate * ms / 1000.0;
  if (fabs(simYawDeg) > simYawPeakDeg) simYawPeakDeg = fabs(simYawDeg);
  gyroDps = -spinModelRate;   // clockwise reads negative on the chip
}

static void runMs(double ms) {
  // In 1 ms slices so the spin model follows the motors.
  while (ms > 0) {
    double slice = ms > 1.0 ? 1.0 : ms;
    avr_cycle_count_t target = avr->cycle + (avr_cycle_count_t)(slice * 16000.0);
    while (avr->cycle < target) {
      int state = avr_run(avr);
      if (state == cpu_Done || state == cpu_Crashed) {
        fprintf(stderr, "cpu stopped (%d)\n", state);
        exit(1);
      }
    }
    stepSpinModel(slice);
    stepSlideModel(slice);
    if (joltLeftMs > 0) joltLeftMs -= slice;
    ms -= slice;
  }
}

static void boot(void) {
  avr = avr_make_mcu_by_name("atmega328p");
  avr_init(avr);
  avr->frequency = 16000000;
  avr_load_firmware(avr, &firmware);
  uint32_t flags = 0;
  avr_ioctl(avr, AVR_IOCTL_UART_GET_FLAGS('0'), &flags);
  flags &= ~AVR_UART_FLAG_STDIO;
  avr_ioctl(avr, AVR_IOCTL_UART_SET_FLAGS('0'), &flags);
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT), onUartOutput, NULL);
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_TWI_GETIRQ(0), TWI_IRQ_OUTPUT), onTwiOutput, NULL);
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 3), onSonarTrig, NULL);
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), 4), onShiftClock, NULL);
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 4), onShiftLatch, NULL);
  selectedDevice = NULL;
  if (haveEeprom) {
    avr_eeprom_desc_t desc = { .ee = eepromCopy, .offset = 0, .size = 1024 };
    avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &desc);
  }
  for (int ch = 0; ch < 6; ch++) {
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + ch), adcMillivolts[ch]);
  }
  avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 0), 1);   // Bluetooth line idles high
  avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(irPort), irBit), 1);   // so does the IR receiver
}

static void saveEeprom(void) {
  avr_eeprom_desc_t desc = { .ee = NULL, .offset = 0, .size = 1024 };
  avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &desc);
  if (desc.ee) {
    memcpy(eepromCopy, desc.ee, 1024);
    haveEeprom = 1;
  }
}

static double nowMs(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// live mode: stdin bytes go to UART0 in real time; a line starting with
// byte 0x01 is a control line instead ("adc <ch> <mv>" or "reboot").
static int liveBluetooth = 0;

static int runLive(void) {
  fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
  avr_irq_t *input = avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT);
  unsigned char pending[4096];
  int pendingLength = 0;
  char control[128];
  int controlLength = -1;
  double start = nowMs();
  double simulatedMs = 0;
  while (1) {
    unsigned char buffer[256];
    ssize_t got = read(0, buffer, sizeof buffer);
    if (got == 0) return 0;
    for (ssize_t i = 0; i < got; i++) {
      if (controlLength >= 0) {
        if (buffer[i] == '\n') {
          control[controlLength] = 0;
          int ch, mv;
          double holdMs = 0;
          if (sscanf(control, "ir %x %lf", &ch, &holdMs) >= 1 && strncmp(control, "ir ", 3) == 0) {
            simulatedMs += sendNec(ch, holdMs);
          } else if (sscanf(control, "adc %d %d", &ch, &mv) == 2 && ch >= 0 && ch < 6) {
            adcMillivolts[ch] = mv;
            avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + ch), mv);
          } else if (strcmp(control, "pwm") == 0) {
            printPwm();
          } else if (handleDeviceCommand(control)) {
            // handled
          } else if (strcmp(control, "reboot") == 0 || strncmp(control, "flash ", 6) == 0) {
            saveEeprom();
            if (control[0] == 'f') {
              if (elf_read_firmware(control + 6, &firmware)) { fprintf(stderr, "cannot read %s\n", control + 6); exit(1); }
              strcpy(firmware.mmcu, "atmega328p");
              firmware.frequency = 16000000;
            }
            boot();
            input = avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT);
          }
          controlLength = -1;
        } else if (controlLength < 127) {
          control[controlLength++] = buffer[i];
        }
      } else if (buffer[i] == 1) {
        controlLength = 0;
      } else if (pendingLength < (int)sizeof pending) {
        pending[pendingLength++] = buffer[i];
      }
    }
    // one byte per 1.1 ms of simulated time (9600 baud), sim paced to wall clock
    if (pendingLength > 0 && liveBluetooth) {
      sendSoftSerialByte(pending[0]);
      memmove(pending, pending + 1, --pendingLength);
      simulatedMs += 10000.0 / 9600.0;
    } else {
      if (pendingLength > 0) {
        avr_raise_irq(input, pending[0]);
        memmove(pending, pending + 1, --pendingLength);
      }
      runMs(1.1);
      simulatedMs += 1.1;
    }
    double ahead = simulatedMs - (nowMs() - start);
    if (ahead > 2) usleep((useconds_t)(ahead * 1000));
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: uno-sim firmware.elf < script\n");
    return 2;
  }
  if (elf_read_firmware(argv[1], &firmware)) return 1;
  strcpy(firmware.mmcu, "atmega328p");
  firmware.frequency = 16000000;
  boot();
  if (argc > 2 && strcmp(argv[2], "live") == 0) {
    liveBluetooth = argc > 3 && strcmp(argv[3], "bt") == 0;
    return runLive();
  }
  char line[512];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\n")] = 0;
    if (strncmp(line, "send ", 5) == 0) {
      avr_irq_t *input = avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT);
      for (char *c = line + 5; *c; c++) {
        char byte = *c;
        if (byte == '\\' && c[1] == 'n') { byte = '\n'; c++; }
        avr_raise_irq(input, (uint8_t)byte);
        runMs(1.1);
      }
    } else if (strncmp(line, "btsend ", 7) == 0) {
      for (char *c = line + 7; *c; c++) {
        sendSoftSerialByte((uint8_t)*c);
      }
    } else if (strncmp(line, "ir ", 3) == 0) {
      int command = 0;
      double holdMs = 0;
      sscanf(line + 3, "%x %lf", &command, &holdMs);
      sendNec(command, holdMs);
    } else if (strncmp(line, "wait ", 5) == 0) {
      runMs(atof(line + 5));
    } else if (strncmp(line, "adc ", 4) == 0) {
      int ch = 0, mv = 0;
      sscanf(line + 4, "%d %d", &ch, &mv);
      adcMillivolts[ch] = mv;
      avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + ch), mv);
    } else if (strcmp(line, "reboot") == 0 || strncmp(line, "flash ", 6) == 0) {
      saveEeprom();
      if (line[0] == 'f') {   // flash <elf>: another sketch, EEPROM kept (like an upload)
        if (elf_read_firmware(line + 6, &firmware)) { fprintf(stderr, "cannot read %s\n", line + 6); exit(1); }
        strcpy(firmware.mmcu, "atmega328p");
        firmware.frequency = 16000000;
      }
      printf("\n===== reboot =====\n");
      boot();
    } else if (strcmp(line, "pwm") == 0) {
      printPwm();
    } else if (handleDeviceCommand(line)) {
      // handled
    } else if (line[0] && line[0] != '#') {
      printf("\n[script: %s]\n", line);
    }
  }
  return 0;
}
