// The one nav bar (and footer) every robotics page shares. The site has no
// build step, so the markup is copied into each page between marker comments.
//   node site-nav.js           write the nav and footer into every page
//   node site-nav.js --check   fail if any page's copy is missing or differs
// To add or rename a nav item, change NAV below and run it without --check.
const fs = require("fs");
const path = require("path");
const root = path.resolve(__dirname, "..");

// The live site: what the current class has covered (plus Blocks, coming up).
const NAV = [
  ["/", "Home"],
  ["/mecanum/remote-guide", "Remote guide"],
  ["/mecanum/ir", "Programming"],
  ["/mecanum/challenges", "Challenges"],
  ["/mecanum/light", "Light bot"],
  ["/mecanum/wiring", "Wiring"],
  ["/mecanum/blocks", "Blocks"],
  ["/mecanum/lessons/", "Lessons"]
];

// Set aside until the class gets there: reachable from /archive/, not the nav.
const SET_ASIDE = [
  ["/mecanum/", "Workshop app"],
  ["/mecanum/sticks", "Sticks"],
  ["/mecanum/calibration", "Calibrate"],
  ["/mecanum/rename", "Rename"],
  ["/mecanum/holonomic", "Holonomic notes"],
  ["/archive/", "Archive"]
];

// A section's own pages, shown as a subnav under the bar on each of them.
const SECTIONS = {
  programming: { title: "Programming", pages: [
    ["/mecanum/ir", "Programming guide"],
    ["/mecanum/square", "Program a square"]
  ] },
  light: { title: "Light bot", pages: [
    ["/mecanum/light", "How to use the Light Bot"],
    ["/mecanum/light-build", "How to build a light sensor"]
  ] }
};

// page -> { active: nav href, setAside: subnav href, section + here: a section subnav,
//           tool: narrow screens get a Back link only, noFoot }
const PAGES = {
  "index.html": { active: "/" },
  "mecanum/remote-guide.html": { active: "/mecanum/remote-guide" },
  "mecanum/ir.html": { active: "/mecanum/ir", section: "programming", here: "/mecanum/ir" },
  "mecanum/square.html": { active: "/mecanum/ir", section: "programming", here: "/mecanum/square" },
  "mecanum/challenges.html": { active: "/mecanum/challenges" },
  "mecanum/light.html": { active: "/mecanum/light", section: "light", here: "/mecanum/light" },
  "mecanum/light-build.html": { active: "/mecanum/light", section: "light", here: "/mecanum/light-build" },
  "mecanum/wiring.html": { active: "/mecanum/wiring" },
  "mecanum/blocks.html": { active: "/mecanum/blocks", noFoot: true },
  "mecanum/index.html": { setAside: "/mecanum/" },
  "mecanum/sticks.html": { setAside: "/mecanum/sticks", tool: true, noFoot: true },
  "mecanum/calibration.html": { setAside: "/mecanum/calibration", tool: true },
  "mecanum/rename.html": { setAside: "/mecanum/rename" },
  "mecanum/holonomic.html": { setAside: "/mecanum/holonomic" },
  "archive/index.html": { setAside: "/archive/" }
};
for (const name of fs.readdirSync(path.join(root, "mecanum/lessons"))) {
  if (name.endsWith(".html")) PAGES["mecanum/lessons/" + name] = { active: "/mecanum/lessons/" };
}

function link(href, label, current) {
  return '<a href="' + href + '"' + (href === current ? ' class="active" aria-current="page"' : "") + ">" + label + "</a>";
}

function navFor(page) {
  let html = '<!-- mlk-nav: written by scripts/site-nav.js, do not edit here -->\n' +
    '<nav class="mlk-nav' + (page.tool ? " mlk-nav-tool" : "") + '" aria-label="Robotics pages">' +
    '<a class="mlk-nav-logo" href="https://makerlabkids.com/"><img src="/img/lockup.png" alt="Maker Lab Kids" width="300" height="99"></a>' +
    NAV.map(function (item) { return link(item[0], item[1], page.active); }).join("") +
    (page.tool ? '<a class="mlk-nav-back" href="/archive/" onclick="if (document.referrer.indexOf(location.origin) === 0) { history.back(); return false; }">&larr; Back</a>' : "") +
    "</nav>\n";
  if (page.section) {
    const section = SECTIONS[page.section];
    html += '<nav class="mlk-subnav" aria-label="' + section.title + ' pages"><span>' + section.title + ':</span>' +
      section.pages.map(function (item) { return link(item[0], item[1], page.here); }).join("") + "</nav>\n";
  }
  if (page.setAside) {
    html += '<nav class="mlk-subnav" aria-label="Set aside for now"><span>Set aside for now:</span>' +
      SET_ASIDE.map(function (item) { return link(item[0], item[1], page.setAside); }).join("") + "</nav>\n";
  }
  return html + "<!-- /mlk-nav -->";
}

const FOOT = '<!-- mlk-foot -->\n<footer class="mlk-foot"><a href="/">Maker Lab Kids Robotics</a>' +
  '<span class="sep">&middot;</span><a href="https://makerlabkids.com/">makerlabkids.com</a>' +
  '<span class="sep">&middot;</span><a href="/archive/">Archive</a></footer>\n<!-- /mlk-foot -->';
const CSS = '<link rel="stylesheet" href="/css/nav.css">';

function render(source, page) {
  let out = source;
  const nav = navFor(page);
  if (/<!-- mlk-nav[\s\S]*?<!-- \/mlk-nav -->/.test(out)) {
    out = out.replace(/<!-- mlk-nav[\s\S]*?<!-- \/mlk-nav -->/, function () { return nav; });
  } else {
    out = out.replace(/<body[^>]*>/, function (body) { return body + "\n" + nav; });
  }
  out = out.replace(/\n?<!-- mlk-foot -->[\s\S]*?<!-- \/mlk-foot -->/, "");
  if (!page.noFoot) out = out.replace(/<\/body>/, function () { return FOOT + "\n</body>"; });
  if (out.indexOf(CSS) < 0) out = out.replace(/<\/head>/, function () { return CSS + "\n</head>"; });
  return out;
}

const check = process.argv.indexOf("--check") >= 0;
let problems = 0;
for (const [file, page] of Object.entries(PAGES)) {
  const full = path.join(root, file);
  if (!fs.existsSync(full)) { console.log("FAIL missing page " + file); problems++; continue; }
  const source = fs.readFileSync(full, "utf8");
  const out = render(source, page);
  if (out === source) continue;
  if (check) { console.log("FAIL " + file + ": its nav or footer differs from site-nav.js (run node site-nav.js)"); problems++; }
  else { fs.writeFileSync(full, out); console.log("wrote " + file); }
}
if (check) console.log(problems ? problems + " FAILED" : "PASS every page has the shared nav and footer");
process.exit(problems ? 1 : 0);
