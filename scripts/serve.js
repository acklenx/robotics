// Local preview that serves the site the way the Worker does: /x serves x.html,
// a folder serves its index.html. Run: node serve.js [port]   (default 8191)
const http = require("http");
const fs = require("fs");
const path = require("path");
const root = path.resolve(__dirname, "..");
const port = parseInt(process.argv[2] || "8191", 10);
const types = { ".html": "text/html", ".css": "text/css", ".js": "application/javascript", ".png": "image/png",
  ".svg": "image/svg+xml", ".json": "application/json", ".pdf": "application/pdf", ".ino": "text/plain",
  ".mp3": "audio/mpeg", ".gif": "image/gif", ".ico": "image/x-icon", ".cur": "image/x-icon", ".stl": "model/stl" };
http.createServer(function (request, response) {
  const wanted = decodeURIComponent(request.url.split("?")[0]);
  let file = path.join(root, wanted);
  if (fs.existsSync(file) && fs.statSync(file).isDirectory()) file = path.join(file, "index.html");
  else if (!fs.existsSync(file) && fs.existsSync(file + ".html")) file = file + ".html";
  if (!file.startsWith(root) || !fs.existsSync(file)) { response.writeHead(404); response.end("404 " + wanted); return; }
  response.writeHead(200, { "Content-Type": types[path.extname(file)] || "application/octet-stream", "Cache-Control": "no-store" });
  fs.createReadStream(file).pipe(response);
}).listen(port, "0.0.0.0", function () { console.log("http://127.0.0.1:" + port + "/"); });
