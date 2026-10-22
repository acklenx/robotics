"use strict";

// ========================================================================
// The Blockly editor — served from this site, never from a CDN.
//
// vendor/blockly.min.js is a copy of Blockly 13.2.1 that lives in your own
// folder. That is the whole fix for the "Blockly didn't load (offline?)"
// message: nothing is fetched from anyone else's server, so nothing can be
// blocked, throttled, or simply absent on club night.
//
// The blocks here are robot instructions, not general programming blocks,
// and every one of them compiles to a line of the same robot language the
// tap editor and the text box produce. Three views, one program.
// ========================================================================

var blocklyWorkspace = null;
var blocklyIsAvailable = typeof Blockly !== "undefined";

var ROBOT_BLOCK_DEFINITIONS = [
  {
    "type": "robot_drive_rooms",
    "message0": "drive %1 room(s) forward",
    "args0": [
      { "type": "field_number", "name": "ROOMS", "value": 1, "min": 1, "max": 8, "precision": 1 }
    ],
    "previousStatement": null, "nextStatement": null, "colour": 25,
    "tooltip": "Uses this robot's measured milliseconds-per-room."
  },
  {
    "type": "robot_turn_quarter",
    "message0": "quarter turn %1",
    "args0": [
      { "type": "field_dropdown", "name": "DIRECTION",
        "options": [["left", "L"], ["right", "R"]] }
    ],
    "previousStatement": null, "nextStatement": null, "colour": 25,
    "tooltip": "Uses this robot's measured milliseconds-per-90-degrees."
  },
  {
    "type": "robot_move",
    "message0": "drive %1 for %2 seconds",
    "args0": [
      { "type": "field_dropdown", "name": "DIRECTION",
        "options": [["forward", "F"], ["backward", "B"]] },
      { "type": "field_number", "name": "SECONDS", "value": 1, "min": 0.1, "max": 20, "precision": 0.01 }
    ],
    "previousStatement": null, "nextStatement": null, "colour": 25
  },
  {
    "type": "robot_spin",
    "message0": "spin %1 for %2 seconds",
    "args0": [
      { "type": "field_dropdown", "name": "DIRECTION",
        "options": [["left", "L"], ["right", "R"]] },
      { "type": "field_number", "name": "SECONDS", "value": 0.4, "min": 0.1, "max": 10, "precision": 0.01 }
    ],
    "previousStatement": null, "nextStatement": null, "colour": 25
  },
  {
    "type": "robot_wait",
    "message0": "wait %1 seconds",
    "args0": [
      { "type": "field_number", "name": "SECONDS", "value": 1, "min": 0.1, "max": 20, "precision": 0.01 }
    ],
    "previousStatement": null, "nextStatement": null, "colour": 210
  },
  {
    "type": "robot_speed",
    "message0": "set speed to %1",
    "args0": [
      { "type": "field_number", "name": "LEVEL", "value": 5, "min": 0, "max": 9, "precision": 1 }
    ],
    "previousStatement": null, "nextStatement": null, "colour": 210
  },
  {
    "type": "robot_servo",
    "message0": "point sonar to %1 degrees",
    "args0": [
      { "type": "field_number", "name": "ANGLE", "value": 90, "min": 0, "max": 180, "precision": 5 }
    ],
    "previousStatement": null, "nextStatement": null, "colour": 210
  },
  {
    "type": "robot_stop",
    "message0": "stop the program",
    "previousStatement": null, "colour": 210
  },
  {
    "type": "robot_repeat",
    "message0": "repeat %1 times %2 do %3",
    "args0": [
      { "type": "field_number", "name": "COUNT", "value": 4, "min": 2, "max": 20, "precision": 1 },
      { "type": "input_dummy" },
      { "type": "input_statement", "name": "DO" }
    ],
    "previousStatement": null, "nextStatement": null, "colour": 120
  },
  {
    "type": "robot_if_sensor",
    "message0": "if %1 %2 do %3",
    "args0": [
      { "type": "field_dropdown", "name": "SENSOR",
        "options": [
          ["left line sees black", "1"],
          ["right line sees black", "2"],
          ["something close on the left", "3"],
          ["something close on the right", "4"],
          ["wall closer than 25 cm", "5"],
          ["the way ahead is clear", "6"],
          ["the right side is clear", "7"],
          ["the left side is clear", "8"]
        ] },
      { "type": "input_dummy" },
      { "type": "input_statement", "name": "DO" }
    ],
    "previousStatement": null, "nextStatement": null, "colour": 270
  }
];

var ROBOT_TOOLBOX = {
  "kind": "categoryToolbox",
  "contents": [
    { "kind": "category", "name": "Move", "colour": "25", "contents": [
      { "kind": "block", "type": "robot_drive_rooms" },
      { "kind": "block", "type": "robot_turn_quarter" },
      { "kind": "block", "type": "robot_move" },
      { "kind": "block", "type": "robot_spin" }
    ]},
    { "kind": "category", "name": "Control", "colour": "210", "contents": [
      { "kind": "block", "type": "robot_wait" },
      { "kind": "block", "type": "robot_speed" },
      { "kind": "block", "type": "robot_servo" },
      { "kind": "block", "type": "robot_stop" }
    ]},
    { "kind": "category", "name": "Repeat", "colour": "120", "contents": [
      { "kind": "block", "type": "robot_repeat" }
    ]},
    { "kind": "category", "name": "Sense", "colour": "270", "contents": [
      { "kind": "block", "type": "robot_if_sensor" }
    ]}
  ]
};

// --- Blockly workspace -> robot language ---------------------------------
// Deliberately hand-written rather than using a Blockly generator class: the
// output language is eleven single letters, and a reader can check this
// function against the robot's own parser in one sitting.

function writeBlocklyChain(firstBlock, outputLines) {
  var block = firstBlock;
  while (block) {
    var calibration = calibrationInUse();
    var type = block.type;

    if (type === "robot_drive_rooms") {
      outputLines.push("F " + Math.round(calibration.cellMs * Number(block.getFieldValue("ROOMS"))));
    } else if (type === "robot_turn_quarter") {
      outputLines.push(block.getFieldValue("DIRECTION") + " " + Math.round(calibration.turnMs));
    } else if (type === "robot_move" || type === "robot_spin") {
      outputLines.push(block.getFieldValue("DIRECTION") + " " +
        secondsToMilliseconds(block.getFieldValue("SECONDS")));
    } else if (type === "robot_wait") {
      outputLines.push("W " + secondsToMilliseconds(block.getFieldValue("SECONDS")));
    } else if (type === "robot_speed") {
      outputLines.push("V " + block.getFieldValue("LEVEL"));
    } else if (type === "robot_servo") {
      outputLines.push("E " + block.getFieldValue("ANGLE"));
    } else if (type === "robot_stop") {
      outputLines.push("X");
    } else if (type === "robot_repeat") {
      outputLines.push("P " + block.getFieldValue("COUNT"));
      writeBlocklyChain(block.getInputTargetBlock("DO"), outputLines);
      outputLines.push("Q");
    } else if (type === "robot_if_sensor") {
      outputLines.push("I " + block.getFieldValue("SENSOR"));
      writeBlocklyChain(block.getInputTargetBlock("DO"), outputLines);
      outputLines.push("Z");
    }
    block = block.getNextBlock();
  }
}

function blocklyProgramAsRobotLanguage() {
  if (!blocklyWorkspace) {
    return "";
  }
  var outputLines = [];
  var topBlocks = blocklyWorkspace.getTopBlocks(true);
  var i;
  for (i = 0; i < topBlocks.length; i++) {
    writeBlocklyChain(topBlocks[i], outputLines);
  }
  return outputLines.join("\n");
}

// --- robot language -> Blockly blocks ------------------------------------
// So that reading a program off the robot fills the canvas, not just the
// text box. Timed moves come back as seconds; there is no way to know
// whether "F 900" meant one room or 0.9 seconds, so we show the honest one.

function buildBlocklyBlock(step) {
  var block;
  if (step.kind === "driveSeconds" || step.kind === "backSeconds") {
    block = blocklyWorkspace.newBlock("robot_move");
    block.setFieldValue(step.kind === "driveSeconds" ? "F" : "B", "DIRECTION");
    block.setFieldValue(String(step.value), "SECONDS");
  } else if (step.kind === "spinLeft" || step.kind === "spinRight") {
    block = blocklyWorkspace.newBlock("robot_spin");
    block.setFieldValue(step.kind === "spinLeft" ? "L" : "R", "DIRECTION");
    block.setFieldValue(String(step.value), "SECONDS");
  } else if (step.kind === "wait") {
    block = blocklyWorkspace.newBlock("robot_wait");
    block.setFieldValue(String(step.value), "SECONDS");
  } else if (step.kind === "speed") {
    block = blocklyWorkspace.newBlock("robot_speed");
    block.setFieldValue(String(step.value), "LEVEL");
  } else if (step.kind === "servo") {
    block = blocklyWorkspace.newBlock("robot_servo");
    block.setFieldValue(String(step.value), "ANGLE");
  } else if (step.kind === "stop") {
    block = blocklyWorkspace.newBlock("robot_stop");
  } else if (step.kind === "repeat") {
    block = blocklyWorkspace.newBlock("robot_repeat");
    block.setFieldValue(String(step.value), "COUNT");
    attachChildren(block, step.children);
  } else if (step.kind === "ifSensor") {
    block = blocklyWorkspace.newBlock("robot_if_sensor");
    block.setFieldValue(String(step.value), "SENSOR");
    attachChildren(block, step.children);
  } else if (step.kind === "driveRooms") {
    block = blocklyWorkspace.newBlock("robot_drive_rooms");
    block.setFieldValue(String(step.value), "ROOMS");
  } else if (step.kind === "turnQuarter") {
    block = blocklyWorkspace.newBlock("robot_turn_quarter");
    block.setFieldValue(step.value === 1 ? "R" : "L", "DIRECTION");
  }
  if (block) {
    block.initSvg();
  }
  return block;
}

function attachChildren(containerBlock, childSteps) {
  var previous = null;
  var i;
  for (i = 0; i < childSteps.length; i++) {
    var child = buildBlocklyBlock(childSteps[i]);
    if (!child) {
      continue;
    }
    if (previous === null) {
      containerBlock.getInput("DO").connection.connect(child.previousConnection);
    } else {
      previous.nextConnection.connect(child.previousConnection);
    }
    previous = child;
  }
}

function loadBlocklyFromSteps(steps) {
  if (!blocklyWorkspace) {
    return;
  }
  blocklyWorkspace.clear();
  var previous = null;
  var i;
  for (i = 0; i < steps.length; i++) {
    var block = buildBlocklyBlock(steps[i]);
    if (!block) {
      continue;
    }
    if (previous) {
      previous.nextConnection.connect(block.previousConnection);
    }
    previous = block;
  }
  blocklyWorkspace.render();
}

// --- start it up ----------------------------------------------------------

function initializeBlocklyEditor() {
  var area = document.getElementById("blocklyArea");
  if (!area) {
    return;
  }
  if (!blocklyIsAvailable) {
    // vendor/blockly.min.js is missing from the folder. Say exactly that,
    // rather than the old "offline?" guess, and point at the way out.
    area.innerHTML = '<div class="blockly-missing">vendor/blockly.min.js is not in this ' +
      'folder, so the drag-and-drop editor cannot start. The <b>Tap blocks</b> view ' +
      'below works regardless, and so does typing robot language directly.</div>';
    logLine("Blockly is not installed in vendor/ — using the tap editor instead.", "err");
    showBlockEditorView("tap");
    return;
  }

  Blockly.defineBlocksWithJsonArray(ROBOT_BLOCK_DEFINITIONS);
  blocklyWorkspace = Blockly.inject(area, {
    // Icons, cursors and click sounds come from our own folder too. Without
    // this Blockly reaches out for its media, which is the same CDN problem
    // wearing a different hat.
    media: "./vendor/blockly-media/",
    toolbox: ROBOT_TOOLBOX,
    scrollbars: true,
    trashcan: true,
    zoom: { controls: true, startScale: 0.9 },
    grid: { spacing: 22, length: 3, colour: "#2b323b", snap: true },
    move: { drag: true, wheel: true, scrollbars: true }
  });

  blocklyWorkspace.addChangeListener(function (changeEvent) {
    if (changeEvent.isUiEvent) {
      return;
    }
    var generated = blocklyProgramAsRobotLanguage();
    if (programText) {
      programText.value = generated;
    }
  });

  logLine("Blockly " + (Blockly.VERSION || "") + " loaded from this site (no internet needed).");
}

// --- switching between the two editors ------------------------------------

function showBlockEditorView(viewName) {
  var blocklyPanel = document.getElementById("blocklyPanel");
  var tapPanel = document.getElementById("tapBlocksPanel");
  var blocklyButton = document.getElementById("blocklyViewButton");
  var tapButton = document.getElementById("tapViewButton");

  var useBlockly = viewName === "blockly" && blocklyIsAvailable;
  blocklyPanel.style.display = useBlockly ? "block" : "none";
  tapPanel.style.display = useBlockly ? "none" : "block";
  blocklyButton.classList.toggle("active", useBlockly);
  tapButton.classList.toggle("active", !useBlockly);

  if (useBlockly && blocklyWorkspace) {
    // Blockly measures its canvas when it becomes visible, not before.
    Blockly.svgResize(blocklyWorkspace);
    loadBlocklyFromSteps(parseRobotLanguage(programText.value));
  } else if (!useBlockly) {
    programSteps = parseRobotLanguage(programText.value);
    renderBlockEditor();
  }
}

document.getElementById("blocklyViewButton").addEventListener("click", function () {
  showBlockEditorView("blockly");
});
document.getElementById("tapViewButton").addEventListener("click", function () {
  showBlockEditorView("tap");
});

initializeBlocklyEditor();

// Phones get the tap editor by default (Blockly's drag-and-drop is painful on
// a small screen); anything wider starts with Blockly.
if (blocklyIsAvailable && window.innerWidth >= 720) {
  showBlockEditorView("blockly");
} else {
  showBlockEditorView("tap");
}
