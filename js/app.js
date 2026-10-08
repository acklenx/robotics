"use strict";

// ========================================================================
// Tabs and startup. Loaded last.
// ========================================================================

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------
var TAB_DEFINITIONS = [
  { name: "drive", button: document.getElementById("driveTabButton"), page: document.getElementById("drivePage") },
  { name: "blocks", button: document.getElementById("blocksTabButton"), page: document.getElementById("blocksPage") },
  { name: "calibrate", button: document.getElementById("calibrateTabButton"), page: document.getElementById("calibratePage") }
];

function showTab(tabName) {
  var i;
  for (i = 0; i < TAB_DEFINITIONS.length; i++) {
    var tab = TAB_DEFINITIONS[i];
    var active = tab.name === tabName;
    tab.page.className = active ? "tab-page visible" : "tab-page";
    tab.button.className = active ? "active" : "";
  }
  if (tabName === "blocks" && typeof renderBlockEditor === "function") {
    renderBlockEditor(); // repaint in case calibration numbers changed
  }
}

function wireTabButton(tab) {
  tab.button.addEventListener("click", function () {
    showTab(tab.name);
    flushBatteryWarningIfSafe(); // switching tabs = a calm moment
  });
}

for (var tabIndex = 0; tabIndex < TAB_DEFINITIONS.length; tabIndex++) {
  wireTabButton(TAB_DEFINITIONS[tabIndex]);
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------
logLine("Workshop v" + APP_VERSION + " (expects kernel " + KERNEL_COMPAT + "+)");

if (!navigator.bluetooth) {
  unsupportedNotice.style.display = "block";
  connectButton.disabled = true;
}
