"use strict";

// ========================================================================
// The program model.
//
// A program is a list of steps. Some steps (repeat, if) hold a list of
// child steps, so the whole thing is a small tree:
//
//   [ {op:"F", arg:900},
//     {op:"P", arg:4, children:[ {op:"F", arg:900}, {op:"L", arg:350} ]} ]
//
// This file knows three things and nothing about buttons or colours:
//   1. how to build steps
//   2. how to write the tree out as robot language  (blocks -> robot)
//   3. how to read robot language back into a tree  (robot -> blocks)
//
// Keeping those three together is what makes the round trip trustworthy:
// whatever the editor produces, the robot can run, and whatever the robot
// has stored, the editor can show.
// ========================================================================

var ROOM_CENTIMETERS = 30.5;

// Every kind of step a kid can place, in palette order. "kind" is our name
// for it; "op" is the single letter the robot's kernel understands.
var STEP_TYPES = [
  { kind: "driveRooms", op: "F", family: "move", label: "drive rooms",
    unit: "rooms", defaultValue: 1, minimum: 1, maximum: 8, step: 1,
    describe: function (value) { return "drive forward " + value + (value === 1 ? " room" : " rooms"); } },

  { kind: "turnQuarter", op: "L", family: "move", label: "quarter turn",
    unit: "direction", defaultValue: 0, minimum: 0, maximum: 1, step: 1,
    choices: ["left", "right"],
    describe: function (value) { return "quarter turn " + (value === 1 ? "right" : "left"); } },

  { kind: "driveSeconds", op: "F", family: "move", label: "drive seconds",
    unit: "seconds", defaultValue: 1, minimum: 0.1, maximum: 20, step: 0.1,
    describe: function (value) { return "drive forward " + value + " s"; } },

  { kind: "backSeconds", op: "B", family: "move", label: "back up",
    unit: "seconds", defaultValue: 1, minimum: 0.1, maximum: 20, step: 0.1,
    describe: function (value) { return "back up " + value + " s"; } },

  { kind: "spinLeft", op: "L", family: "move", label: "spin left",
    unit: "seconds", defaultValue: 0.4, minimum: 0.1, maximum: 10, step: 0.1,
    describe: function (value) { return "spin left " + value + " s"; } },

  { kind: "spinRight", op: "R", family: "move", label: "spin right",
    unit: "seconds", defaultValue: 0.4, minimum: 0.1, maximum: 10, step: 0.1,
    describe: function (value) { return "spin right " + value + " s"; } },

  { kind: "wait", op: "W", family: "control", label: "wait",
    unit: "seconds", defaultValue: 1, minimum: 0.1, maximum: 20, step: 0.1,
    describe: function (value) { return "wait " + value + " s"; } },

  { kind: "speed", op: "V", family: "control", label: "set speed",
    unit: "level", defaultValue: 5, minimum: 0, maximum: 9, step: 1,
    describe: function (value) { return "set speed to " + value; } },

  { kind: "servo", op: "E", family: "control", label: "point sonar",
    unit: "degrees", defaultValue: 90, minimum: 0, maximum: 180, step: 5,
    describe: function (value) { return "point sonar to " + value + "\u00B0"; } },

  { kind: "repeat", op: "P", family: "loop", label: "repeat", container: true,
    unit: "times", defaultValue: 4, minimum: 2, maximum: 20, step: 1,
    describe: function (value) { return "repeat " + value + " times"; } },

  { kind: "ifSensor", op: "I", family: "sense", label: "if sensor", container: true,
    unit: "sensor", defaultValue: 5, minimum: 1, maximum: 8, step: 1,
    choices: ["left line sees black", "right line sees black",
              "something close on the left", "something close on the right",
              "wall closer than 25 cm", "the way ahead is clear",
              "the right side is clear", "the left side is clear"],
    describe: function (value) { return "if " + STEP_TYPES_BY_KIND.ifSensor.choices[value - 1]; } },

  { kind: "stop", op: "X", family: "control", label: "stop the program",
    unit: "none", defaultValue: 0,
    describe: function () { return "stop the program"; } }
];

var STEP_TYPES_BY_KIND = {};
for (var stepTypeIndex = 0; stepTypeIndex < STEP_TYPES.length; stepTypeIndex++) {
  STEP_TYPES_BY_KIND[STEP_TYPES[stepTypeIndex].kind] = STEP_TYPES[stepTypeIndex];
}

var programSteps = [];          // the tree the editor edits
var nextStepIdentifier = 1;

function makeStep(kind) {
  var type = STEP_TYPES_BY_KIND[kind];
  var step = {
    identifier: nextStepIdentifier,
    kind: kind,
    value: type.defaultValue
  };
  nextStepIdentifier = nextStepIdentifier + 1;
  if (type.container) {
    step.children = [];
  }
  return step;
}

// --- finding steps inside the tree ---------------------------------------

function findStepList(steps, identifier) {
  // Returns the array that directly contains the step, or null.
  var i;
  for (i = 0; i < steps.length; i++) {
    if (steps[i].identifier === identifier) {
      return steps;
    }
    if (steps[i].children) {
      var found = findStepList(steps[i].children, identifier);
      if (found) {
        return found;
      }
    }
  }
  return null;
}

function findStep(steps, identifier) {
  var i;
  for (i = 0; i < steps.length; i++) {
    if (steps[i].identifier === identifier) {
      return steps[i];
    }
    if (steps[i].children) {
      var found = findStep(steps[i].children, identifier);
      if (found) {
        return found;
      }
    }
  }
  return null;
}

function removeStep(identifier) {
  var list = findStepList(programSteps, identifier);
  if (!list) {
    return;
  }
  var i;
  for (i = 0; i < list.length; i++) {
    if (list[i].identifier === identifier) {
      list.splice(i, 1);
      return;
    }
  }
}

function moveStep(identifier, offset) {
  var list = findStepList(programSteps, identifier);
  if (!list) {
    return;
  }
  var i;
  for (i = 0; i < list.length; i++) {
    if (list[i].identifier === identifier) {
      var target = i + offset;
      if (target < 0 || target >= list.length) {
        return;
      }
      var moved = list.splice(i, 1)[0];
      list.splice(target, 0, moved);
      return;
    }
  }
}

function countSteps(steps) {
  var total = 0;
  var i;
  for (i = 0; i < steps.length; i++) {
    total = total + 1;
    if (steps[i].children) {
      total = total + countSteps(steps[i].children) + 1; // +1 for the closer
    }
  }
  return total;
}

// --- blocks -> robot language --------------------------------------------

function calibrationInUse() {
  if (typeof activeCalibration !== "undefined" && activeCalibration) {
    return activeCalibration;
  }
  return { cellMs: 900, turnMs: 350 };
}

function secondsToMilliseconds(secondsValue) {
  return Math.round(Number(secondsValue) * 1000);
}

function writeStepLines(steps, outputLines) {
  var i;
  for (i = 0; i < steps.length; i++) {
    var step = steps[i];
    var calibration = calibrationInUse();

    if (step.kind === "driveRooms") {
      outputLines.push("F " + Math.round(calibration.cellMs * step.value));
    } else if (step.kind === "turnQuarter") {
      outputLines.push((step.value === 1 ? "R " : "L ") + Math.round(calibration.turnMs));
    } else if (step.kind === "driveSeconds") {
      outputLines.push("F " + secondsToMilliseconds(step.value));
    } else if (step.kind === "backSeconds") {
      outputLines.push("B " + secondsToMilliseconds(step.value));
    } else if (step.kind === "spinLeft") {
      outputLines.push("L " + secondsToMilliseconds(step.value));
    } else if (step.kind === "spinRight") {
      outputLines.push("R " + secondsToMilliseconds(step.value));
    } else if (step.kind === "wait") {
      outputLines.push("W " + secondsToMilliseconds(step.value));
    } else if (step.kind === "speed") {
      outputLines.push("V " + step.value);
    } else if (step.kind === "servo") {
      outputLines.push("E " + step.value);
    } else if (step.kind === "stop") {
      outputLines.push("X");
    } else if (step.kind === "repeat") {
      outputLines.push("P " + step.value);
      writeStepLines(step.children, outputLines);
      outputLines.push("Q");
    } else if (step.kind === "ifSensor") {
      outputLines.push("I " + step.value);
      writeStepLines(step.children, outputLines);
      outputLines.push("Z");
    }
  }
}

function programAsRobotLanguage() {
  var outputLines = [];
  writeStepLines(programSteps, outputLines);
  return outputLines.join("\n");
}

// --- robot language -> blocks --------------------------------------------
// Used when reading a program back off the robot. Timed moves come back as
// seconds; there is no way to know whether "F 900" was meant as one room or
// as 0.9 seconds, so the honest choice is to show the seconds.

function parseRobotLanguage(programText) {
  var lines = String(programText).split("\n");
  var rootSteps = [];
  var stack = [rootSteps];
  var i;

  for (i = 0; i < lines.length; i++) {
    var trimmed = lines[i].trim();
    if (trimmed.length === 0) {
      continue;
    }
    var operation = trimmed.charAt(0).toUpperCase();
    var argument = Number(trimmed.slice(1).trim());
    if (isNaN(argument)) {
      argument = 0;
    }
    var currentList = stack[stack.length - 1];
    var step = null;

    if (operation === "F") {
      step = makeStep("driveSeconds");
      step.value = Math.round(argument) / 1000;
    } else if (operation === "B") {
      step = makeStep("backSeconds");
      step.value = Math.round(argument) / 1000;
    } else if (operation === "L") {
      step = makeStep("spinLeft");
      step.value = Math.round(argument) / 1000;
    } else if (operation === "R") {
      step = makeStep("spinRight");
      step.value = Math.round(argument) / 1000;
    } else if (operation === "W") {
      step = makeStep("wait");
      step.value = Math.round(argument) / 1000;
    } else if (operation === "V") {
      step = makeStep("speed");
      step.value = argument;
    } else if (operation === "E") {
      step = makeStep("servo");
      step.value = argument;
    } else if (operation === "X") {
      step = makeStep("stop");
    } else if (operation === "P") {
      step = makeStep("repeat");
      step.value = argument < 2 ? 2 : argument;
      currentList.push(step);
      stack.push(step.children);
      continue;
    } else if (operation === "I") {
      step = makeStep("ifSensor");
      step.value = argument < 1 || argument > 8 ? 5 : argument;
      currentList.push(step);
      stack.push(step.children);
      continue;
    } else if (operation === "Q" || operation === "Z") {
      if (stack.length > 1) {
        stack.pop();
      }
      continue;
    }

    if (step) {
      currentList.push(step);
    }
  }
  return rootSteps;
}

function loadProgramFromRobotLanguage(programText) {
  programSteps = parseRobotLanguage(programText);
  if (typeof renderBlockEditor === "function") {
    renderBlockEditor();
  }
}

// --- the buttons that talk to the robot ----------------------------------

var programText = document.getElementById("programText");
var slotButtons = document.querySelectorAll("[data-slot]");
var activeSlotNumber = 1;

function refreshProgramText() {
  if (programText) {
    programText.value = programAsRobotLanguage();
  }
}

function wireSlotButton(button) {
  button.addEventListener("click", function () {
    var i;
    for (i = 0; i < slotButtons.length; i++) {
      slotButtons[i].classList.remove("active");
    }
    button.classList.add("active");
    activeSlotNumber = Number(button.getAttribute("data-slot"));
    sendCommand("*" + activeSlotNumber + "\n");
    sendCommand("K"); // read it back; the listing lands in the editor
    logLine("Slot " + activeSlotNumber + " selected \u2014 reading what the robot has stored there.");
  });
}

for (var slotButtonIndex = 0; slotButtonIndex < slotButtons.length; slotButtonIndex++) {
  wireSlotButton(slotButtons[slotButtonIndex]);
}

document.getElementById("sendProgramButton").addEventListener("click", function () {
  var programBody = programText.value.trim();
  if (programBody.length === 0) {
    logLine("Nothing to send: the program is empty.", "err");
    return;
  }
  sendCommand("H0\n"); // silence telemetry so it cannot collide with the upload
  sendCommand("[\n" + programBody + "\n]");
  sendCommand("H1\n");
  logLine("Sent " + programBody.split("\n").length + " steps to slot " + activeSlotNumber +
    ". Watch for SAVED steps=" + programBody.split("\n").length + " \u2014 that number must match.");
});

document.getElementById("runProgramButton").addEventListener("click", function () {
  sendCommand("G");
});

document.getElementById("stopProgramButton").addEventListener("click", function () {
  sendStopInsurance();
});

document.getElementById("listProgramButton").addEventListener("click", function () {
  sendCommand("K");
});
