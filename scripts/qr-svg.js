// Prints an SVG QR code for a URL, using the site's own copy of qrcode.js.
// Run: node scripts/qr-svg.js https://robotics.makerlabkids.com/mecanum/light
const fs = require("fs");
const path = require("path");
const source = fs.readFileSync(path.join(__dirname, "..", "mecanum", "vendor", "qrcode", "qrcode.js"), "utf8");
const qrcode = new Function(source + "\nreturn qrcode;")();
const url = process.argv[2];
const code = qrcode(0, "M");
code.addData(url, "Byte");
code.make();
const n = code.getModuleCount();
const quiet = 4;
let d = "";
for (let r = 0; r < n; r++) {
  for (let c = 0; c < n; c++) {
    if (code.isDark(r, c)) d += "M" + (c + quiet) + " " + (r + quiet) + "h1v1h-1z";
  }
}
const size = n + quiet * 2;
process.stdout.write('<svg class="qr" viewBox="0 0 ' + size + " " + size + '" role="img" aria-label="QR code for ' + url + '" shape-rendering="crispEdges">' +
  '<rect width="' + size + '" height="' + size + '" fill="#fff"/><path d="' + d + '" fill="#000"/></svg>');
