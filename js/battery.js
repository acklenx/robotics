"use strict";

// ========================================================================
// Battery health: 5V rail chip, pack gauge, cutoff banner, safe-moment warnings.
// ========================================================================

// ---------------------------------------------------------------------------
// Battery health. The robot reports its 5V RAIL (measured against the AVR's
// internal bandgap), not raw pack voltage — which is the more useful number:
// it's what the sensors and servo actually run on, and it sags before the
// pack looks empty. Motors dip the rail momentarily, so assessments only use
// readings taken while the wheels are stopped.
//
//   4.90 V and up   healthy    fresh cells, everything nominal
//   4.75 - 4.90 V   okay       still fine; expect this after a long session
//   4.60 - 4.75 V   low        swap cells soon: servo jitter, flaky sonar
//   below 4.60 V    dangerous  brown-out territory; readings lie, programs
//                              stall mid-run, the board may reset
// ---------------------------------------------------------------------------
var BATTERY_HEALTHY_MV = 4900;

var BATTERY_OKAY_MV = 4750;

var BATTERY_LOW_MV = 4600;

var idleRailSamples = [];

var lastAssessedLevel = null;

var lastWarnedLevel = null;

var lastWarnTime = 0;

var pendingBatteryWarning = null;

var startupBatteryCheckDone = false;

function batteryLevelFor(millivolts) {
  if (millivolts >= BATTERY_HEALTHY_MV) { return "high"; }
  if (millivolts >= BATTERY_OKAY_MV) { return "medium"; }
  if (millivolts >= BATTERY_LOW_MV) { return "low"; }
  return "danger";
}

function batteryMessageFor(level, volts) {
  var railNote = " (This is the regulated 5V rail, not pack voltage \u2014 the pack sits higher.)";
  if (level === "medium") {
    return { title: "5V rail slightly down (" + volts + " V)",
             text: "Still fine to work with, but the regulator is losing a little headroom, which usually means the pack is past half." + railNote };
  }
  if (level === "low") {
    return { title: "5V rail sagging (" + volts + " V) \u2014 batteries getting tired",
             text: "The regulator is struggling, so the pack is running down. Below about 4.75 V the servo jitters and sonar gets unreliable, so calibration numbers taken now may not hold. Charge or swap the cells." + railNote };
  }
  return { title: "5V rail collapsing (" + volts + " V) \u2014 stop and charge",
           text: "The regulator has run out of headroom: the board can reset mid-program and sensors report nonsense. Charge the pack before doing anything else." + railNote };
}

function medianOfNumbers(values) {
  var sorted = values.slice().sort(function (a, b) { return a - b; });
  return sorted[Math.floor(sorted.length / 2)];
}

function updateBatteryDisplay(railMillivolts, motorsAreStopped) {
  var volts = (railMillivolts / 1000).toFixed(2);
  var chip = document.getElementById("batteryReadout");
  var instantLevel = batteryLevelFor(railMillivolts);
  chip.textContent = "\u26A1 rail " + volts + " V";
  chip.className = "battery-chip battery-" + instantLevel;
  chip.title = "Regulated 5V rail (NOT pack voltage): " + volts + " V \u2014 " + instantLevel +
    ". Healthy is 4.90-5.10 V. Your battery pack sits upstream at a higher voltage.";

  if (!motorsAreStopped) {
    return; // motor load dips the rail; never judge the pack mid-drive
  }
  idleRailSamples.push(railMillivolts);
  if (idleRailSamples.length > 15) {
    idleRailSamples.shift();
  }
  if (idleRailSamples.length < 5) {
    return;
  }

  var steadyMillivolts = medianOfNumbers(idleRailSamples);
  var level = batteryLevelFor(steadyMillivolts);
  lastAssessedLevel = level;
  if (level === "high") {
    return;
  }

  var now = Date.now();
  var levelGotWorse = level !== lastWarnedLevel;
  var longEnoughSinceLastWarning = now - lastWarnTime > 300000; // 5 minutes
  if (!startupBatteryCheckDone || levelGotWorse || longEnoughSinceLastWarning) {
    startupBatteryCheckDone = true;
    lastWarnedLevel = level;
    lastWarnTime = now;
    pendingBatteryWarning = batteryMessageFor(level, (steadyMillivolts / 1000).toFixed(2));
    pendingBatteryWarning.level = level;
    flushBatteryWarningIfSafe();
  }
}

// ---------------------------------------------------------------------------
// Pack voltage (only present once the divider is wired). 2S 18650, UNPROTECTED:
//   8.4 V full   7.8 good   7.4 nominal   7.0 warn   6.6 CUTOFF   6.0 damage
// The kernel enforces the cutoff itself; this is the human-facing half.
// ---------------------------------------------------------------------------
var PACK_WARN_MV = 7000;

var PACK_CUTOFF_MV = 6600;

var packWarningShown = false;

function packLevelFor(millivolts) {
  if (millivolts >= 7800) { return "high"; }
  if (millivolts >= 7400) { return "medium"; }
  if (millivolts >= PACK_WARN_MV) { return "low"; }
  return "danger";
}

var PACK_ABSENT_MV = 3000;

function updatePackDisplay(packMillivolts) {
  var chipElement = document.getElementById("packReadout");
  if (packMillivolts < PACK_ABSENT_MV) {
    // VIN is dead on USB-only power: no pack to report, nothing to warn about.
    chipElement.style.display = "inline-block";
    chipElement.textContent = "\uD83D\uDD0C USB power";
    chipElement.className = "battery-chip battery-medium";
    chipElement.title = "Running on USB: VIN is unpowered, so pack voltage can't be read. " +
      "Battery cutoff is disarmed.";
    document.getElementById("cutoffBanner").style.display = "none";
    return;
  }
  var packVolts = (packMillivolts / 1000).toFixed(2);
  var perCell = (packMillivolts / 2000).toFixed(2);
  chipElement.style.display = "inline-block";
  chipElement.textContent = "\uD83D\uDD0B " + packVolts + " V";
  chipElement.className = "battery-chip battery-" + packLevelFor(packMillivolts);
  chipElement.title = "Battery pack: " + packVolts + " V (" + perCell + " V per cell). " +
    "Cutoff at 6.60 V; cells are damaged below 6.00 V.";

  var cutoffBanner = document.getElementById("cutoffBanner");
  if (packMillivolts < PACK_CUTOFF_MV) {
    document.getElementById("cutoffVoltage").textContent = packVolts + " V (" + perCell + " V/cell)";
    cutoffBanner.style.display = "block";
    return;
  }
  cutoffBanner.style.display = "none";

  if (packMillivolts < PACK_WARN_MV && !packWarningShown) {
    packWarningShown = true;
    pendingBatteryWarning = {
      level: "low",
      title: "Battery pack low (" + packVolts + " V \u2014 " + perCell + " V per cell)",
      text: "Wrap up what you're doing and put these cells on the charger. The robot stops " +
        "itself at 6.60 V to protect the cells, and you're close to it."
    };
    flushBatteryWarningIfSafe();
  } else if (packMillivolts > PACK_WARN_MV + 300) {
    packWarningShown = false;
  }
}

// A warning may only appear when the person isn't steering something.
function interactionIsSafeForWarning() {
  if (typeof joystickActive !== "undefined" && joystickActive) { return false; }
  if (typeof tiltDriveActive !== "undefined" && tiltDriveActive) { return false; }
  if (typeof pendingAckButton !== "undefined" && pendingAckButton) { return false; }
  if (document.querySelector(".dpad button.pressed")) { return false; }
  if (lastKnownMode !== 0) { return false; } // robot is running a mode/program
  return true;
}

function flushBatteryWarningIfSafe() {
  if (!pendingBatteryWarning || !interactionIsSafeForWarning()) {
    return;
  }
  var banner = document.getElementById("batteryBanner");
  document.getElementById("batteryBannerTitle").textContent = pendingBatteryWarning.title;
  document.getElementById("batteryBannerText").textContent = pendingBatteryWarning.text;
  banner.className = "level-" + pendingBatteryWarning.level;
  banner.style.display = "block";
  logLine(pendingBatteryWarning.title + " \u2014 " + pendingBatteryWarning.text, "err");
  pendingBatteryWarning = null;
}

document.getElementById("batteryBannerDismiss").addEventListener("click", function () {
  document.getElementById("batteryBanner").style.display = "none";
});
