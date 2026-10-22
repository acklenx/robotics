"use strict";

// ========================================================================
// Driving: joystick, D-pad, mode buttons with retry, speed/servo sliders, tilt drive.
// ========================================================================

// ---------------------------------------------------------------------------
// Joystick
// ---------------------------------------------------------------------------
var joystickActive = false;

var lastJoystickSendTime = 0;

var lastSentLeft = 0;

var lastSentRight = 0;

function moveKnob(offsetX, offsetY) {
  joystickKnob.style.transform =
    "translate(calc(-50% + " + offsetX + "px), calc(-50% + " + offsetY + "px))";
}

function computeMotorValues(normalizedX, normalizedY) {
  var forward = -normalizedY;
  var turn = normalizedX;
  var leftValue = Math.round((forward + turn) * MAX_MOTOR_PWM);
  var rightValue = Math.round((forward - turn) * MAX_MOTOR_PWM);
  leftValue = Math.max(-MAX_MOTOR_PWM, Math.min(MAX_MOTOR_PWM, leftValue));
  rightValue = Math.max(-MAX_MOTOR_PWM, Math.min(MAX_MOTOR_PWM, rightValue));
  return { left: leftValue, right: rightValue };
}

function sendMotorValues(leftValue, rightValue, forceSend) {
  var now = Date.now();
  var changed = leftValue !== lastSentLeft || rightValue !== lastSentRight;
  if (!forceSend && (!changed || now - lastJoystickSendTime < JOYSTICK_SEND_INTERVAL_MS)) {
    return;
  }
  lastJoystickSendTime = now;
  lastSentLeft = leftValue;
  lastSentRight = rightValue;
  leftReadout.textContent = leftValue;
  rightReadout.textContent = rightValue;
  sendCommand("M" + leftValue + "," + rightValue + "\n");
}

function handleJoystickPointer(event) {
  var rect = joystickZone.getBoundingClientRect();
  var centerX = rect.left + rect.width / 2;
  var centerY = rect.top + rect.height / 2;
  var radius = rect.width / 2 - 36;

  var deltaX = event.clientX - centerX;
  var deltaY = event.clientY - centerY;
  var distance = Math.sqrt(deltaX * deltaX + deltaY * deltaY);
  if (distance > radius) {
    deltaX = deltaX * radius / distance;
    deltaY = deltaY * radius / distance;
  }

  moveKnob(deltaX, deltaY);

  var motors = computeMotorValues(deltaX / radius, deltaY / radius);
  sendMotorValues(motors.left, motors.right, false);
}

function releaseJoystick() {
  if (!joystickActive) {
    return;
  }
  joystickActive = false;
  moveKnob(0, 0);
  sendMotorValues(0, 0, true);
  sendStopInsurance();
  leftReadout.textContent = "0";
  rightReadout.textContent = "0";
}

joystickZone.addEventListener("pointerdown", function (event) {
  joystickActive = true;
  joystickZone.setPointerCapture(event.pointerId);
  handleJoystickPointer(event);
});

joystickZone.addEventListener("pointermove", function (event) {
  if (joystickActive) {
    handleJoystickPointer(event);
  }
});

joystickZone.addEventListener("pointerup", releaseJoystick);

joystickZone.addEventListener("pointercancel", releaseJoystick);

// ---------------------------------------------------------------------------
// D-pad, modes, sliders
// ---------------------------------------------------------------------------
function wireDrivePadButton(button) {
  var holdRepeatTimer = null;
  function pressPad(event) {
    event.preventDefault();
    button.classList.add("pressed");
    button.setPointerCapture(event.pointerId);
    sendCommand(button.getAttribute("data-drive"));
    clearActiveModeHighlight();
    // The kernel's dead-man expires manual drives after 400ms; while the
    // button is held, keep refreshing so the hold stays alive.
    holdRepeatTimer = setInterval(function () {
      sendCommand(button.getAttribute("data-drive"));
    }, 250);
  }
  function releasePad() {
    if (holdRepeatTimer) {
      clearInterval(holdRepeatTimer);
      holdRepeatTimer = null;
    }
    button.classList.remove("pressed");
    sendStopInsurance();
  }
  button.addEventListener("pointerdown", pressPad);
  button.addEventListener("pointerup", releasePad);
  button.addEventListener("pointercancel", releasePad);
}

var drivePadButtons = document.querySelectorAll("[data-drive]");

for (var padIndex = 0; padIndex < drivePadButtons.length; padIndex++) {
  wireDrivePadButton(drivePadButtons[padIndex]);
}

var modeButtons = document.querySelectorAll("[data-mode]");

function clearActiveModeHighlight() {
  for (var i = 0; i < modeButtons.length; i++) {
    modeButtons[i].classList.remove("active");
  }
}

// Half-duplex reality: a single command byte can be destroyed by an
// outgoing telemetry line. Mode commands are idempotent, so if telemetry
// doesn't confirm the mode within 700ms, just ask again.
var pendingModeCommand = null;

var pendingModeAttempts = 0;

var pendingModeTimer = null;

function clearPendingMode() {
  if (pendingModeTimer) {
    clearTimeout(pendingModeTimer);
    pendingModeTimer = null;
  }
  pendingModeCommand = null;
  pendingModeAttempts = 0;
}

function sendModeCommandWithRetry(commandLetter) {
  pendingModeCommand = commandLetter;
  pendingModeAttempts = pendingModeAttempts + 1;
  sendCommand(commandLetter);
  if (pendingModeTimer) {
    clearTimeout(pendingModeTimer);
  }
  pendingModeTimer = setTimeout(function () {
    if (!pendingModeCommand) {
      return;
    }
    if (pendingModeAttempts < 3) {
      logLine("No mode confirmation yet \u2014 resending " + pendingModeCommand +
        " (try " + (pendingModeAttempts + 1) + " of 3)", "err");
      sendModeCommandWithRetry(pendingModeCommand);
    } else {
      logLine("Robot never confirmed mode " + pendingModeCommand +
        " after 3 tries. Check the connection.", "err");
      clearPendingMode();
    }
  }, 700);
}

function noteModeConfirmed(mode) {
  if (pendingModeCommand && COMMAND_TO_MODE[pendingModeCommand] === mode) {
    logLine("Robot confirmed mode: " + (MODE_NAMES[mode] || mode));
    clearPendingMode();
  }
}

function wireModeButton(button) {
  button.addEventListener("click", function () {
    // Reveal this mode's sensor UI right away — even disconnected the
    // layout appears (with placeholder values); the mode button itself
    // only locks on when the robot's telemetry confirms the mode.
    var previewMode = COMMAND_TO_MODE[button.getAttribute("data-mode")];
    if (previewMode !== undefined) {
      applyTelemetryLayout(previewMode);
    }
    pendingModeAttempts = 0;
    sendModeCommandWithRetry(button.getAttribute("data-mode"));
  });
}

for (var modeIndex = 0; modeIndex < modeButtons.length; modeIndex++) {
  wireModeButton(modeButtons[modeIndex]);
}

stopButton.addEventListener("click", function () {
  clearPendingMode(); // never let a retry restart a mode after STOP
  clearActiveModeHighlight();
  sendStopInsurance();
  setTimeout(flushBatteryWarningIfSafe, 400); // after STOP is always safe
});

speedSlider.addEventListener("input", function () {
  speedValueLabel.textContent = speedSlider.value;
  sendCommand(speedSlider.value);
});

function setServoAngleUi(angle) {
  angle = Math.max(0, Math.min(180, Math.round(angle)));
  servoSlider.value = angle;
  servoValueLabel.innerHTML = angle + "&deg;";
  sendCommand("E" + angle + "\n");
  return angle;
}

servoSlider.addEventListener("input", function () {
  setServoAngleUi(Number(servoSlider.value));
});

document.getElementById("servoCenterButton").addEventListener("click", function () {
  setServoAngleUi(90);
});

// ---------------------------------------------------------------------------
// Tilt drive — phone orientation streams M commands while the button is held
// ---------------------------------------------------------------------------
var TILT_DEADZONE_DEGREES = 5;

var TILT_FULL_SCALE_DEGREES = 28;

var tiltHoldButton = document.getElementById("tiltHoldButton");

var tiltDriveActive = false;

var tiltNeutralCaptured = false;

var tiltNeutralBeta = 0;

var tiltNeutralGamma = 0;

var tiltPermissionGranted = false;

function scaleTiltAxis(deltaDegrees) {
  var magnitude = Math.abs(deltaDegrees);
  if (magnitude < TILT_DEADZONE_DEGREES) {
    return 0;
  }
  var scaled = (magnitude - TILT_DEADZONE_DEGREES) / (TILT_FULL_SCALE_DEGREES - TILT_DEADZONE_DEGREES);
  scaled = Math.min(1, scaled);
  return deltaDegrees < 0 ? -scaled : scaled;
}

function handleDeviceOrientation(event) {
  if (!tiltDriveActive) {
    return;
  }
  if (event.beta === null || event.gamma === null) {
    return;
  }
  if (!tiltNeutralCaptured) {
    tiltNeutralBeta = event.beta;
    tiltNeutralGamma = event.gamma;
    tiltNeutralCaptured = true;
    return;
  }
  var forwardAmount = scaleTiltAxis(tiltNeutralBeta - event.beta); // tilt top edge away = forward
  var turnAmount = scaleTiltAxis(event.gamma - tiltNeutralGamma);  // tilt right = turn right
  var motors = computeMotorValues(turnAmount, -forwardAmount);
  sendMotorValues(motors.left, motors.right, false);
}

function beginTiltDrive(event) {
  event.preventDefault();
  tiltHoldButton.setPointerCapture(event.pointerId);

  function activate() {
    tiltPermissionGranted = true;
    tiltDriveActive = true;
    tiltNeutralCaptured = false;
    tiltHoldButton.classList.add("holding");
    clearActiveModeHighlight();
  }

  if (!tiltPermissionGranted &&
      typeof DeviceOrientationEvent !== "undefined" &&
      typeof DeviceOrientationEvent.requestPermission === "function") {
    DeviceOrientationEvent.requestPermission().then(function (state) {
      if (state === "granted") {
        activate();
      } else {
        logLine("Motion sensor permission denied.", "err");
      }
    }).catch(function (error) {
      logLine("Motion sensors unavailable: " + error.message, "err");
    });
  } else if (typeof DeviceOrientationEvent === "undefined") {
    logLine("No motion sensors on this device.", "err");
  } else {
    activate();
  }
}

function endTiltDrive() {
  if (!tiltDriveActive) {
    tiltHoldButton.classList.remove("holding");
    return;
  }
  tiltDriveActive = false;
  tiltHoldButton.classList.remove("holding");
  sendMotorValues(0, 0, true);
  sendStopInsurance();
  leftReadout.textContent = "0";
  rightReadout.textContent = "0";
}

tiltHoldButton.addEventListener("pointerdown", beginTiltDrive);

tiltHoldButton.addEventListener("pointerup", endTiltDrive);

tiltHoldButton.addEventListener("pointercancel", endTiltDrive);

window.addEventListener("deviceorientation", handleDeviceOrientation);
