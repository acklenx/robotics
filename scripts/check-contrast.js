// Run: node scripts/check-contrast.js [page.html ...]   (default: every page on the site)
// WCAG contrast of every visible text element on every page: text colour vs
// the first opaque background behind it.
// Links are checked as both unvisited and visited (browsers hide :visited
// from scripts, so a page that leaves visited links unstyled gets flagged).
const { spawn, execSync } = require('child_process');
const path = require('path');
const puppeteer = require('puppeteer');
const PORT = 8196;
const root = path.resolve(__dirname, '..');
const files = process.argv.slice(2).length ? process.argv.slice(2) :
  execSync("find . -name '*.html' -not -path '*/vendor/*' -not -path '*/lafvin/*' -not -path '*/node_modules/*' -not -path '*/temp/*'", { cwd: root })
    .toString().trim().split('\n').map((f) => f.replace(/^\.\//, '')).sort();
(async () => {
  const server = spawn('python3', ['-m', 'http.server', String(PORT), '--bind', '0.0.0.0'], { cwd: root, stdio: 'ignore' });
  await new Promise((r) => setTimeout(r, 700));
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox', '--disable-gpu'] });
  let total = 0;
  for (const file of files) {
    const page = await browser.newPage();
    await page.setViewport({ width: 1100, height: 900 });
    await page.goto(`http://127.0.0.1:${PORT}/${file}`, { waitUntil: 'load', timeout: 30000 });
    await new Promise((r) => setTimeout(r, 300));
    // Open every <details> and every tab page so hidden text is measured too.
    await page.evaluate(() => {
      document.querySelectorAll('details').forEach((d) => { d.open = true; });
      document.querySelectorAll('.tab-page').forEach((p) => { p.style.display = 'block'; });
    });
    const problems = await page.evaluate(() => {
      function parse(c) {
        const m = c.match(/rgba?\(([^)]+)\)/);
        if (!m) return null;
        const p = m[1].split(',').map((x) => parseFloat(x));
        return { r: p[0], g: p[1], b: p[2], a: p.length > 3 ? p[3] : 1 };
      }
      function lum(c) {
        const f = (v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * f(c.r) + 0.7152 * f(c.g) + 0.0722 * f(c.b);
      }
      function blend(top, under) {
        const a = top.a;
        return { r: top.r * a + under.r * (1 - a), g: top.g * a + under.g * (1 - a), b: top.b * a + under.b * (1 - a), a: 1 };
      }
      // Background behind an element: composite translucent layers up the tree.
      function background(el) {
        const layers = [];
        for (let e = el; e; e = e.parentElement) {
          const c = parse(getComputedStyle(e).backgroundColor);
          if (c && c.a > 0) { layers.push(c); if (c.a >= 1) break; }
        }
        let bg = { r: 255, g: 255, b: 255, a: 1 };
        for (let i = layers.length - 1; i >= 0; i--) bg = blend(layers[i], bg);
        return bg;
      }
      function ratio(a, b) {
        const l1 = lum(a), l2 = lum(b);
        return (Math.max(l1, l2) + 0.05) / (Math.min(l1, l2) + 0.05);
      }
      function name(el) {
        return el.tagName.toLowerCase() + (el.id ? '#' + el.id : '') + (el.className && typeof el.className === 'string' ? '.' + el.className.trim().split(/\s+/).join('.') : '');
      }
      const out = {};
      const all = document.querySelectorAll('body *');
      for (const el of all) {
        const text = [...el.childNodes].filter((n) => n.nodeType === 3).map((n) => n.textContent.trim()).join(' ').trim();
        if (!text) continue;
        if (el.closest('svg')) continue;
        const style = getComputedStyle(el);
        if (style.visibility === 'hidden' || style.display === 'none' || el.getClientRects().length === 0) continue;
        if (el.closest('[disabled]') || el.disabled) continue;   // disabled controls are dimmed on purpose
        let fg = parse(style.color);
        if (!fg) continue;
        const bg = background(el);
        let opacity = 1;
        for (let e = el; e; e = e.parentElement) opacity *= parseFloat(getComputedStyle(e).opacity);
        fg = blend({ ...fg, a: fg.a * opacity }, bg);
        const size = parseFloat(style.fontSize), bold = parseInt(style.fontWeight, 10) >= 700;
        const large = size >= 24 || (size >= 18.66 && bold);
        const need = large ? 3 : 4.5;
        const r = ratio(fg, bg);
        const key = name(el) + ' ' + style.color + ' on rgb(' + [bg.r, bg.g, bg.b].map(Math.round).join(',') + ')';
        if (r < need) {
          if (!out[key]) out[key] = { ratio: r.toFixed(2), need, text: text.slice(0, 40), count: 0 };
          out[key].count++;
        }
        // Links: check the browser's default visited purple when the page sets no visited colour.
        if (el.tagName === 'A' && el.hasAttribute('href')) {
          // Any author colour on a link also applies once it is visited; only
          // links left on the browser's blue turn purple.
          const usesDefault = /rgb\(0, 0, 238\)/.test(style.color);
          if (usesDefault) {
            const visited = { r: 85, g: 26, b: 139, a: 1 };
            const rv = ratio(blend(visited, bg), bg);
            if (rv < need || usesDefault) {
              const vk = name(el) + ' (default link / visited colours) on rgb(' + [bg.r, bg.g, bg.b].map(Math.round).join(',') + ')';
              if (!out[vk]) out[vk] = { ratio: rv.toFixed(2), need, text: text.slice(0, 40), count: 0 };
              out[vk].count++;
            }
          }
        }
      }
      return out;
    });
    const keys = Object.keys(problems);
    total += keys.length;
    if (keys.length) {
      console.log('\n== ' + file);
      for (const k of keys) console.log(`  ${problems[k].ratio} < ${problems[k].need}  x${problems[k].count}  ${k}  "${problems[k].text}"`);
    }
    await page.close();
  }
  await browser.close();
  server.kill();
  console.log('\n' + (total ? total + ' low-contrast text styles' : 'no low-contrast text'));
})();
