#!/usr/bin/env node
// Renders the HTML/CSS design in headless Chrome and diffs it against the LVGL
// rendering from the simulator.
//
// Why this exists: the design in design/src/weather-app/ is the source of truth
// for visual intent, but until now the only way to compare it against the port
// was to look at both and remember. This makes the comparison a file.
//
// Two facts decide how it works:
//
//   1. `Weather App.dc.html` is not a standalone page. It runs inside Claude
//      Design's canvas: the runtime in support.js boots on DOMContentLoaded and
//      throws unless window.React / window.ReactDOM already exist. So the host
//      page injects React before support.js — see buildHost().
//   2. The design pulls its sibling component (WeatherIcon.dc.html) with fetch(),
//      which CORS blocks under file://. Loaded that way every icon renders as an
//      empty grey box. Hence the throwaway static server instead of a file: URL.
//
// No npm dependencies: Chrome is driven through its own --screenshot flag, and
// the pixel diff is done by Chrome too — two stacked images under
// mix-blend-mode: difference — rather than by pulling in an image library.
//
//   node tools/extract-design.mjs                        render every screen
//   node tools/extract-design.mjs --compare shots        ... and diff each against shots/
//   node tools/extract-design.mjs --screen settings      just the one
//
// Requires: node, google-chrome (or chromium), and design/vendor/react*.js
// (fetch them with --vendor once, needs network).

import http from 'node:http';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const DESIGN_DIR = path.join(REPO, 'design/src/weather-app');
const VENDOR_DIR = path.join(REPO, 'design/vendor');
const DESIGN_FILE = 'Weather App.dc.html';
const HOST_FILE = '_host.html'; // generated, gitignored

const VENDOR = [
  ['react.js', 'https://cdn.jsdelivr.net/npm/react@18.3.1/umd/react.production.min.js'],
  ['react-dom.js', 'https://cdn.jsdelivr.net/npm/react-dom@18.3.1/umd/react-dom.production.min.js'],
];

const MIME = {
  '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
  '.json': 'application/json', '.png': 'image/png', '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml', '.woff2': 'font/woff2',
};

// Screens other than the main one sit behind interactions in the design. Rather
// than synthesising clicks into React's handlers, each screen is opened by
// patching the component's *initial* state — the `state = { ... }` block in the
// dc-script — so the page comes up already on that screen. Deterministic, and it
// survives a re-layout of the controls that would open it.
//
// Every patch is applied only inside the state block: several of these field
// names appear again in setState() calls further down the file.
const SCREENS = {
  main: [],
  settings: [
    ["settingsOpen: false,", "settingsOpen: true,"],
  ],
  detail: [
    ["selectedDayIndex: null,", "selectedDayIndex: 0,"],
  ],
  search: [
    ["searchOpen: false,", "searchOpen: true,"],
    ["searchQuery: '',", "searchQuery: 'Tri',"],
    // Shape as Open-Meteo's geocoding API returns it — the design maps
    // r.name / r.admin1 / r.country itself. Seeded rather than fetched so the
    // screenshot does not depend on a live search.
    ["searchResults: [],",
     "searchResults: [" +
     "{ name: 'Trier', admin1: 'Rheinland-Pfalz', country: 'Germany', latitude: 49.75, longitude: 6.64 }," +
     "{ name: 'Trieste', admin1: 'Friuli Venezia Giulia', country: 'Italy', latitude: 45.65, longitude: 13.78 }" +
     "],"],
  ],
  wifi: [
    ["wifiOpen: false,", "wifiOpen: true,"],
    // The list the design's own scanWifi() produces after its 1.4 s delay.
    ["wifiNetworks: [],",
     "wifiNetworks: [" +
     "{ ssid: 'Home-5G', strength: 3, secured: true }," +
     "{ ssid: 'Home-2.4G', strength: 3, secured: true }," +
     "{ ssid: 'FRITZ!Box 7590', strength: 2, secured: true }," +
     "{ ssid: 'Cafe Free WiFi', strength: 1, secured: false }," +
     "{ ssid: 'Nachbar_WLAN', strength: 1, secured: true }" +
     "],"],
  ],
};

// ---- arguments -------------------------------------------------------------

function parseArgs(argv) {
  const opts = { out: 'shots-design', port: 8731, compare: null, vendor: false, keep: false, screen: 'all' };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--out' && argv[i + 1]) opts.out = argv[++i];
    else if (a === '--port' && argv[i + 1]) opts.port = Number(argv[++i]);
    else if (a === '--compare' && argv[i + 1]) opts.compare = argv[++i];
    else if (a === '--screen' && argv[i + 1]) opts.screen = argv[++i];
    else if (a === '--vendor') opts.vendor = true;
    else if (a === '--keep-host') opts.keep = true;
    else if (a === '--help' || a === '-h') { usage(); process.exit(0); }
    else { console.error(`Unknown argument: ${a}\n`); usage(); process.exit(2); }
  }
  return opts;
}

function usage() {
  console.log(`Usage: node tools/extract-design.mjs [options]

  --out <dir>      where the PNGs go (default: shots-design)
  --screen <list>  which screens: main, settings, detail, search, wifi —
                   comma-separated, or "all" (default)
  --compare <dir>  also build a side-by-side + difference sheet per screen
                   against the LVGL screenshots in <dir> (produce them with
                   ./scripts/sim.sh --shots <dir>)
  --vendor         (re-)download the React UMD builds into design/vendor
  --port <n>       port for the throwaway static server (default: 8731)
  --keep-host      leave the generated _host.html in place for inspection

Screens other than main are opened by patching the design component's initial
state, not by clicking — see SCREENS at the top of this file. The simulator's
first-boot state has no counterpart in the design and is skipped.`);
}

// ---- pieces ----------------------------------------------------------------

async function ensureVendor(force) {
  await fsp.mkdir(VENDOR_DIR, { recursive: true });
  for (const [name, url] of VENDOR) {
    const dest = path.join(VENDOR_DIR, name);
    if (!force && fs.existsSync(dest)) continue;
    process.stdout.write(`  fetching ${name} ... `);
    const res = await fetch(url);
    if (!res.ok) throw new Error(`${url} returned ${res.status}`);
    await fsp.writeFile(dest, Buffer.from(await res.arrayBuffer()));
    console.log('ok');
  }
  for (const [name] of VENDOR) {
    if (!fs.existsSync(path.join(VENDOR_DIR, name))) {
      throw new Error(`design/vendor/${name} missing — run once with --vendor (needs network)`);
    }
  }
}

// Rewrites the `state = { ... }` block, and only that block.
function patchInitialState(src, patches) {
  if (patches.length === 0) return src;
  const start = src.indexOf('  state = {');
  if (start === -1) throw new Error(`${DESIGN_FILE}: no "state = {" block — did the design change?`);
  const end = src.indexOf('\n  };', start);
  if (end === -1) throw new Error(`${DESIGN_FILE}: the state block is not closed as expected`);

  let block = src.slice(start, end);
  for (const [find, replace] of patches) {
    if (!block.includes(find)) {
      throw new Error(`${DESIGN_FILE}: "${find}" not in the state block — the design's state has changed, update SCREENS`);
    }
    block = block.replace(find, replace);
  }
  return src.slice(0, start) + block + src.slice(end);
}

// The canvas runtime throws if React is not already on window when it boots, so
// the tags go in ahead of support.js rather than after it.
async function buildHost(screen) {
  const src = await fsp.readFile(path.join(DESIGN_DIR, DESIGN_FILE), 'utf8');
  const tag = '<script src="./support.js"></script>';
  if (!src.includes(tag)) throw new Error(`${DESIGN_FILE}: support.js tag not found — did the export format change?`);
  const injected = src.replace(tag,
    '<script src="../../vendor/react.js"></script>\n' +
    '<script src="../../vendor/react-dom.js"></script>\n' + tag);
  const dest = path.join(DESIGN_DIR, HOST_FILE);
  await fsp.writeFile(dest, patchInitialState(injected, SCREENS[screen]));
  return dest;
}

function startServer(root, port) {
  return new Promise((resolve, reject) => {
    const server = http.createServer((req, res) => {
      const rel = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
      const file = path.join(root, rel);
      if (!file.startsWith(root)) { res.writeHead(403).end(); return; }
      fs.readFile(file, (err, data) => {
        if (err) { res.writeHead(404).end(); return; }
        res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] ?? 'application/octet-stream' });
        res.end(data);
      });
    });
    server.on('error', reject);
    server.listen(port, '127.0.0.1', () => resolve(server));
  });
}

function findChrome() {
  const candidates = ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'];
  for (const c of candidates) {
    const probe = whichBinary(c);
    if (probe) return probe;
  }
  throw new Error(`no Chrome found — looked for: ${candidates.join(', ')}`);
}

// `which`, without a dependency.
function whichBinary(cmd) {
  for (const dir of (process.env.PATH ?? '').split(path.delimiter)) {
    const p = path.join(dir, cmd);
    try { fs.accessSync(p, fs.constants.X_OK); return p; } catch { /* keep looking */ }
  }
  return null;
}

function shoot(chrome, url, outFile, { width, height }) {
  return new Promise((resolve, reject) => {
    const args = [
      '--headless=new', '--disable-gpu', '--no-sandbox', '--hide-scrollbars',
      `--window-size=${width},${height}`,
      '--force-device-scale-factor=1',
      // The canvas runtime renders asynchronously; without a virtual-time budget
      // Chrome captures the empty document.
      '--virtual-time-budget=8000',
      `--screenshot=${outFile}`,
      url,
    ];
    const child = spawn(chrome, args, { stdio: ['ignore', 'ignore', 'pipe'] });
    let stderr = '';
    child.stderr.on('data', (d) => { stderr += d; });
    child.on('error', reject);
    child.on('close', (code) => {
      if (code !== 0 || !fs.existsSync(outFile)) {
        reject(new Error(`chrome exited ${code}\n${stderr.trim()}`));
      } else resolve();
    });
  });
}

// A comparison sheet: design, LVGL, and the two under mix-blend-mode:difference,
// where anything that matches goes black and every deviation lights up.
function comparisonPage(designRel, lvglRel, label) {
  return `<!doctype html><meta charset="utf-8"><title>${label}</title>
<style>
  body { margin:0; background:#0d0d12; color:#e9e9ed;
         font:13px/1.5 system-ui,sans-serif; padding:16px; }
  h2 { font-size:12px; letter-spacing:.08em; text-transform:uppercase;
       color:#9397ab; margin:18px 0 6px; font-weight:500; }
  h2:first-child { margin-top:0; }
  .frame { width:1024px; height:600px; position:relative; outline:1px solid #3f424d; }
  .frame img { position:absolute; inset:0; width:1024px; height:600px; display:block; }
  .diff { background:#000; }
  .diff img + img { mix-blend-mode:difference; }
  p { color:#75798c; max-width:1024px; margin:6px 0 0; }
</style>
<h2>Design — Weather App.dc.html in Chrome</h2>
<div class="frame"><img src="${designRel}"></div>
<h2>LVGL — the firmware's own rendering, from the simulator</h2>
<div class="frame"><img src="${lvglRel}"></div>
<h2>Difference</h2>
<div class="frame diff"><img src="${designRel}"><img src="${lvglRel}"></div>
<p>Black means the two agree. Everything that lights up is a deviation — bear in
mind that different text rasterisation alone makes every glyph glow faintly, so
read the shapes and positions, not the sparkle.</p>`;
}

// ---- main ------------------------------------------------------------------

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const outDir = path.resolve(REPO, opts.out);
  await fsp.mkdir(outDir, { recursive: true });

  const screens = opts.screen === 'all' ? Object.keys(SCREENS) : opts.screen.split(',');
  for (const s of screens) {
    if (!SCREENS[s]) {
      throw new Error(`unknown screen "${s}" — one of: ${Object.keys(SCREENS).join(', ')}, or all`);
    }
  }

  console.log('== Vendor ==');
  await ensureVendor(opts.vendor);

  const chrome = findChrome();
  const server = await startServer(REPO, opts.port);
  const base = `http://127.0.0.1:${opts.port}`;
  const hostRel = path.relative(REPO, path.join(DESIGN_DIR, HOST_FILE))
    .split(path.sep).map(encodeURIComponent).join('/');

  try {
    for (const screen of screens) {
      console.log(`== ${screen} ==`);
      await buildHost(screen);

      const designPng = path.join(outDir, `design-${screen}.png`);
      await shoot(chrome, `${base}/${hostRel}`, designPng, { width: 1024, height: 600 });
      console.log(`  ${path.relative(REPO, designPng)}`);

      if (!opts.compare) continue;

      const lvgl = path.resolve(REPO, opts.compare, `${screen}.png`);
      if (!fs.existsSync(lvgl)) {
        console.log(`  skipping the comparison: ${path.relative(REPO, lvgl)} not there`);
        continue;
      }
      const sheetHtml = path.join(outDir, `_compare-${screen}.html`);
      await fsp.writeFile(sheetHtml, comparisonPage(
        path.relative(outDir, designPng),
        path.relative(outDir, lvgl),
        `Design vs LVGL — ${screen}`));
      const sheetPng = path.join(outDir, `compare-${screen}.png`);
      const sheetUrl = `${base}/${path.relative(REPO, sheetHtml).split(path.sep).join('/')}`;
      await shoot(chrome, sheetUrl, sheetPng, { width: 1064, height: 1980 });
      console.log(`  ${path.relative(REPO, sheetPng)}`);
    }
  } finally {
    server.close();
    if (!opts.keep) await fsp.rm(path.join(DESIGN_DIR, HOST_FILE), { force: true });
  }

  console.log('== Done ==');
}

main().catch((err) => {
  console.error(`\nFEHLER: ${err.message}`);
  process.exit(1);
});
