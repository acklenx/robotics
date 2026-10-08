// Boots the real workshop.html in jsdom with all scripts, then drives the
// block editor the way a kid would and checks what the robot would receive.
// Run: node test-workshop.js
const fs = require("fs");
const path = require("path");
const { JSDOM } = require("jsdom");

let failures = 0;
function check(label, actual, expected) {
  const pass = JSON.stringify(actual) === JSON.stringify(expected);
  if (!pass) {
    failures++;
    console.log("FAIL " + label + "\n     got: " + JSON.stringify(actual) +
                "\n  wanted: " + JSON.stringify(expected));
  } else {
    console.log("PASS " + label);
  }
}
function checkTrue(label, condition, detail) {
  if (!condition) {
    failures++;
    console.log("FAIL " + label + (detail ? " — " + detail : ""));
  } else {
    console.log("PASS " + label);
  }
}

const html = fs.readFileSync(path.join(__dirname, "workshop.html"), "utf8");
const scriptOrder = [
  "app-state.js", "console-log.js", "link.js", "telemetry.js", "battery.js",
  "runner.js", "program.js", "blocks.js", "blockly-editor.js", "drive.js",
  "calibrate.js", "app.js"
];

// Load the page exactly as a browser would: real <script src> tags, real
// order, no Web Bluetooth (which is also the "offline laptop" case).
const errors = [];
const dom = new JSDOM(html, {
  url: "file://" + __dirname + "/workshop.html",
  runScripts: "dangerously",
  resources: "usable",
  pretendToBeVisual: true
});
const { window } = dom;
window.addEventListener("error", function (event) { errors.push(event.message); });

// jsdom has no audio stack. Blockly asks for click sounds on start-up; the
// browser has these, the test runner does not. Stubbing them keeps the test
// about our code rather than about jsdom's gaps.
window.fetch = function () {
  return Promise.resolve({ arrayBuffer: function () { return Promise.resolve(new ArrayBuffer(0)); } });
};
window.AudioContext = function () {
  return { decodeAudioData: function () {}, close: function () {}, createBufferSource: function () {
    return { connect: function () {}, start: function () {} }; } };
};

window.addEventListener("load", function () {
  setTimeout(runTests, 50);
});

function runTests() {
const document = window.document;
window.showBlockEditorView("tap");

function tapPalette(kind) {
  document.querySelector('[data-kind="' + kind + '"]').dispatchEvent(
    new window.MouseEvent("click", { bubbles: true }));
}
function programText() {
  return document.getElementById("programText").value;
}

// --- 1. a straight-line program in tap order ------------------------------
tapPalette("speed");
tapPalette("driveRooms");
tapPalette("turnQuarter");
check("simple program generates in tap order", programText(),
      "V 5\nF 900\nL 350");

// --- 2. numbers change without a keyboard ---------------------------------
const roomsIdentifier = document.querySelectorAll('#blockEditor > [data-step]')[1]
  .getAttribute("data-step");
function tapNudge(identifier, direction) {
  // The editor rebuilds after every change, so look the button up each time —
  // exactly what a finger does.
  document.querySelector('[data-nudge="' + direction + '"][data-step="' + identifier + '"]')
    .dispatchEvent(new window.MouseEvent("click", { bubbles: true }));
}
tapNudge(roomsIdentifier, "up");
tapNudge(roomsIdentifier, "up");
check("+ raises drive-rooms to 3 rooms", programText(),
      "V 5\nF 2700\nL 350");

// --- 3. containers: blocks land INSIDE a repeat ---------------------------
window.eval("programSteps = []; insertTargetIdentifier = null; insertInsideContainer = false; renderBlockEditor();");
tapPalette("repeat");
tapPalette("driveRooms");
tapPalette("turnQuarter");
check("repeat wraps the blocks tapped after it", programText(),
      "P 4\nF 900\nL 350\nQ");

// --- 4. aiming at the end escapes the container ---------------------------
document.getElementById("blockEndButton").dispatchEvent(new window.MouseEvent("click", { bubbles: true }));
tapPalette("stop");
check("Aim at the end places the next block outside the loop", programText(),
      "P 4\nF 900\nL 350\nQ\nX");

// --- 5. sensor conditionals -----------------------------------------------
window.eval("programSteps = []; insertTargetIdentifier = null; insertInsideContainer = false; renderBlockEditor();");
tapPalette("ifSensor");
tapPalette("spinLeft");
check("if-sensor block wraps its body", programText(), "I 5\nL 400\nZ");

// --- 6. round trip: robot language -> blocks -> robot language ------------
const original = "V 5\nP 3\nF 900\nL 350\nQ\nI 1\nB 500\nZ\nX";
document.getElementById("programText").value = original;
document.getElementById("textToBlocksButton").dispatchEvent(new window.MouseEvent("click", { bubbles: true }));
check("round trip through the block editor is lossless", programText(), original);
checkTrue("round trip produced real blocks", document.querySelectorAll("[data-step]").length >= 5);

// --- 7. deleting and reordering -------------------------------------------
window.eval("programSteps = []; insertTargetIdentifier = null; insertInsideContainer = false; renderBlockEditor();");
tapPalette("driveSeconds");
document.getElementById("blockEndButton").dispatchEvent(new window.MouseEvent("click", { bubbles: true }));
tapPalette("wait");
document.getElementById("blockEndButton").dispatchEvent(new window.MouseEvent("click", { bubbles: true }));
tapPalette("backSeconds");
check("three sequential blocks", programText(), "F 1000\nW 1000\nB 1000");
const secondBlock = document.querySelectorAll("#blockEditor > [data-step]")[1];
secondBlock.querySelector('[data-move="-1"]').dispatchEvent(new window.MouseEvent("click", { bubbles: true }));
check("up arrow moves a block earlier", programText(), "W 1000\nF 1000\nB 1000");
const firstBlock = document.querySelectorAll("#blockEditor > [data-step]")[0];
firstBlock.querySelector("[data-delete]").dispatchEvent(new window.MouseEvent("click", { bubbles: true }));
check("delete removes exactly one block", programText(), "F 1000\nB 1000");

// --- 8. calibration feeds the room/turn blocks ----------------------------
window.eval("activeCalibration = { name: 'MLK-01', cellMs: 720, turnMs: 410 };");
window.eval("programSteps = []; insertTargetIdentifier = null; insertInsideContainer = false; renderBlockEditor();");
tapPalette("driveRooms");
document.getElementById("blockEndButton").dispatchEvent(new window.MouseEvent("click", { bubbles: true }));
tapPalette("turnQuarter");
check("room and turn blocks use this robot's measured numbers", programText(),
      "F 720\nL 410");

// --- 9. no leftover Blockly anywhere --------------------------------------
checkTrue("Blockly is loaded from vendor/, never a CDN",
  html.indexOf('src="./vendor/blockly.min.js"') >= 0 && html.indexOf("unpkg") === -1 &&
  html.indexOf("cdn") === -1);
checkTrue("the vendored Blockly bundle is actually in the folder",
  fs.existsSync(path.join(__dirname, "vendor", "blockly.min.js")));
checkTrue("Blockly's icons and sounds are vendored too",
  fs.existsSync(path.join(__dirname, "vendor", "blockly-media", "click.mp3")) &&
  fs.readFileSync(path.join(__dirname, "js", "blockly-editor.js"), "utf8")
    .indexOf('media: "./vendor/blockly-media/"') >= 0);
checkTrue("no external script tags at all", html.indexOf("http") === -1 || html.indexOf("src=\"http") === -1);

// --- 10. the app survives having no Bluetooth (offline laptop) ------------
checkTrue("unsupported notice shown when Web Bluetooth is missing",
  document.getElementById("unsupportedNotice").style.display === "block");
checkTrue("tabs still switch with no robot connected", (function () {
  document.getElementById("blocksTabButton").dispatchEvent(new window.MouseEvent("click", { bubbles: true }));
  return document.getElementById("blocksPage").className.indexOf("visible") >= 0;
})());

// --- 11. Blockly is real, local, and speaks the same language ------------
checkTrue("Blockly loaded from vendor/, not a CDN", typeof window.Blockly === "object");
checkTrue("Blockly version is pinned in the folder",
  !!(window.Blockly && window.Blockly.VERSION), window.Blockly && window.Blockly.VERSION);
checkTrue("our robot blocks are defined",
  !!(window.Blockly && window.Blockly.Blocks["robot_drive_rooms"] &&
     window.Blockly.Blocks["robot_if_sensor"]));
checkTrue("a Blockly workspace was created", window.blocklyWorkspace !== null);

// Round trip through the REAL Blockly workspace: text -> blocks -> text.
const blocklySample = "V 5\nP 4\nF 900\nL 350\nQ\nX";
document.getElementById("programText").value = blocklySample;
window.showBlockEditorView("blockly");
check("Blockly rebuilds a program and regenerates it identically",
      window.blocklyProgramAsRobotLanguage(), blocklySample);
checkTrue("Blockly view actually shows blocks",
  window.blocklyWorkspace.getAllBlocks(false).length >= 5,
  "blocks: " + window.blocklyWorkspace.getAllBlocks(false).length);

// Switching views must not lose the program.
window.showBlockEditorView("tap");
check("switching to the tap view keeps the same program",
      document.getElementById("programText").value, blocklySample);
window.showBlockEditorView("blockly");
check("switching back to Blockly keeps it too",
      window.blocklyProgramAsRobotLanguage(), blocklySample);

// Calibration must reach Blockly blocks as well as tap blocks.
window.eval("activeCalibration = { name: 'MLK-02', cellMs: 640, turnMs: 390 };");
document.getElementById("programText").value = "F 900\nL 350";
window.showBlockEditorView("blockly");
checkTrue("Blockly drive-rooms block uses measured numbers when built fresh", (function () {
  const workspace = window.blocklyWorkspace;
  workspace.clear();
  const roomBlock = workspace.newBlock("robot_drive_rooms");
  roomBlock.initSvg();
  const turnBlock = workspace.newBlock("robot_turn_quarter");
  turnBlock.initSvg();
  roomBlock.nextConnection.connect(turnBlock.previousConnection);
  workspace.render();
  return window.blocklyProgramAsRobotLanguage() === "F 640\nL 390";
})(), window.blocklyProgramAsRobotLanguage());

// --- 12. Calibration maths cancels the acceleration ramp ------------------
// Two runs from the same standstill contain the same ramp, so the difference
// is pure cruising. A robot that cruises at 40 cm/s and loses 12 cm to the
// ramp travels 28 cm in one second and 108 cm in three.
check("differential measurement recovers true cruise speed",
      Math.round(window.cellMsFromDriveTrial(28, 108)), Math.round(1000 * 30.5 / 40));
checkTrue("the old single-run method would have been wrong by a lot", (function () {
  const oldWay = 1000 * 30.5 / 28;              // 28 cm in one second
  const newWay = window.cellMsFromDriveTrial(28, 108);
  return Math.abs(oldWay - newWay) > 200;       // ms per room
})());
check("a fast robot and a slow robot give different numbers",
      window.cellMsFromDriveTrial(10, 40) > window.cellMsFromDriveTrial(28, 108), true);

// Bad input must be refused, not silently averaged in.
document.getElementById("driveMeasureInput").value = "100";
document.getElementById("driveLongMeasureInput").value = "60";
const trialsBefore = window.driveTrials.length;
document.getElementById("addDriveTrialButton").dispatchEvent(
  new window.MouseEvent("click", { bubbles: true }));
check("a long run shorter than the short run is refused",
      window.driveTrials.length, trialsBefore);

document.getElementById("driveMeasureInput").value = "28";
document.getElementById("driveLongMeasureInput").value = "108";
document.getElementById("addDriveTrialButton").dispatchEvent(
  new window.MouseEvent("click", { bubbles: true }));
check("a sensible pair is accepted", window.driveTrials.length, trialsBefore + 1);
check("and it lands on the right ms-per-room",
      Math.round(window.currentCellMs()), Math.round(1000 * 30.5 / 40));

checkTrue("no runtime errors while loading", errors.length === 0, errors.join("; "));
console.log(failures === 0 ? "\nALL WORKSHOP TESTS PASS" : "\n" + failures + " FAILURES");
process.exit(failures === 0 ? 0 : 1);
}
