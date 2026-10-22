"use strict";

// ========================================================================
// Shared constants, connection state, and element handles. Loaded first.
// ========================================================================

var APP_VERSION = "0.2.2";

var KERNEL_COMPAT = "0.1.13";

var BLE_SERVICE_UUID = 0xFFE0;

var BLE_CHARACTERISTIC_UUID = 0xFFE1;

var MAX_MOTOR_PWM = 255;

var JOYSTICK_SEND_INTERVAL_MS = 90;

var BLE_CHUNK_SIZE = 20;

var BLE_CHUNK_GAP_MS = 30;

var bluetoothDevice = null;

var serialCharacteristic = null;

var writeChain = Promise.resolve();

var connectButton = document.getElementById("connectButton");

var statusDot = document.getElementById("statusDot");

var statusText = document.getElementById("statusText");

var consoleElement = document.getElementById("console");

var joystickZone = document.getElementById("joystickZone");

var joystickKnob = document.getElementById("joystickKnob");

var leftReadout = document.getElementById("leftReadout");

var rightReadout = document.getElementById("rightReadout");

var speedSlider = document.getElementById("speedSlider");

var speedValueLabel = document.getElementById("speedValueLabel");

var servoSlider = document.getElementById("servoSlider");

var servoValueLabel = document.getElementById("servoValueLabel");

var stopButton = document.getElementById("stopButton");

var unsupportedNotice = document.getElementById("unsupportedNotice");

// (programText lives in program.js, which owns the program view)
