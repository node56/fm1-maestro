// SPDX-License-Identifier: GPL-3.0-only
//
// Maestro web installer: flashes the Maestro .fwsc onto an M-VAVE FM-1 over
// USB-MIDI using WebMIDI (Chrome/Edge, HTTPS). The update protocol lives in
// vendor/fm1ota.js and the package parsing in vendor/fm1pkg.js, both by
// Leo Kuroshita / Hügelton Instruments (GPL-3.0), vendored verbatim.

import { Updater } from './vendor/fm1ota.js';
import { productOf, logicalImage, sha256hex } from './vendor/fm1pkg.js';

const FW_URL = 'fw/maestro-v0.20.fwsc';
const IS_OTA = (id) => /^ota-/i.test(id.model);

const $ = (id) => document.getElementById(id);
const ui = {
  status: $('status'), device: $('deviceInfo'), pkg: $('packageInfo'),
  log: $('log'), bar: $('bar'), barLabel: $('barLabel'), barWrap: $('barWrap'),
  btnConnect: $('btnConnect'), btnInstall: $('btnInstall'),
  btnResume: $('btnResume'), btnDisconnect: $('btnDisconnect'),
};

let access = null, updater = null;
let device = null;        // { link, id, name } — running firmware
let loaderDev = null;     // { link, id, name } — update loader (interrupted install)
let image = null, product = null;

function logLine(msg) {
  const t = new Date().toLocaleTimeString();
  ui.log.textContent += `[${t}] ${msg}\n`;
  ui.log.scrollTop = ui.log.scrollHeight;
}

function setStatus(msg, kind = '') {
  ui.status.textContent = msg;
  ui.status.className = 'status ' + kind;
}

function setButtons({ connect, install, resume, disconnect }) {
  ui.btnConnect.disabled = !connect;
  ui.btnInstall.disabled = !install;
  ui.btnResume.disabled = !resume;
  ui.btnDisconnect.disabled = !disconnect;
}

function setProgress(pct, label) {
  ui.barWrap.style.display = 'block';
  ui.bar.style.width = Math.max(0, Math.min(100, pct)) + '%';
  ui.barLabel.textContent = label || '';
}

const ERROR_HELP = {
  notfound: 'FM-1 not found. Check the USB cable (it must be a data cable, not charge-only) and close any other app holding the MIDI port (DAW, another installer tab).',
  lost: 'The FM-1 was disconnected mid-update. If it is sitting in update mode, reconnect it and use “Resume interrupted update”.',
  stopped: 'The device stopped answering. Replug the USB cable and try again.',
  noloader: 'The update loader did not appear. Replug the USB cable and press Install again — the FM-1 stays in update mode until an install finishes.',
  noreturn: 'The FM-1 did not come back after writing. Power-cycle it (off, then on).',
  badreq: 'Protocol error talking to the FM-1. Replug and try again.',
};

function showError(e) {
  console.error(e);
  const help = ERROR_HELP[e.code] || '';
  setStatus('Error: ' + e.message, 'err');
  logLine('ERROR [' + (e.code || '?') + ']: ' + e.message);
  if (help) logLine(help);
  if (e.detail) logLine('detail: ' + e.detail);
  setButtons({ connect: false, install: !!device, resume: !!loaderDev, disconnect: true });
}

async function loadPackage() {
  setStatus('Downloading firmware…');
  const res = await fetch(FW_URL);
  if (!res.ok) throw new Error('could not download ' + FW_URL + ' (' + res.status + ')');
  const raw = new Uint8Array(await res.arrayBuffer());
  product = productOf(raw);
  image = logicalImage(raw);
  const sha = await sha256hex(raw);
  ui.pkg.innerHTML =
    '<b>Package:</b> ' + escapeHtml(product) +
    ' <span class="dim">(' + (raw.length / 1024).toFixed(0) + ' KB, sha256 ' + sha.slice(0, 16) + '…)</span>';
  logLine('package ' + product + ' loaded (' + image.length + ' image bytes)');
}

function escapeHtml(s) {
  return s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

async function connect() {
  setButtons({ connect: false, install: false, resume: false, disconnect: false });
  try {
    if (!navigator.requestMIDIAccess) {
      throw Object.assign(new Error('WebMIDI is not available in this browser.'),
        { code: 'nowebmidi', message: 'WebMIDI is not available — use Chrome or Edge on desktop.' });
    }
    setStatus('Requesting MIDI access… (approve the browser prompt)');
    try {
      access = await navigator.requestMIDIAccess({ sysex: true });
    } catch (_) {
      throw Object.assign(new Error('MIDI access was denied.'), { code: 'denied' });
    }
    updater = new Updater(access);
    await loadPackage();

    setStatus('Looking for FM-1…');
    logLine('probing MIDI ports…');
    loaderDev = await updater.find((id) => IS_OTA(id));
    if (!loaderDev) device = await updater.find((id) => !IS_OTA(id));

    if (loaderDev) {
      const [model] = product.split('_');
      ui.device.innerHTML = '<b>Device:</b> ' + escapeHtml(loaderDev.id.text) +
        ' <span class="warn">— in update mode (a previous install was interrupted)</span>';
      setStatus('FM-1 found in update mode.', 'warn');
      logLine('found loader ' + loaderDev.id.text + ' on "' + loaderDev.name + '"');
      if (loaderDev.id.model !== 'ota-' + model) {
        logLine('note: the loader is for ' + loaderDev.id.model + '; the package is for ' + model +
          ' — resume may refuse a mismatched model');
      }
      setButtons({ connect: false, install: false, resume: true, disconnect: true });
    } else if (device) {
      ui.device.innerHTML = '<b>Device:</b> ' + escapeHtml(device.id.text) +
        ' <span class="dim">on "' + escapeHtml(device.name) + '"</span>';
      const [model] = product.split('_');
      if (device.id.model !== model) {
        setStatus('Model mismatch: device is ' + device.id.text + ', package is for ' + product + '.', 'err');
        logLine('refusing: model mismatch');
        device.link.close(); device = null;
        setButtons({ connect: true, install: false, resume: false, disconnect: true });
      } else {
        setStatus('FM-1 ready.', 'ok');
        logLine('found ' + device.id.text);
        setButtons({ connect: false, install: true, resume: false, disconnect: true });
      }
    } else {
      setStatus('FM-1 not found.', 'err');
      logLine(ERROR_HELP.notfound);
      setButtons({ connect: true, install: false, resume: false, disconnect: true });
    }
  } catch (e) { showError(e); }
}

function onStep(kind, arg) {
  if (kind === 'start') { logLine('starting the update on ' + arg); setProgress(2, 'Starting…'); }
  else if (kind === 'verify') { setProgress(5, 'Checking package… (' + arg + ' reads)'); }
  else if (kind === 'loader') { logLine('switching to update mode…'); setProgress(8, 'Entering update mode…'); }
  else if (kind === 'write') {
    const pct = Math.min(99, Math.round(arg * 512 * 100 / image.length));
    setProgress(pct, 'Writing… ' + pct + '% — do not unplug');
  }
  else if (kind === 'reboot') { logLine('restarting…'); setProgress(100, 'Restarting…'); }
  else if (kind === 'done') {
    setProgress(100, 'Done');
    setStatus('Done — the FM-1 now runs ' + arg + '.', 'ok');
    logLine('done: the FM-1 runs ' + arg);
  }
}

async function install() {
  const target = device || loaderDev;
  if (!target || !image) return;
  if (!confirm('Install ' + product + ' on ' + target.id.text +
      '?\n\nDo not unplug the FM-1 while it is writing.')) return;
  setButtons({ connect: false, install: false, resume: false, disconnect: false });
  ui.barWrap.style.display = 'block';
  try {
    if (loaderDev) {
      logLine('resuming interrupted update…');
      await updater.resume(image, onStep, product);
    } else {
      await updater.install(image, product, onStep);
    }
    device = null; loaderDev = null;
    setButtons({ connect: true, install: false, resume: false, disconnect: true });
  } catch (e) { showError(e); }
}

function disconnect() {
  try { if (device) device.link.close(); } catch (_) {}
  try { if (loaderDev) loaderDev.link.close(); } catch (_) {}
  device = loaderDev = null;
  ui.device.innerHTML = '<span class="dim">Not connected.</span>';
  setStatus('Disconnected.');
  setButtons({ connect: true, install: false, resume: false, disconnect: false });
}

ui.btnConnect.addEventListener('click', connect);
ui.btnInstall.addEventListener('click', install);
ui.btnResume.addEventListener('click', install);
ui.btnDisconnect.addEventListener('click', disconnect);

setStatus('Connect your FM-1 over USB, then press Connect.');
logLine('Maestro web installer ready. Needs Chrome/Edge with WebMIDI (SysEx) access.');
