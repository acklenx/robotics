"use strict";

// ========================================================================
// The block editor.
//
// This is ours, not Blockly. Reasons: it loads from your own site (no CDN,
// no "didn't load" message, works with no internet at all), it is built for
// thumbs first, and every block is a real robot instruction rather than a
// general-purpose puzzle piece.
//
// How editing works, deliberately without drag-and-drop:
//   - tap a palette block  -> it is added at the insert point
//   - tap a block in the program -> it becomes the insert point
//   - the arrows on a block move it up or down among its siblings
//   - -/+ change its number; no keyboard needed
//   - container blocks (repeat, if) have an "inside" area; tap that to aim
//     new blocks into the loop instead of after it
//
// Dragging is lovely on a laptop and miserable on a phone in a gym. Tap to
// place, tap to move is reliable on both.
// ========================================================================

var blockEditorElement = document.getElementById("blockEditor");
var blockPaletteElement = document.getElementById("blockPalette");
var blockStatusElement = document.getElementById("blockStatus");

var FAMILY_COLORS = {
  move: "move",
  control: "control",
  loop: "loop",
  sense: "sense"
};

// Where the next tapped palette block goes. null = end of the program.
var insertTargetIdentifier = null;
var insertInsideContainer = false;

function describeStep(step) {
  var type = STEP_TYPES_BY_KIND[step.kind];
  return type.describe(step.value);
}

function formatStepValue(step) {
  var type = STEP_TYPES_BY_KIND[step.kind];
  if (type.choices) {
    return type.choices[type.unit === "direction" ? step.value : step.value - 1];
  }
  if (type.unit === "seconds") {
    return step.value.toFixed(1) + " s";
  }
  if (type.unit === "none") {
    return "";
  }
  return step.value + " " + type.unit;
}

// --- building one block's DOM --------------------------------------------

function buildStepElement(step, depth) {
  var type = STEP_TYPES_BY_KIND[step.kind];
  var wrapper = document.createElement("div");
  wrapper.className = "block block-" + FAMILY_COLORS[type.family];
  if (insertTargetIdentifier === step.identifier && !insertInsideContainer) {
    wrapper.classList.add("insert-here");
  }
  wrapper.setAttribute("data-step", step.identifier);

  var headline = document.createElement("div");
  headline.className = "block-headline";

  var label = document.createElement("span");
  label.className = "block-label";
  label.textContent = type.label;
  headline.appendChild(label);

  if (type.unit !== "none") {
    var valueGroup = document.createElement("span");
    valueGroup.className = "block-value-group";

    var downButton = document.createElement("button");
    downButton.className = "block-nudge";
    downButton.textContent = "\u2212";
    downButton.setAttribute("data-nudge", "down");
    downButton.setAttribute("data-step", step.identifier);
    valueGroup.appendChild(downButton);

    var valueText = document.createElement("span");
    valueText.className = "block-value";
    valueText.textContent = formatStepValue(step);
    valueGroup.appendChild(valueText);

    var upButton = document.createElement("button");
    upButton.className = "block-nudge";
    upButton.textContent = "+";
    upButton.setAttribute("data-nudge", "up");
    upButton.setAttribute("data-step", step.identifier);
    valueGroup.appendChild(upButton);

    headline.appendChild(valueGroup);
  }

  var tools = document.createElement("span");
  tools.className = "block-tools";
  tools.innerHTML =
    '<button class="block-tool" data-move="-1" data-step="' + step.identifier + '">&#9650;</button>' +
    '<button class="block-tool" data-move="1" data-step="' + step.identifier + '">&#9660;</button>' +
    '<button class="block-tool block-delete" data-delete="' + step.identifier + '">&times;</button>';
  headline.appendChild(tools);

  wrapper.appendChild(headline);

  var plain = document.createElement("div");
  plain.className = "block-plain";
  plain.textContent = describeStep(step);
  wrapper.appendChild(plain);

  if (step.children) {
    var inside = document.createElement("div");
    inside.className = "block-inside";
    if (insertTargetIdentifier === step.identifier && insertInsideContainer) {
      inside.classList.add("insert-here");
    }
    inside.setAttribute("data-inside", step.identifier);

    if (step.children.length === 0) {
      var empty = document.createElement("div");
      empty.className = "block-empty";
      empty.textContent = "tap here, then pick blocks to put inside";
      inside.appendChild(empty);
    } else {
      var i;
      for (i = 0; i < step.children.length; i++) {
        inside.appendChild(buildStepElement(step.children[i], depth + 1));
      }
    }
    wrapper.appendChild(inside);
  }

  return wrapper;
}

function renderBlockEditor() {
  blockEditorElement.innerHTML = "";
  if (programSteps.length === 0) {
    var empty = document.createElement("div");
    empty.className = "block-empty-program";
    empty.textContent = "No blocks yet. Tap one from the palette below to start.";
    blockEditorElement.appendChild(empty);
  } else {
    var i;
    for (i = 0; i < programSteps.length; i++) {
      blockEditorElement.appendChild(buildStepElement(programSteps[i], 0));
    }
  }

  var stepCount = countSteps(programSteps);
  blockStatusElement.textContent = stepCount + (stepCount === 1 ? " step" : " steps") +
    " \u2014 robot holds 100";
  refreshProgramText();
}

// --- the palette ---------------------------------------------------------

function buildPalette() {
  blockPaletteElement.innerHTML = "";
  var i;
  for (i = 0; i < STEP_TYPES.length; i++) {
    var type = STEP_TYPES[i];
    var button = document.createElement("button");
    button.className = "palette-block palette-" + FAMILY_COLORS[type.family];
    button.setAttribute("data-kind", type.kind);
    button.textContent = type.label;
    blockPaletteElement.appendChild(button);
  }
}

function addStepOfKind(kind) {
  var step = makeStep(kind);

  if (insertTargetIdentifier === null) {
    programSteps.push(step);
  } else {
    var target = findStep(programSteps, insertTargetIdentifier);
    if (!target) {
      programSteps.push(step);
    } else if (insertInsideContainer && target.children) {
      target.children.push(step);
    } else {
      var list = findStepList(programSteps, insertTargetIdentifier);
      var position = 0;
      var i;
      for (i = 0; i < list.length; i++) {
        if (list[i].identifier === insertTargetIdentifier) {
          position = i + 1;
        }
      }
      list.splice(position, 0, step);
    }
  }

  // Newly placed block becomes the insert point, so tapping several palette
  // blocks in a row builds a sequence in the order you tapped them.
  insertTargetIdentifier = step.identifier;
  insertInsideContainer = step.children ? true : false;
  renderBlockEditor();
}

function nudgeStepValue(identifier, direction) {
  var step = findStep(programSteps, identifier);
  if (!step) {
    return;
  }
  var type = STEP_TYPES_BY_KIND[step.kind];
  if (type.unit === "none") {
    return;
  }
  var next = step.value + direction * type.step;
  next = Math.round(next * 10) / 10;
  if (next < type.minimum) {
    next = type.minimum;
  }
  if (next > type.maximum) {
    next = type.maximum;
  }
  step.value = next;
  renderBlockEditor();
}

// One delegated listener for the whole editor: fewer handlers, and it keeps
// working no matter how many blocks get rebuilt.
blockEditorElement.addEventListener("click", function (event) {
  var target = event.target;

  var nudge = target.getAttribute && target.getAttribute("data-nudge");
  if (nudge) {
    nudgeStepValue(Number(target.getAttribute("data-step")), nudge === "up" ? 1 : -1);
    return;
  }

  var moveBy = target.getAttribute && target.getAttribute("data-move");
  if (moveBy) {
    moveStep(Number(target.getAttribute("data-step")), Number(moveBy));
    renderBlockEditor();
    return;
  }

  var deleteIdentifier = target.getAttribute && target.getAttribute("data-delete");
  if (deleteIdentifier) {
    removeStep(Number(deleteIdentifier));
    if (insertTargetIdentifier === Number(deleteIdentifier)) {
      insertTargetIdentifier = null;
      insertInsideContainer = false;
    }
    renderBlockEditor();
    return;
  }

  var insideElement = target.closest ? target.closest("[data-inside]") : null;
  var blockElement = target.closest ? target.closest("[data-step]") : null;

  if (insideElement) {
    insertTargetIdentifier = Number(insideElement.getAttribute("data-inside"));
    insertInsideContainer = true;
    renderBlockEditor();
    return;
  }
  if (blockElement) {
    insertTargetIdentifier = Number(blockElement.getAttribute("data-step"));
    insertInsideContainer = false;
    renderBlockEditor();
  }
});

blockPaletteElement.addEventListener("click", function (event) {
  var kind = event.target.getAttribute && event.target.getAttribute("data-kind");
  if (kind) {
    addStepOfKind(kind);
  }
});

document.getElementById("blockClearButton").addEventListener("click", function () {
  if (programSteps.length === 0) {
    return;
  }
  programSteps = [];
  insertTargetIdentifier = null;
  insertInsideContainer = false;
  renderBlockEditor();
  logLine("Block editor cleared.");
});

document.getElementById("blockEndButton").addEventListener("click", function () {
  insertTargetIdentifier = null;
  insertInsideContainer = false;
  renderBlockEditor();
});

// Typing in the text box is allowed — it is the same program, written out.
// Reading it back into blocks keeps the two views honest with each other.
document.getElementById("textToBlocksButton").addEventListener("click", function () {
  programSteps = parseRobotLanguage(programText.value);
  insertTargetIdentifier = null;
  insertInsideContainer = false;
  renderBlockEditor();
  logLine("Read " + countSteps(programSteps) + " steps out of the text box into blocks.");
});

buildPalette();
renderBlockEditor();
