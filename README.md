# robotics.makerlabkids.com

The Maker Lab Kids robotics class site: the mecanum robot driven and programmed
from an IR remote (ir.ino), its guides, challenges, light bot and wiring, the Blocks
editor, and the lesson pages. The Workshop app, Sticks, calibration and rename pages
are set aside for later; the 2WD robot is retired to archive/. See CLAUDE.md, "What
the site shows".

It is a static site with no build step. The source lives in this GitHub
repository (acklenx/robotics); a Cloudflare Worker (static assets) deploys every push to main and
serves it at https://robotics.makerlabkids.com (build command: none; output
directory: /). `_headers` carries the Content-Security-Policy, which has no
unsafe-eval: the Blocks page runs its programs in an interpreter for that reason.

The pages that need a robot (Web Serial, Web Bluetooth) only work over https
or on localhost. `npm run serve` serves the site on port 8191.

## Tests

The tooling lives in `scripts/` (its package.json is there on purpose: one at
the root would make the Cloudflare build try to install dependencies for a site
that needs none).

```
cd scripts && npm install
npm run check:shared     # the three sketches share one motion engine
npm run check:blocks     # the Blocks page under the real CSP (headless Chrome)
npm run test:mecanum     # every simulator suite (needs arduino-cli + simavr)
npm run test:site        # shared nav on every page, every page parses, every internal link resolves
```

(`npm run test:workshop`, the 2WD app's headless test, has crashed in jsdom
since before the move: "fetch is not defined" inside Blockly. Not yet fixed.)

Enable the 20 MB guard once per clone: `git config core.hooksPath .githooks`.

This repository is the source: edit and commit here. (Until 2026-10-21 these
pages lived under /abot/ in the makerlabkids repository, which now redirects
/abot/* to this site.) Wiring for the robot and its light sensors:
`mecanum/wiring.html`.

## Where things are

```
index.html            home
mecanum/              the live pages (remote-guide, ir, challenges, light, wiring, blocks),
                      lessons/, and the set-aside app, sticks, calibration, rename, holonomic
ir/ mecanum_holonomic/ calibration/ rename/   the four sketches
resources/            build PDFs (assembly instructions, wiring guide)
archive/              /archive/: the set-aside list, the retired 2WD robot (2wd/) and flyers
css/                  site.css (home, lessons, archive) and nav.css (the shared nav bar)
js/lesson-page.js     the lesson pages' script
vendor/               Blockly, self-hosted (the Workshop app and the 2WD app use it)
scripts/              tests and tools; site-nav.js writes the nav into every page
_redirects            old URLs (2WD pages, numbered lessons) to their new homes
.assetsignore         repo files the site does not serve
```
