// Checks the whole site the way a browser would: every page parses, every
// internal link points at a file that exists, every script and stylesheet
// resolves, and nothing anywhere reaches out to the internet.
// Run: node test-site.js
const fs = require("fs");
const path = require("path");
const { JSDOM } = require("jsdom");
const siteRoot = path.resolve(__dirname, "..");   // the site root: one level up

let failures = 0;
function fail(message) { failures++; console.log("FAIL " + message); }
function pass(message) { console.log("PASS " + message); }

const root = path.resolve(__dirname, '..');   // the site root: one level up
const skipDirectories = ["node_modules", ".git"];

function collectHtmlFiles(directory, found) {
  for (const entry of fs.readdirSync(directory)) {
    if (skipDirectories.indexOf(entry) >= 0) { continue; }
    const full = path.join(directory, entry);
    if (fs.statSync(full).isDirectory()) {
      collectHtmlFiles(full, found);
    } else if (entry.endsWith(".html")) {
      found.push(full);
    }
  }
  return found;
}

const pages = collectHtmlFiles(root, []);
console.log("Found " + pages.length + " pages\n");

let brokenLinks = 0;
let externalReferences = 0;
let emptyPages = 0;

for (const page of pages) {
  const relative = path.relative(root, page);
  const source = fs.readFileSync(page, "utf8");
  let dom;
  try {
    dom = new JSDOM(source);
  } catch (error) {
    fail(relative + " does not parse: " + error.message);
    continue;
  }
  const document = dom.window.document;

  if (!document.querySelector("title") || document.body.textContent.trim().length < 40) {
    fail(relative + " looks empty");
    emptyPages++;
  }

  const references = [];
  document.querySelectorAll("a[href]").forEach(function (element) {
    references.push(["link", element.getAttribute("href")]);
  });
  document.querySelectorAll("script[src]").forEach(function (element) {
    references.push(["script", element.getAttribute("src")]);
  });
  document.querySelectorAll("link[href]").forEach(function (element) {
    references.push(["stylesheet", element.getAttribute("href")]);
  });

  for (const [kind, reference] of references) {
    if (kind === "link" && reference.indexOf("https://makerlabkids.com/") === 0) { continue; }   // back to the main site: allowed
    if (reference.indexOf("http://") === 0 || reference.indexOf("https://") === 0 ||
        reference.indexOf("//") === 0) {
      // Anything absolute is off-site by definition. vendor/ files are
      // relative, so they pass the same check everything else does.
      fail(relative + " reaches the internet for a " + kind + ": " + reference);
      externalReferences++;
      continue;
    }
    if (reference.indexOf("#") === 0 || reference.indexOf("mailto:") === 0 || reference.indexOf("data:") === 0) { continue; }
    // "/x" is relative to the website's root: this folder.
    const bare = reference.split("#")[0];
    let target = bare.indexOf("/") === 0 ? path.join(root, bare) : path.resolve(path.dirname(page), bare);
    if (fs.existsSync(target) && fs.statSync(target).isDirectory()) { target = path.join(target, "index.html"); }
    if (!fs.existsSync(target)) {
      fail(relative + " links to a missing file: " + reference);
      brokenLinks++;
    }
  }
}

if (brokenLinks === 0) { pass("every internal link resolves to a real file"); }
if (externalReferences === 0) { pass("no page depends on anything outside the folder"); }
if (emptyPages === 0) { pass("every page has a title and real content"); }

// Every lesson page must carry the things that make it usable in a session.
const lessonFiles = fs.readdirSync(path.join(root, "lessons"))
  .filter(function (name) { return name.match(/^\d\d-/); });
let lessonProblems = 0;
for (const name of lessonFiles) {
  const source = fs.readFileSync(path.join(root, "lessons", name), "utf8");
  const document = new JSDOM(source).window.document;
  const headings = [];
  document.querySelectorAll("h2").forEach(function (h) { headings.push(h.textContent.trim()); });
  const required = ["Today's goal", "Ideas in this lesson", "How the session runs",
                    "Try this", "When it goes wrong"];
  for (const heading of required) {
    if (headings.indexOf(heading) < 0) {
      fail(name + " is missing its '" + heading + "' section");
      lessonProblems++;
    }
  }
  if (!document.querySelector("details.teacher")) {
    fail(name + " has no notes for the grown-up");
    lessonProblems++;
  }
  if (!document.querySelector(".page-turns a")) {
    fail(name + " has no way to reach the next or previous lesson");
    lessonProblems++;
  }
}
if (lessonProblems === 0) {
  pass("all " + lessonFiles.length + " lesson pages have every required section");
}

// Sample programs must be valid robot language, checked with the real parser.
const programSource = fs.readFileSync(path.join(root, "js", "program.js"), "utf8");
const sandbox = new JSDOM("<div id='programText'></div>", { runScripts: "outside-only" }).window;
sandbox.eval("var activeCalibration = null;");
sandbox.eval(programSource.replace(/^"use strict";/, "")
  .replace(/var programText = document[^\n]*\n/, "var programText = null;\n")
  .replace(/var slotButtons = document[^\n]*\n/, "var slotButtons = [];\n")
  .replace(/for \(var slotButtonIndex[\s\S]*$/, ""));

const validOperations = "FBLRWVEPQIZX";
let programProblems = 0;
for (const name of lessonFiles) {
  const source = fs.readFileSync(path.join(root, "lessons", name), "utf8");
  const document = new JSDOM(source).window.document;
  const block = document.querySelector("pre.program");
  if (!block) { continue; }
  const programText = block.textContent.trim();

  let depth = 0;
  for (const line of programText.split("\n")) {
    const operation = line.trim().charAt(0).toUpperCase();
    if (validOperations.indexOf(operation) < 0) {
      fail(name + ": '" + line.trim() + "' is not a robot instruction");
      programProblems++;
    }
    if (operation === "P" || operation === "I") { depth++; }
    if (operation === "Q" || operation === "Z") { depth--; }
    if (depth < 0) {
      fail(name + ": a loop or if closes before it opens");
      programProblems++;
    }
  }
  if (depth !== 0) {
    fail(name + ": " + depth + " loop(s) or if(s) never close");
    programProblems++;
  }

  // Round trip it through the real parser and generator.
  const parsed = sandbox.parseRobotLanguage(programText);
  const lines = [];
  sandbox.programSteps = parsed;
  sandbox.writeStepLines(parsed, lines);
  const regenerated = lines.join("\n");
  const sameShape = regenerated.split("\n").length === programText.split("\n").length;
  if (!sameShape) {
    fail(name + ": the block editor cannot round-trip this sample\n     in:  " +
         programText.replace(/\n/g, " | ") + "\n     out: " + regenerated.replace(/\n/g, " | "));
    programProblems++;
  }
}
if (programProblems === 0) {
  pass("every sample program is valid robot language and survives the block editor");
}

// The cheat sheet is the page kids trust when the robot misbehaves, so every
// command printed on it must actually exist in the firmware.
const kernelSource = fs.readFileSync(path.join(root, "robot_kernel.ino"), "utf8");
const cheatDocument = new JSDOM(fs.readFileSync(path.join(root, "cheatsheet.html"), "utf8")).window.document;
const printedCommands = [];
cheatDocument.querySelectorAll("table.watch td:first-child").forEach(function (cell) {
  printedCommands.push(cell.textContent.trim());
});

const commandsToVerify = {
  "h1 / h0": "telemetryEnabled",
  "*1 *2 *3": "SLOT_COUNT",
  "z z": "handleZeroRequest",
  "d": "printDiagnostics"
};
let cheatProblems = 0;
for (const printed of Object.keys(commandsToVerify)) {
  if (printedCommands.indexOf(printed) < 0) {
    fail("cheat sheet no longer lists '" + printed + "'");
    cheatProblems++;
  } else if (kernelSource.indexOf(commandsToVerify[printed]) < 0) {
    fail("cheat sheet promises '" + printed + "' but the firmware has no " +
         commandsToVerify[printed]);
    cheatProblems++;
  }
}

// Sensor numbers 1-8 on the sheet must match the firmware's condition table.
const sensorRows = [];
cheatDocument.querySelectorAll("h2").forEach(function (heading) {
  if (heading.textContent.indexOf("Sensor numbers") >= 0) {
    heading.parentElement.querySelectorAll("table.watch tr").forEach(function (row) {
      sensorRows.push(row.textContent.trim());
    });
  }
});
if (sensorRows.length !== 8) {
  fail("cheat sheet lists " + sensorRows.length + " sensor numbers; the firmware has 8");
  cheatProblems++;
}
if (kernelSource.indexOf("sensorId == 8") < 0) {
  fail("the firmware no longer defines sensor 8, but the cheat sheet still prints it");
  cheatProblems++;
}
if (cheatProblems === 0) {
  pass("every command on the cheat sheet exists in the firmware");
}

// Printing matters: these pages get taped to a table.
const siteCss = fs.readFileSync(path.join(root, "css", "site.css"), "utf8");
if (siteCss.indexOf("@media print") < 0) {
  fail("no print styles: lesson pages would print dark and unreadable");
} else {
  pass("print styles exist for lesson pages and the cheat sheet");
}

console.log(failures === 0 ? "\nALL SITE TESTS PASS" : "\n" + failures + " FAILURES");
process.exit(failures === 0 ? 0 : 1);
