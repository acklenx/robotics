"use strict";

// ========================================================================
// The scrolling robot console, and instant touch feedback on every button.
// ========================================================================

// ---------------------------------------------------------------------------
// Every button press gets instant visual feedback and a log line, no matter
// what the robot does about it. Capture phase: fires before any handler.
// ---------------------------------------------------------------------------
document.addEventListener("pointerdown", function (event) {
  var pressedButton = event.target && event.target.closest ? event.target.closest("button") : null;
  if (!pressedButton) {
    // Tapping empty chrome — a label, a panel, the console: nothing is being
    // steered, so this is a good moment to surface a queued warning.
    var interactive = event.target && event.target.closest ?
      event.target.closest("input, select, textarea, a, .dpad, #joystickZone, #blockEditor, #blockPalette") : null;
    if (!interactive) {
      flushBatteryWarningIfSafe();
    }
    return;
  }
  pressedButton.classList.add("just-touched");
  setTimeout(function () { pressedButton.classList.remove("just-touched"); }, 250);
  var buttonLabel = (pressedButton.textContent || "").trim().replace(/\s+/g, " ").slice(0, 44);
  logLine("\u00bb " + buttonLabel + (pressedButton.disabled ? " (locked)" : ""));
}, true);

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------
function logLine(text, cssClass) {
  var line = document.createElement("div");
  if (cssClass) {
    line.className = cssClass;
  }
  line.textContent = text;
  consoleElement.appendChild(line);
  while (consoleElement.childNodes.length > 200) {
    consoleElement.removeChild(consoleElement.firstChild);
  }
  consoleElement.scrollTop = consoleElement.scrollHeight;
}
