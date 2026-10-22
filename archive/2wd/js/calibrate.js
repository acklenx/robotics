"use strict";

// ========================================================================
// Calibration: acceleration curve, distance, turns, drift trim, servo zero, saved robots.
// ========================================================================

// ---------------------------------------------------------------------------
// Calibration wizard — measures this robot's ms-per-room and ms-per-90deg
// ---------------------------------------------------------------------------
var ROOM_CM = 30.5;

var DEFAULT_CELL_MS = 900;

var DEFAULT_TURN_MS = 350;

var CALIBRATION_STORAGE_KEY = "mlk_robot_calibrations";

var activeCalibration = { name: null, cellMs: DEFAULT_CELL_MS, turnMs: DEFAULT_TURN_MS };

var driveTrials = [];

var spinTrials = [];

var runDriveTestButton = document.getElementById("runDriveTestButton");

var driveMeasureInput = document.getElementById("driveMeasureInput");

var addDriveTrialButton = document.getElementById("addDriveTrialButton");

var driveTrialsList = document.getElementById("driveTrialsList");

var driveResult = document.getElementById("driveResult");

var runSpinTestButton = document.getElementById("runSpinTestButton");

var spinMinutesInput = document.getElementById("spinMinutesInput");

var spinDirectionSelect = document.getElementById("spinDirectionSelect");

var addSpinTrialButton = document.getElementById("addSpinTrialButton");

var spinTrialsList = document.getElementById("spinTrialsList");

var spinResult = document.getElementById("spinResult");

var victoryLapButton = document.getElementById("victoryLapButton");

var robotNameInput = document.getElementById("robotNameInput");

var saveCalibrationButton = document.getElementById("saveCalibrationButton");

var savedRobotsList = document.getElementById("savedRobotsList");

var calibrationSummary = document.getElementById("calibrationSummary");

function meanOfNumbers(values) {
  var total = 0;
  var i;
  for (i = 0; i < values.length; i++) {
    total = total + values[i];
  }
  return total / values.length;
}

function spreadOfNumbers(values) {
  return Math.max.apply(null, values) - Math.min.apply(null, values);
}

var SHORT_RUN_SECONDS = 1;
var LONG_RUN_SECONDS = 3;

function cellMsFromDriveTrial(shortRunCm, longRunCm) {
  // Both runs start from a standstill, so both contain exactly the same
  // acceleration ramp. Subtract them and the ramp disappears, leaving pure
  // cruising distance over a known time. This is why we ask for two numbers
  // instead of one: it measures the robot's real speed rather than an
  // average of speeding-up-and-then-going.
  var cruisingCm = longRunCm - shortRunCm;
  var cruisingSeconds = LONG_RUN_SECONDS - SHORT_RUN_SECONDS;
  var centimetresPerSecond = cruisingCm / cruisingSeconds;
  return 1000 * ROOM_CM / centimetresPerSecond;
}

function turnMsFromSpinTrial(commandedTotalMs, clockMinutesOff, overshot) {
  var errorDegrees = clockMinutesOff * 6;
  var actualDegrees = overshot ? 360 + errorDegrees : 360 - errorDegrees;
  return commandedTotalMs * 90 / actualDegrees;
}

function currentCellMs() {
  if (driveTrials.length === 0) {
    return activeCalibration.cellMs;
  }
  return meanOfNumbers(driveTrials.map(function (trial) { return trial.cellMs; }));
}

function currentTurnMs() {
  if (spinTrials.length === 0) {
    return activeCalibration.turnMs;
  }
  return meanOfNumbers(spinTrials.map(function (trial) { return trial.turnMs; }));
}

function makeSquareProgram(cellMs, turnMs) {
  // Half-room sides: same math, half the wait, ends with a victory wiggle.
  var halfRoomMs = Math.round(cellMs / 2);
  return "V 5\nW 200\nP 4\nF " + halfRoomMs + "\nL " + Math.round(turnMs) +
    "\nQ\nR 120\nL 240\nR 120";
}

var runLongDriveTestButton = document.getElementById("runLongDriveTestButton");
var driveLongMeasureInput = document.getElementById("driveLongMeasureInput");

runDriveTestButton.addEventListener("click", function () {
  requestProgramRun(runDriveTestButton, "V 5\nW 500\nF " + (SHORT_RUN_SECONDS * 1000),
    "the " + SHORT_RUN_SECONDS + "-second short run");
});

runLongDriveTestButton.addEventListener("click", function () {
  requestProgramRun(runLongDriveTestButton, "V 5\nW 500\nF " + (LONG_RUN_SECONDS * 1000),
    "the " + LONG_RUN_SECONDS + "-second long run");
});

addDriveTrialButton.addEventListener("click", function () {
  var rawShortText = driveMeasureInput.value;
  var rawLongText = driveLongMeasureInput.value;
  var shortRunCm = parseDecimalInput(rawShortText);
  var longRunCm = parseDecimalInput(rawLongText);
  logLine("Add trial: short='" + rawShortText + "' long='" + rawLongText + "' \u2192 " +
    shortRunCm + " cm and " + longRunCm + " cm");

  if (String(rawShortText).trim() === "" || String(rawLongText).trim() === "" ||
      isNaN(shortRunCm) || isNaN(longRunCm)) {
    flashButtonLabel(addDriveTrialButton, "Need both runs", "flash-err");
    driveResult.textContent = "Run both tests from the same start line, then type both " +
      "distances in centimetres.";
    logLine("Not added: both the short run and the long run are needed.", "err");
    return;
  }
  if (longRunCm <= shortRunCm) {
    flashButtonLabel(addDriveTrialButton, "Long run must be further", "flash-err");
    driveResult.textContent = "The " + LONG_RUN_SECONDS + "-second run went " + longRunCm +
      " cm and the " + SHORT_RUN_SECONDS + "-second run went " + shortRunCm +
      " cm. The long one has to be further \u2014 are the numbers the right way round?";
    logLine("Not added: the long run is not longer than the short run.", "err");
    return;
  }

  var computedCellMs = cellMsFromDriveTrial(shortRunCm, longRunCm);
  var centimetresPerSecond = (longRunCm - shortRunCm) / (LONG_RUN_SECONDS - SHORT_RUN_SECONDS);
  if (computedCellMs < 50 || computedCellMs > 20000) {
    flashButtonLabel(addDriveTrialButton, "Out of range", "flash-err");
    driveResult.textContent = "That works out to " + Math.round(centimetresPerSecond) +
      " cm per second, which cannot be right. Check the measurements.";
    logLine("Not added: " + Math.round(computedCellMs) + " ms/room is not believable.", "err");
    return;
  }

  driveTrials.push({ shortCm: shortRunCm, longCm: longRunCm, cellMs: computedCellMs });
  driveMeasureInput.value = "";
  driveLongMeasureInput.value = "";
  renderDriveTrials();
  flashButtonLabel(addDriveTrialButton, "Added \u2713", "flash-ok");
  logLine("Added trial " + driveTrials.length + ": " + longRunCm + " \u2212 " + shortRunCm +
    " = " + Math.round(longRunCm - shortRunCm) + " cm of pure cruising in " +
    (LONG_RUN_SECONDS - SHORT_RUN_SECONDS) + " s \u2192 " + centimetresPerSecond.toFixed(1) +
    " cm/s \u2192 " + Math.round(computedCellMs) + " ms per 30.5 cm room. Mean is now " +
    Math.round(currentCellMs()) + " ms/room.");

  if (centimetresPerSecond > 200) {
    logLine("\u26A0 " + centimetresPerSecond.toFixed(0) + " cm/s is very fast for this robot. " +
      "If your tape reads millimetres, divide both numbers by 10.", "err");
  }
});

function renderDriveTrials() {
  driveTrialsList.innerHTML = "";
  var i;
  for (i = 0; i < driveTrials.length; i++) {
    var line = document.createElement("div");
    line.innerHTML = "trial " + (i + 1) + ": " + driveTrials[i].shortCm + " / " +
      driveTrials[i].longCm + " cm \u2192 " +
      Math.round(driveTrials[i].cellMs) + " ms/room " +
      "<span class='trial-remove' data-kind='drive' data-index='" + i + "'>\u2715</span>";
    driveTrialsList.appendChild(line);
  }
  if (driveTrials.length === 0) {
    driveResult.textContent = "One room (30.5 cm) = ? ms \u2014 run both tests, then add a trial";
    return;
  }
  var values = driveTrials.map(function (trial) { return trial.cellMs; });
  var summary = "One room = " + Math.round(meanOfNumbers(values)) + " ms";
  if (driveTrials.length > 1) {
    var spread = Math.round(spreadOfNumbers(values));
    summary = summary + "  (spread " + spread + " ms" +
      (spread > 120 ? " \u2014 inconsistent! check wheels/batteries" : "") + ")";
  }
  driveResult.textContent = summary;
}

runSpinTestButton.addEventListener("click", function () {
  var commandedTotal = Math.round(currentTurnMs() * 4);
  logLine("> spin test will command L " + commandedTotal + " (4 quarter turns at current estimate)");
  requestProgramRun(runSpinTestButton, "V 5\nW 500\nL " + commandedTotal, "the quadruple spin");
});

addSpinTrialButton.addEventListener("click", function () {
  var rawMinutesText = spinMinutesInput.value;
  var minutes = parseDecimalInput(rawMinutesText);
  logLine("Add spin trial: field='" + rawMinutesText + "' parsed as " + minutes + " clock-minutes");
  if (String(rawMinutesText).trim() === "" || isNaN(minutes) || minutes < 0 || minutes > 30) {
    flashButtonLabel(addSpinTrialButton, "Type clock-minutes first!", "flash-err");
    spinResult.textContent = "Need the miss in clock-minutes (0-30) in the box. Dead on = 0.";
    logLine("Not added: need 0-30 clock-minutes (1 minute = 6 degrees).", "err");
    return;
  }
  var commandedTotal = Math.round(currentTurnMs() * 4);
  var overshot = spinDirectionSelect.value === "over";
  var computedTurnMs = turnMsFromSpinTrial(commandedTotal, minutes, overshot);
  spinTrials.push({ minutes: minutes, overshot: overshot, turnMs: computedTurnMs });
  spinMinutesInput.value = "";
  renderSpinTrials();
  flashButtonLabel(addSpinTrialButton, "Added \u2713", "flash-ok");
  logLine("Added: " + minutes + " min " + (overshot ? "past" : "short of") + " start on a commanded " +
    commandedTotal + " ms quad-spin \u2192 " + Math.round(computedTurnMs) +
    " ms/90\u00B0. Mean is now " + Math.round(currentTurnMs()) + " ms.");
});

function renderSpinTrials() {
  spinTrialsList.innerHTML = "";
  var i;
  for (i = 0; i < spinTrials.length; i++) {
    var trial = spinTrials[i];
    var line = document.createElement("div");
    line.innerHTML = "trial " + (i + 1) + ": " + trial.minutes + " min " +
      (trial.overshot ? "over" : "short") + " \u2192 " + Math.round(trial.turnMs) + " ms/90\u00B0 " +
      "<span class='trial-remove' data-kind='spin' data-index='" + i + "'>\u2715</span>";
    spinTrialsList.appendChild(line);
  }
  if (spinTrials.length === 0) {
    spinResult.textContent = "A quarter turn = ? ms \u2014 run a trial";
    return;
  }
  spinResult.textContent = "A quarter turn = " + Math.round(currentTurnMs()) +
    " ms  (next spin test uses this)";
}

function handleTrialRemoval(event) {
  var target = event.target;
  if (!target.classList || !target.classList.contains("trial-remove")) {
    return;
  }
  var index = Number(target.getAttribute("data-index"));
  if (target.getAttribute("data-kind") === "drive") {
    driveTrials.splice(index, 1);
    renderDriveTrials();
  } else {
    spinTrials.splice(index, 1);
    renderSpinTrials();
  }
}

driveTrialsList.addEventListener("click", handleTrialRemoval);

spinTrialsList.addEventListener("click", handleTrialRemoval);

victoryLapButton.addEventListener("click", function () {
  requestProgramRun(victoryLapButton, makeSquareProgram(currentCellMs(), currentTurnMs()), "the victory lap");
  logLine("> victory lap: half-room square with YOUR numbers. Come home, robot.");
});

function loadSavedCalibrations() {
  try {
    return JSON.parse(localStorage.getItem(CALIBRATION_STORAGE_KEY)) || {};
  } catch (error) {
    return {};
  }
}

function persistSavedCalibrations(saved) {
  try {
    localStorage.setItem(CALIBRATION_STORAGE_KEY, JSON.stringify(saved));
  } catch (error) {
    logLine("Couldn't save (private browsing?). Write the numbers down!", "err");
  }
}

function updateCalibrationSummary() {
  calibrationSummary.textContent = "Active: " +
    (activeCalibration.name || "defaults") + " (" +
    Math.round(activeCalibration.cellMs) + " ms/room, " +
    Math.round(activeCalibration.turnMs) + " ms/90\u00B0)";
}

function renderSavedRobots() {
  var saved = loadSavedCalibrations();
  savedRobotsList.innerHTML = "";
  var names = Object.keys(saved).sort();
  var i;
  for (i = 0; i < names.length; i++) {
    var record = saved[names[i]];
    var line = document.createElement("div");
    line.className = "saved-robot";
    line.setAttribute("data-name", names[i]);
    line.innerHTML = "<b>" + names[i] + "</b> \u2014 " + Math.round(record.cellMs) +
      " ms/room, " + Math.round(record.turnMs) + " ms/90\u00B0 (tap to load) " +
      "<span class='trial-remove' data-name='" + names[i] + "' data-kind='robot'>\u2715</span>";
    savedRobotsList.appendChild(line);
  }
}

saveCalibrationButton.addEventListener("click", function () {
  var robotName = robotNameInput.value.trim();
  if (robotName.length === 0) {
    flashButtonLabel(saveCalibrationButton, "Name it first!", "flash-err");
    calibrationSummary.textContent = "Give the robot a name (e.g. MLK-01), then Save.";
    return;
  }
  var saved = loadSavedCalibrations();
  saved[robotName] = {
    cellMs: currentCellMs(),
    turnMs: currentTurnMs(),
    trimPercent: activeTrimPercent,
    savedAt: new Date().toISOString().slice(0, 10)
  };
  persistSavedCalibrations(saved);
  activeCalibration = { name: robotName, cellMs: currentCellMs(), turnMs: currentTurnMs() };
  updateCalibrationSummary();
  renderSavedRobots();
  flashButtonLabel(saveCalibrationButton, "Saved ✓", "flash-ok");
});

savedRobotsList.addEventListener("click", function (event) {
  var target = event.target;
  if (target.classList && target.classList.contains("trial-remove")) {
    var saved = loadSavedCalibrations();
    delete saved[target.getAttribute("data-name")];
    persistSavedCalibrations(saved);
    renderSavedRobots();
    return;
  }
  var row = target.closest ? target.closest(".saved-robot") : null;
  if (!row) {
    return;
  }
  var record = loadSavedCalibrations()[row.getAttribute("data-name")];
  if (record) {
    activeCalibration = {
      name: row.getAttribute("data-name"),
      cellMs: record.cellMs,
      turnMs: record.turnMs
    };
    activeTrimPercent = record.trimPercent || 0;
    updateDriftResult();
    if (serialCharacteristic) {
      sendCommand("J" + activeTrimPercent + "\n"); // push this robot's trim to it
    }
    driveTrials = [];
    spinTrials = [];
    renderDriveTrials();
    renderSpinTrials();
    updateCalibrationSummary();
    logLine("Loaded calibration for " + activeCalibration.name + " (trim " + activeTrimPercent + "%)");
  }
});

document.getElementById("servoNudgeLeftButton").addEventListener("click", function () {
  setServoAngleUi(Number(servoSlider.value) + 3); // logical: bigger = robot's left
});

document.getElementById("servoNudgeRightButton").addEventListener("click", function () {
  setServoAngleUi(Number(servoSlider.value) - 3);
});

document.getElementById("servoTrimCenterButton").addEventListener("click", function () {
  setServoAngleUi(90);
});

var setServoZeroButton = document.getElementById("setServoZeroButton");

var zeroArmed = false;

var zeroArmTimer = null;

setServoZeroButton.addEventListener("click", function () {
  if (!runIsPossible(setServoZeroButton)) {
    return;
  }
  sendCommand("Z"); // kernel asks for confirmation on the first Z
  if (!zeroArmed) {
    zeroArmed = true;
    setServoZeroButton.classList.add("flash-err");
    setServoZeroButton.textContent = "Press again to confirm new zero";
    zeroArmTimer = setTimeout(function () {
      zeroArmed = false;
      setServoZeroButton.classList.remove("flash-err");
      setServoZeroButton.textContent = "ZERO \u2014 save this position as the new center";
    }, 4000);
    return;
  }
  clearTimeout(zeroArmTimer);
  zeroArmed = false;
  setServoZeroButton.classList.remove("flash-err");
  setServoZeroButton.textContent = "ZERO \u2014 save this position as the new center";
  setServoAngleUi(90); // kernel now calls this position 90
  flashButtonLabel(setServoZeroButton, "New zero saved ✓", "flash-ok");
});

// --- Acceleration curve: live tuning, watched by eye ---
var rampSeedSlider = document.getElementById("rampSeedSlider");

var rampGrowthSlider = document.getElementById("rampGrowthSlider");

function estimateRampMilliseconds(seedHundredths, growthThousandths, fullScalePwm) {
  // step/ms = seed/100 + v*growth/1000  ->  t = ln(1 + v*k/s) / k
  var s = seedHundredths / 100;
  var k = growthThousandths / 1000;
  if (k <= 0) {
    return Math.round(fullScalePwm / s);
  }
  return Math.round(Math.log(1 + fullScalePwm * k / s) / k);
}

function refreshRampEstimate() {
  var seed = Number(rampSeedSlider.value);
  var growth = Number(rampGrowthSlider.value);
  document.getElementById("rampSeedLabel").textContent = seed;
  document.getElementById("rampGrowthLabel").textContent = growth;
  var milliseconds = estimateRampMilliseconds(seed, growth, 168);
  document.getElementById("rampEstimate").textContent =
    "About " + (milliseconds / 1000).toFixed(1) + " s from stop to full speed";
}

rampSeedSlider.addEventListener("input", function () {
  refreshRampEstimate();
  sendCommand("N" + rampSeedSlider.value + "\n");
});

rampGrowthSlider.addEventListener("input", function () {
  refreshRampEstimate();
  sendCommand("O" + rampGrowthSlider.value + "\n");
});

refreshRampEstimate();

document.getElementById("rampTestButton").addEventListener("click", function () {
  requestProgramRun(document.getElementById("rampTestButton"),
    "V 5\nW 500\nF 2000", "the launch test");
});

// --- Curve trim: iterate toward dead-straight; the robot stores the trim ---
var activeTrimPercent = 0;

var runDriftTestButton = document.getElementById("runDriftTestButton");

var driftMeasureInput = document.getElementById("driftMeasureInput");

var driftDirectionSelect = document.getElementById("driftDirectionSelect");

var applyDriftTrimButton = document.getElementById("applyDriftTrimButton");

var driftResult = document.getElementById("driftResult");

function updateDriftResult() {
  driftResult.textContent = "Current trim: " + activeTrimPercent + "% (+ boosts left motor)";
}

runDriftTestButton.addEventListener("click", function () {
  requestProgramRun(runDriftTestButton, "V 5\nW 500\nF 3000", "the 3-second drift test");
});

applyDriftTrimButton.addEventListener("click", function () {
  var rawDriftText = driftMeasureInput.value;
  var driftCm = parseDecimalInput(rawDriftText);
  logLine("Apply trim: field='" + rawDriftText + "' parsed as " + driftCm + " cm, direction=" +
    driftDirectionSelect.value);
  if (String(rawDriftText).trim() === "" || isNaN(driftCm) || driftCm < 0 || driftCm > 300) {
    flashButtonLabel(applyDriftTrimButton, "Type cm off the line first", "flash-err");
    logLine("Not applied: need 0-300 cm.", "err");
    return;
  }
  if (driftCm === 0) {
    flashButtonLabel(applyDriftTrimButton, "Dead straight \u2014 no change \u2713", "flash-ok");
    logLine("0 cm drift: trim stays at " + activeTrimPercent + "%. Nicely done.");
    return;
  }
  if (!serialCharacteristic) {
    flashButtonLabel(applyDriftTrimButton, "Not connected!", "flash-err");
    return;
  }
  // Drifting RIGHT means the left motor is effectively stronger: weaken it.
  var correctionStep = Math.max(1, Math.min(5, Math.round(driftCm / 5)));
  var newTrim = activeTrimPercent + (driftDirectionSelect.value === "right" ? -correctionStep : correctionStep);
  newTrim = Math.max(-15, Math.min(15, newTrim));
  sendCommand("J" + newTrim + "\n");
  logLine("Drift " + driftCm + " cm " + driftDirectionSelect.value + " \u2192 step " +
    correctionStep + "% \u2192 trim " + activeTrimPercent + "% becomes " + newTrim +
    "%. Saved on the robot. Run the 3-second test again to iterate.");
  activeTrimPercent = newTrim;
  driftMeasureInput.value = "";
  updateDriftResult();
  flashButtonLabel(applyDriftTrimButton, "Trim " + newTrim + "% saved \u2713", "flash-ok");
});

var resetCalibrationButton = document.getElementById("resetCalibrationButton");

var resetArmed = false;

var resetArmTimer = null;

resetCalibrationButton.addEventListener("click", function () {
  if (!resetArmed) {
    resetArmed = true;
    resetCalibrationButton.classList.add("flash-err");
    resetCalibrationButton.textContent = "Press again to reset everything";
    resetArmTimer = setTimeout(function () {
      resetArmed = false;
      resetCalibrationButton.classList.remove("flash-err");
      resetCalibrationButton.textContent = "Start over — clear all trials, back to defaults";
    }, 2500);
    return;
  }
  clearTimeout(resetArmTimer);
  resetArmed = false;
  resetCalibrationButton.classList.remove("flash-err");
  resetCalibrationButton.textContent = "Start over — clear all trials, back to defaults";

  driveTrials = [];
  spinTrials = [];
  activeCalibration = { name: null, cellMs: DEFAULT_CELL_MS, turnMs: DEFAULT_TURN_MS };
  renderDriveTrials();
  renderSpinTrials();
  updateCalibrationSummary();
  flashButtonLabel(resetCalibrationButton, "Reset ✓", "flash-ok");
  logLine("Calibration session reset to defaults. Saved robots untouched.");
});

updateCalibrationSummary();

renderSavedRobots();
