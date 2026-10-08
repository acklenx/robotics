// Freezes the Mecanum Workshop app for the firmware it works with, and keeps
// mecanum/versions.json (which app goes with which firmware) up to date.
//
// The Uno has no room to stay compatible with every app, the website does:
// when a sketch change breaks the protocol (a new firmware line, 0.11.0),
// run this BEFORE changing the app, so robots still on the old line are
// offered the app that matches them (the app checks versions.json when it
// hears a firmware it does not support).
//
//   node scripts/snapshot-mecanum-app.js            freeze the current app into
//                                                   mecanum/v/<app version>/
//   node scripts/snapshot-mecanum-app.js --list     only refresh versions.json
//                                                   (the current app and latest firmware)
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const appDir = path.join(root, 'mecanum');
const appFile = path.join(appDir, 'index.html');
const listFile = path.join(appDir, 'versions.json');
const sketchFile = path.join(root, 'mecanum_holonomic', 'mecanum_holonomic.ino');

const app = fs.readFileSync(appFile, 'utf8');
const appVersion = /var APP_VERSION = "([\d.]+)"/.exec(app)[1];
const firmwareCompat = /var FIRMWARE_COMPAT = "([\d.]+)"/.exec(app)[1];
const latestFirmware = /#define FIRMWARE_VERSION "([\d.]+)"/.exec(fs.readFileSync(sketchFile, 'utf8'))[1];
const line = firmwareCompat.split('.').slice(0, 2).join('.');   // 0.10: every 0.10.x build

const list = fs.existsSync(listFile) ? JSON.parse(fs.readFileSync(listFile, 'utf8')) : { versions: [] };
list.latestFirmware = latestFirmware;
const listOnly = process.argv.includes('--list');
list.versions = list.versions.filter((entry) => listOnly ? entry.path !== '/mecanum/' : entry.app !== appVersion || entry.path === '/mecanum/');

if (listOnly) {
  list.versions.unshift({ app: appVersion, path: '/mecanum/', firmwareMin: firmwareCompat, firmwareMax: line + '.9999' });
} else {
  // A frozen copy two folders down: relative links climb two more, it reads
  // the live versions.json, and search engines skip it.
  const frozenDir = path.join(appDir, 'v', appVersion);
  fs.mkdirSync(frozenDir, { recursive: true });
  const frozen = app
    .replace(/(["'(])\.\.\//g, '$1../../../')
    .replace(/href="sticks\.html"/g, 'href="../../sticks.html"')
    .replace(/href="lessons\//g, 'href="../../lessons/')
    .replace(/fetch\("versions\.json"/g, 'fetch("../../versions.json"')
    .replace(/<head>/, '<head>\n<meta name="robots" content="noindex">');
  fs.writeFileSync(path.join(frozenDir, 'index.html'), frozen);
  list.versions.push({ app: appVersion, path: '/mecanum/v/' + appVersion + '/', firmwareMin: firmwareCompat, firmwareMax: line + '.9999' });
  console.log('froze Workshop v' + appVersion + ' (firmware ' + firmwareCompat + ' - ' + line + '.x) in ' + path.relative(root, frozenDir));
  console.log('now: change the app, raise FIRMWARE_COMPAT to the new line, and run with --list');
}
list.versions.sort((a, b) => (a.path === '/mecanum/' ? -1 : b.path === '/mecanum/' ? 1 : b.app.localeCompare(a.app, undefined, { numeric: true })));
fs.writeFileSync(listFile, JSON.stringify(list, null, 2) + '\n');
console.log('wrote ' + path.relative(root, listFile));
