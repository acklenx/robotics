"use strict";

// ========================================================================
// Running programs on the robot: button feedback, RUN acknowledgement, step echoes, read-back.
// ========================================================================

// Button feedback: every press visibly does something, immediately.
// Test buttons lock with a countdown while the robot runs, so nobody
// re-fires a run by jamming the button.
function lockButtonWithCountdown(button, totalSeconds) {
  var originalLabel = button.textContent;
  var secondsLeft = totalSeconds;
  button.disabled = true;
  button.textContent = "Robot running… " + secondsLeft;

  function tick() {
    secondsLeft = secondsLeft - 1;
    if (secondsLeft > 0) {
      button.textContent = "Robot running… " + secondsLeft;
      setTimeout(tick, 1000);
    } else {
      button.disabled = false;
      button.textContent = originalLabel;
    }
  }
  setTimeout(tick, 1000);
}

function flashButtonLabel(button, temporaryLabel, cssClass) {
  var originalLabel = button.textContent;
  button.disabled = true;
  button.classList.add(cssClass);
  button.textContent = temporaryLabel;
  setTimeout(function () {
    button.classList.remove(cssClass);
    button.textContent = originalLabel;
    button.disabled = false;
  }, 1100);
}

function runIsPossible(button) {
  if (!serialCharacteristic) {
    flashButtonLabel(button, "Not connected!", "flash-err");
    return false;
  }
  return true;
}

function parseDecimalInput(inputValue) {
  return Number(String(inputValue).replace(",", ".")); // accept 30,5 and 30.5
}

// Test runs are ACK-driven: press -> "Waiting for robot..." -> the robot's
// own RUN reply starts the countdown ON THE BUTTON THAT ASKED. No ack in 3s
// = red flash, button unlocked, nothing left queued to fire later.
var pendingAckButton = null;

var pendingAckTimer = null;

var pendingTestName = "";

var STEP_DESCRIPTIONS = {
  "F": "forward", "B": "backward", "L": "spin left", "R": "spin right",
  "W": "wait", "V": "set speed", "E": "point servo",
  "P": "start repeat x", "Q": "end repeat", "I": "if sensor", "Z": "end if", "X": "end program"
};

function handleProgramStepEcho(stepLine) {
  var parts = stepLine.split(",");
  if (parts.length < 4) {
    return;
  }
  var op = parts[2];
  var arg = parts[3];
  var described = (STEP_DESCRIPTIONS[op] || op) +
    (op === "F" || op === "B" || op === "L" || op === "R" || op === "W" ? " " + arg + " ms" :
     (op === "V" || op === "E" || op === "P" || op === "I" ? " " + arg : ""));
  logLine("Robot executing: " + described + " \u2026 NOW");
}

function requestProgramRun(button, programBody, testName) {
  if (!runIsPossible(button)) {
    return;
  }
  if (pendingAckButton) {
    logLine("Still waiting on the robot for the last run \u2014 hang on.", "err");
    return;
  }
  pendingAckButton = button;
  pendingTestName = testName || "program";
  button.setAttribute("data-original-label", button.textContent);
  button.disabled = true;
  button.textContent = "Waiting for robot\u2026";
  sendCommand("H0\n[\n" + programBody + "\n]G");
  sendCommand("H1\n");
  logLine("> sent " + programBody.split("\n").length + "-line program + run request; waiting for RUN ack");
  pendingAckTimer = setTimeout(function () {
    var timedOutButton = pendingAckButton;
    pendingAckButton = null;
    timedOutButton.disabled = false;
    timedOutButton.textContent = timedOutButton.getAttribute("data-original-label");
    flashButtonLabel(timedOutButton, "No ack \u2014 press again", "flash-err");
    logLine("No RUN ack within 3s: the robot never confirmed. Nothing is queued; press again.", "err");
  }, 3000);
}

var programListingRemaining = 0;

var programListingLines = [];

function beginProgramListingCapture(stepCount) {
  programListingLines = [];
  if (!stepCount || stepCount <= 0) {
    programListingRemaining = 0;
    programText.value = "";
    logLine("That slot is empty \u2014 editor cleared.");
    return;
  }
  programListingRemaining = stepCount;
}

function captureProgramListingLine(listingLine) {
  programListingLines.push(listingLine);
  programListingRemaining = programListingRemaining - 1;
  if (programListingRemaining === 0) {
    programText.value = programListingLines.join("\n");
    logLine("Loaded " + programListingLines.length + " steps from the robot into the editor.");
  }
}

function robotReplyHook(replyLine) {
  if (replyLine.indexOf("BATTERY CUTOFF") === 0) {
    logLine("ROBOT STOPPED ITSELF: battery cutoff. Charge the pack.", "err");
    return;
  }
  if (replyLine.indexOf("DONE") === 0) {
    logLine("Robot finished" + (pendingTestName ? " " + pendingTestName : "") + ".");
    setTimeout(flushBatteryWarningIfSafe, 300); // program over: calm moment
    return;
  }
  if (replyLine.indexOf("PROGRAM steps=") === 0) {
    beginProgramListingCapture(parseInt(replyLine.split("=")[1], 10));
    return;
  }
  if (programListingRemaining > 0) {
    captureProgramListingLine(replyLine);
    return;
  }
  if (!pendingAckButton) {
    return;
  }
  if (replyLine.indexOf("RUN") === 0) {
    clearTimeout(pendingAckTimer);
    var ackedButton = pendingAckButton;
    pendingAckButton = null;
    ackedButton.disabled = false;
    ackedButton.textContent = ackedButton.getAttribute("data-original-label");
    lockButtonWithCountdown(ackedButton, 3);
    logLine("Robot acknowledged " + pendingTestName + " \u2014 steps will announce themselves as they execute.");
  } else if (replyLine.indexOf("DONE") === 0) {
    logLine("Robot finished " + pendingTestName + ".");
  } else if (replyLine.indexOf("EMPTY SLOT") === 0) {
    clearTimeout(pendingAckTimer);
    var failedButton = pendingAckButton;
    pendingAckButton = null;
    failedButton.disabled = false;
    failedButton.textContent = failedButton.getAttribute("data-original-label");
    flashButtonLabel(failedButton, "Robot: nothing to run!", "flash-err");
  }
}
