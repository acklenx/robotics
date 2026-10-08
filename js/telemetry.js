"use strict";

// ========================================================================
// Live telemetry: sensor displays, motor bars, plain-language analysis.
// ========================================================================

// ---------------------------------------------------------------------------
// Live telemetry rendering (kernel streams: T,mode,lL,lR,pL,pR,ldrL,ldrR,front,mL,mR)
// ---------------------------------------------------------------------------
var telemetryPanel = document.getElementById("telemetryPanel");

var telemetryModeName = document.getElementById("telemetryModeName");

var lightBlock = document.getElementById("lightBlock");

var lineBlock = document.getElementById("lineBlock");

var distanceBlock = document.getElementById("distanceBlock");

var proxBlock = document.getElementById("proxBlock");

var analysisLine = document.getElementById("analysisLine");

var lastTelemetryReceived = 0;

var MODE_NAMES = { 0: "manual", 1: "line tracking", 2: "obstacle avoid", 3: "follow", 4: "light seek", 5: "program", 6: "seek/radar" };

var MODE_TO_BUTTON_COMMAND = { 1: "T", 2: "A", 3: "W", 4: "P", 6: "Y" };

function confirmActiveModeButton(mode) {
  // The robot's own telemetry decides which mode button glows: a button
  // lights up only when the robot says that mode is truly running (and it
  // lights up even when the mode was started from the IR remote).
  var confirmedCommand = MODE_TO_BUTTON_COMMAND[mode] || null;
  var i;
  for (i = 0; i < modeButtons.length; i++) {
    var matches = modeButtons[i].getAttribute("data-mode") === confirmedCommand;
    modeButtons[i].classList.toggle("active", matches);
  }
}

function setBarWidth(elementId, fraction) {
  document.getElementById(elementId).style.width = Math.round(Math.max(0, Math.min(1, fraction)) * 100) + "%";
}

function brightnessFromRaw(rawReading) {
  // These LDR dividers read LOWER with more light; flip so more light = bigger.
  var brightness = 1023 - rawReading;
  return brightness < 0 ? 0 : (brightness > 1023 ? 1023 : brightness);
}

function computeMotorFill(motorValue, fullScalePwm) {
  var scale = fullScalePwm > 0 ? fullScalePwm : 255;
  var fraction = Math.abs(motorValue) / scale;
  return {
    widthPercent: Math.round(Math.min(1, fraction) * 100),
    reverse: motorValue < 0
  };
}

function setMotorBar(elementId, motorValue, fullScalePwm) {
  var fill = document.getElementById(elementId);
  var style = computeMotorFill(motorValue, fullScalePwm);
  fill.style.width = style.widthPercent + "%";
  fill.style.background = style.reverse ? "var(--blue)" : "var(--orange)";
}

function describeSteering(motorLeft, motorRight) {
  if (motorLeft === 0 && motorRight === 0) { return "stopped"; }
  if (motorLeft > 0 && motorRight < 0) { return "spinning right"; }
  if (motorLeft < 0 && motorRight > 0) { return "spinning left"; }
  if (motorLeft < 0 && motorRight < 0) { return "backing up"; }
  var difference = motorLeft - motorRight;
  if (Math.abs(difference) < 20) { return "driving straight"; }
  return difference > 0 ? "curving right" : "curving left";
}

function analyzeMode(mode, lineL, lineR, proxL, proxR, front, motorLeft, motorRight) {
  var steering = describeSteering(motorLeft, motorRight);
  if (mode === 1) {
    var seen = (lineL === 1 ? "L" : "") + (lineR === 1 ? "R" : "");
    if (seen === "") { return "both white \u2192 " + steering; }
    if (seen === "LR") { return "both on black (crossing/end) \u2192 " + steering; }
    return "black under " + seen + " \u2192 " + steering;
  }
  if (mode === 2) {
    if (front <= 20) { return "wall at " + front + " cm \u2192 stop & scan"; }
    if (proxL === 0 && proxR === 0) { return "boxed in both sides \u2192 " + steering; }
    if (proxL === 0) { return "left prox sees \u2192 " + steering; }
    if (proxR === 0) { return "right prox sees \u2192 " + steering; }
    return "path clear \u2192 " + steering;
  }
  if (mode === 3) {
    if (front < 8) { return "too close (" + front + " cm) \u2192 " + steering; }
    if (front <= 15) { return "holding distance \u2192 " + steering; }
    if (front <= 40) { return "target at " + front + " cm \u2192 chasing"; }
    if (proxL === 0 || proxR === 0) { return "target off to the side \u2192 " + steering; }
    return "nothing in range \u2192 " + steering;
  }
  if (mode === 4) { return steering + " (toward the light)"; }
  if (mode === 6) { return "seek: " + steering + " \u2014 watch console for lock/search status"; }
  if (mode === 5) { return "program running \u2192 " + steering; }
  return steering;
}

function updateTelemetry(telemetryLine) {
  var fields = telemetryLine.split(",");
  if (fields.length < 11) { return; }
  var mode = parseInt(fields[1], 10);
  var lineL = parseInt(fields[2], 10);
  var lineR = parseInt(fields[3], 10);
  var proxL = parseInt(fields[4], 10);
  var proxR = parseInt(fields[5], 10);
  var ldrL = parseInt(fields[6], 10);
  var ldrR = parseInt(fields[7], 10);
  var front = parseInt(fields[8], 10);
  var motorLeft = parseInt(fields[9], 10);
  var motorRight = parseInt(fields[10], 10);
  var fullScalePwm = fields.length >= 12 ? parseInt(fields[11], 10) : 255;
  var servoAngle = fields.length >= 13 ? parseInt(fields[12], 10) : null;
  var railMillivolts = fields.length >= 14 ? parseInt(fields[13], 10) : null;
  var packMillivolts = fields.length >= 15 ? parseInt(fields[14], 10) : 0;

  lastTelemetryReceived = Date.now();
  lastKnownMode = mode;
  confirmActiveModeButton(mode);
  noteModeConfirmed(mode);
  applyTelemetryLayout(mode);

  var showEverything = showAllSensors;
  var showDistance = showEverything || mode === 2 || mode === 3 || mode === 6;

  if (showEverything || mode === 4) {
    var brightnessLeft = brightnessFromRaw(ldrL);
    var brightnessRight = brightnessFromRaw(ldrR);
    document.getElementById("ldrLeftValue").textContent = brightnessLeft;
    document.getElementById("ldrRightValue").textContent = brightnessRight;
    setBarWidth("ldrLeftBar", brightnessLeft / 1023);
    setBarWidth("ldrRightBar", brightnessRight / 1023);
  }
  if (showEverything || mode === 1) {
    document.getElementById("lineLeftDot").className = lineL === 1 ? "line-dot black" : "line-dot";
    document.getElementById("lineRightDot").className = lineR === 1 ? "line-dot black" : "line-dot";
  }
  if (showDistance) {
    document.getElementById("proxLeftDot").className = proxL === 0 ? "line-dot black" : "line-dot";
    document.getElementById("proxRightDot").className = proxR === 0 ? "line-dot black" : "line-dot";
  }
  if (showDistance) {
    document.getElementById("frontValue").textContent = front >= 400 ? "clear" : front;
    setBarWidth("distanceFill", front / 100);
    document.getElementById("distanceMarker").style.left = (mode === 2 ? 20 : 15) + "%";
    var proxText = (proxL === 0 ? "\u25C0 seen " : "") + (proxR === 0 ? " seen \u25B6" : "");
    document.getElementById("proxState").textContent = proxText === "" ? "prox clear" : proxText;
  }

  document.getElementById("motorLeftValue").textContent = motorLeft;
  document.getElementById("motorRightValue").textContent = motorRight;
  setMotorBar("motorLeftBar", motorLeft, fullScalePwm);
  setMotorBar("motorRightBar", motorRight, fullScalePwm);

  document.getElementById("servoReadout").innerHTML =
    servoAngle === null ? "servo &mdash;" : "servo " + servoAngle + "&deg;";

  if (railMillivolts !== null) {
    updateBatteryDisplay(railMillivolts, motorLeft === 0 && motorRight === 0);
  }
  if (packMillivolts > 0 || fields.length >= 15) {
    updatePackDisplay(packMillivolts);
  }

  analysisLine.textContent = analyzeMode(mode, lineL, lineR, proxL, proxR, front, motorLeft, motorRight);
}

setInterval(function () {
  var connected = !!serialCharacteristic;
  if (connected && telemetryPanel.style.display === "block" &&
      Date.now() - lastTelemetryReceived > 2000 && !showAllSensors) {
    telemetryPanel.style.display = "none";
  }
}, 1000);

var showAllSensors = false; // "Read sensors" toggle
var lastKnownMode = 0;

var diagButton = document.getElementById("diagButton");

var COMMAND_TO_MODE = { "T": 1, "A": 2, "W": 3, "P": 4, "Y": 6 };

function applyTelemetryLayout(mode) {
  telemetryPanel.style.display = "block";
  telemetryModeName.textContent = MODE_NAMES[mode] || "?";
  var showDistance = showAllSensors || mode === 2 || mode === 3 || mode === 6;
  lightBlock.style.display = (showAllSensors || mode === 4) ? "block" : "none";
  lineBlock.style.display = (showAllSensors || mode === 1) ? "block" : "none";
  proxBlock.style.display = showDistance ? "block" : "none";
  distanceBlock.style.display = showDistance ? "block" : "none";
}

diagButton.addEventListener("click", function () {
  showAllSensors = !showAllSensors;
  diagButton.classList.toggle("active", showAllSensors);
  applyTelemetryLayout(lastKnownMode); // reveal immediately, connected or not
  if (showAllSensors) {
    sendCommand("D");
  }
});
