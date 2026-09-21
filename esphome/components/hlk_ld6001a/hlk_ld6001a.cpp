#include "hlk_ld6001a.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/components/network/util.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::hlk_ld6001a {

static const char *const TAG = "hlk_ld6001a";
// Default AT+DEBUG=0 protocol header -- recognized and checksum-validated
// so it can be cleanly consumed off the wire (and feed the watchdog), but
// never decoded further. See hlk_ld6001a.h's header comment.
static const uint8_t OLD_HEADER[2] = {0x55, 0xAA};
// AT+DEBUG=3 ("demonstration mode") frame header -- the only frame this
// component actually decodes. See PROTOCOL.md.
static const uint8_t DEBUG3_HEAD[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
static const char AT_OK_TOKEN[] = "AT+OK";
static const char AT_ERR_TOKEN[] = "AT+ERR";
// Generous headroom for up to 10 tracked people (protocol max) plus a
// point cloud the manual claims is "always 0" but this component never
// assumes -- see the 6001B project's own MAX_BUFFER comment for why an
// early too-tight estimate is worse than headroom (repeatedly
// underestimated on real hardware there).
static const size_t MAX_BUFFER = 6144;
static const size_t MAX_DEBUG3_FRAME_LEN = 4096;

static void send_command(uart::UARTDevice *dev, const char *cmd) {
  dev->write_array(reinterpret_cast<const uint8_t *>(cmd), strlen(cmd));
}

// 3D viewer page -- direct port of the HLK-LD6001B project's VIEWER_HTML
// (see that file's header comment for the Three.js/CDN rationale, kept
// unchanged here). Adapted for a SINGLE target stream (id+x+y+z+vx+vy+vz
// per target, straight from AT+DEBUG=3) instead of two streams to
// reconcile, and for this module's own settings (AT+DPKTH/AT+RANGE/
// AT+HEIGHTD/single rectangular zone) instead of the 6001B's
// sensitivity/range/scan/monitor/heartbeat/6-zone panel.
static const char VIEWER_HTML[] = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>HLK-LD6001A - Carte 3D</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;700&display=swap" rel="stylesheet">
<style>
  :root {
    --bg:#0a0e14; --panel-bg:rgba(18,24,34,0.78); --panel-border:rgba(0,212,255,0.18);
    --accent:#00d4ff; --accent-dim:#0a7f9e; --text:#e8edf2; --text-dim:#8b98a9;
    --ok:#00e676; --warn:#ffb74d; --danger:#ff5252;
  }
  html, body { margin:0; padding:0; overflow:hidden; background:var(--bg); color:var(--text); font-family:'Inter',system-ui,sans-serif; }
  canvas { display:block; }
  .mono { font-family:'JetBrains Mono',monospace; }

  @keyframes status-pulse { 0%,100% { box-shadow:0 0 6px currentColor; } 50% { box-shadow:0 0 14px 2px currentColor; } }
  #app-header .status-dot.ok { animation:status-pulse 2s ease-in-out infinite; }

  #app-header {
    position:absolute; top:0; left:0; right:0; height:52px; z-index:20;
    display:flex; align-items:center; gap:18px; padding:0 18px; box-sizing:border-box;
    background:var(--panel-bg); backdrop-filter:blur(12px); -webkit-backdrop-filter:blur(12px);
    border-bottom:1px solid var(--panel-border);
  }
  #app-header .brand { display:flex; flex-direction:column; line-height:1.15; }
  #app-header .brand .name { font-size:15px; font-weight:700; letter-spacing:0.06em; }
  #app-header .brand .sub { font-size:10px; color:var(--text-dim); letter-spacing:0.08em; text-transform:uppercase; }
  #app-header .status-pill { display:flex; align-items:center; gap:7px; font-size:11px; color:var(--text-dim); letter-spacing:0.04em; text-transform:uppercase; }
  #app-header .status-dot { width:8px; height:8px; border-radius:50%; background:var(--danger); box-shadow:0 0 8px var(--danger); transition:background .3s, box-shadow .3s; }
  #app-header .status-dot.ok { background:var(--ok); color:var(--ok); box-shadow:0 0 8px var(--ok); }
  #app-header .count-readout { margin-left:auto; text-align:right; }
  #app-header .count-readout .value { font-family:'JetBrains Mono',monospace; font-size:26px; font-weight:700; color:var(--accent); line-height:1; }
  #app-header .count-readout .label { font-size:10px; color:var(--text-dim); letter-spacing:0.08em; text-transform:uppercase; }

  #target-table {
    position:absolute; top:52px; left:0; right:0; z-index:20;
    display:grid; grid-template-columns:repeat(3, 1fr); grid-template-rows:repeat(2, 1fr);
    gap:1px; background:var(--panel-border); border-bottom:1px solid var(--panel-border);
  }
  .target-cell {
    display:flex; flex-direction:column; align-items:center; justify-content:center;
    background:var(--panel-bg); backdrop-filter:blur(12px); -webkit-backdrop-filter:blur(12px);
    font-family:'JetBrains Mono',monospace; color:var(--text-dim); transition:background-color .2s, color .2s;
    padding:7px 4px; box-sizing:border-box; min-height:56px;
  }
  .target-cell.active { color:#04141a; }
  .target-cell-title { font-size:11px; font-weight:700; letter-spacing:0.04em; }
  .target-cell-pos, .target-cell-vel { font-size:10px; opacity:0.9; margin-top:2px; }

  #sidebar {
    position:absolute; top:144px; left:14px; bottom:14px; z-index:10; width:300px;
    background:var(--panel-bg); backdrop-filter:blur(12px); -webkit-backdrop-filter:blur(12px);
    border:1px solid var(--panel-border); border-radius:10px;
    padding:14px 16px; font-size:13px; line-height:1.5; overflow-y:auto; box-sizing:border-box;
    box-shadow:0 8px 32px rgba(0,0,0,0.4);
  }

  #room-form, #radar-form { margin-top:10px; padding-top:8px; border-top:1px solid var(--panel-border); }
  .tabs { display:flex; gap:4px; margin-top:14px; border-top:1px solid var(--panel-border); padding-top:12px; }
  .tabs button {
    flex:1; background:transparent; color:var(--text-dim); border:1px solid var(--panel-border);
    padding:7px 4px; cursor:pointer; font-size:11px; letter-spacing:0.03em; text-transform:uppercase;
    border-radius:6px; transition:background .15s, color .15s, border-color .15s;
  }
  .tabs button:hover { color:var(--text); border-color:var(--accent-dim); }
  .tabs button.active { background:var(--accent); color:#04141a; border-color:var(--accent); font-weight:600; }
  .tab-content { display:none; }
  .tab-content.active { display:block; animation:tab-fade-in .25s ease; }
  @keyframes tab-fade-in { from { opacity:0; transform:translateY(4px); } to { opacity:1; transform:translateY(0); } }

  #room-form label, #radar-form label { display:block; font-size:11px; color:var(--text-dim); margin-top:10px; letter-spacing:0.02em; }
  #room-form input, #room-form select, #radar-form input, #radar-form select {
    width:100%; box-sizing:border-box; margin-top:4px; background:rgba(255,255,255,0.04); color:var(--text);
    border:1px solid var(--panel-border); border-radius:5px; padding:6px 8px; font-size:13px; font-family:'JetBrains Mono',monospace;
  }
  #room-form input:focus, #radar-form input:focus, #room-form select:focus { outline:none; border-color:var(--accent); }
  #room-form button, #radar-form button, #console-form button {
    margin-top:10px; width:100%; padding:8px; background:var(--accent); color:#04141a; border:none;
    border-radius:6px; cursor:pointer; font-weight:600; font-size:12px; letter-spacing:0.03em; text-transform:uppercase;
    transition:filter .15s;
  }
  #room-form button:hover, #radar-form button:hover, #console-form button:hover { filter:brightness(1.15); }
  #room-error, #radar-error { color:var(--danger); font-size:11px; margin-top:6px; min-height:14px; }

  .live-status { background:rgba(0,230,118,0.08); border:1px solid rgba(0,230,118,0.3); border-radius:6px; padding:8px 10px; font-size:11px; margin-top:8px; font-family:'JetBrains Mono',monospace; line-height:1.5; word-break:break-all; }
  .live-status.stale { background:rgba(255,183,77,0.08); border-color:rgba(255,183,77,0.35); }
  .zone-row { border:1px solid var(--panel-border); border-radius:6px; padding:8px; margin-top:8px; }
  .zone-row .zone-title { font-size:11px; color:var(--accent); display:flex; justify-content:space-between; align-items:center; text-transform:uppercase; letter-spacing:0.03em; }
  .zone-row .zone-coords { display:grid; grid-template-columns:1fr 1fr; gap:6px; margin-top:6px; }
  .zone-row .zone-coords input { margin-top:0; }
  .slider-row { display:flex; align-items:center; gap:8px; }
  .slider-row input[type=range] { flex:1; margin-top:2px; accent-color:var(--accent); }
  .slider-value { font-size:12px; color:var(--accent); min-width:36px; text-align:right; font-family:'JetBrains Mono',monospace; }
  #console-form input[type=text], #console-log { font-family:'JetBrains Mono',monospace; }
  .console-result-ok { color:var(--ok); font-weight:600; }
  .console-result-fail { color:var(--danger); font-weight:600; }

  #aux-views {
    position:absolute; top:144px; right:14px; bottom:14px; z-index:10;
    display:flex; flex-direction:column; gap:10px; overflow-y:auto;
  }
  .aux-view-panel {
    width:330px; background:var(--panel-bg); backdrop-filter:blur(12px); -webkit-backdrop-filter:blur(12px);
    border:1px solid var(--panel-border); border-top:2px solid var(--accent-dim); border-radius:10px;
    overflow:hidden; box-shadow:0 8px 32px rgba(0,0,0,0.4); transition:border-top-color .2s;
  }
  .aux-view-panel:hover { border-top-color:var(--accent); }
  .aux-view-panel .aux-view-label {
    font-size:10px; color:var(--text-dim); letter-spacing:0.08em; text-transform:uppercase;
    padding:6px 10px; border-bottom:1px solid var(--panel-border);
  }
  .aux-view-panel canvas { width:330px; height:270px; display:block; }
  .aux-view-canvas-wrap { position:relative; }
  .axis-label {
    position:absolute; font-family:'JetBrains Mono',monospace; font-size:12px; font-weight:700;
    pointer-events:none; text-shadow:0 0 4px #000, 0 0 4px #000;
  }
  .axis-label-top { top:4px; left:50%; transform:translateX(-50%); }
  .axis-label-bottom { bottom:4px; left:50%; transform:translateX(-50%); }
  .axis-label-left { left:4px; top:50%; transform:translateY(-50%); }
  .axis-label-right { right:4px; top:50%; transform:translateY(-50%); }

  @media (max-width: 900px) {
    #sidebar { width:calc(100vw - 28px); }
    #aux-views { display:none; }
    #target-table { grid-template-columns:repeat(2, 1fr); grid-template-rows:repeat(3, 1fr); }
  }
</style>
</head>
<body>
<div id="app-header">
  <div class="brand">
    <span class="name">HLK-LD6001A</span>
    <span class="sub">Radar plafond</span>
  </div>
  <div class="status-pill"><span class="status-dot" id="status-dot"></span><span id="status-text">Connexion...</span></div>
  <div class="count-readout">
    <div class="value" id="count-value">-</div>
    <div class="label">Personnes detectees</div>
  </div>
</div>
<div id="target-table">
  <div class="target-cell" id="target-cell-0"><div class="target-cell-title"></div><div class="target-cell-pos"></div><div class="target-cell-vel"></div></div>
  <div class="target-cell" id="target-cell-1"><div class="target-cell-title"></div><div class="target-cell-pos"></div><div class="target-cell-vel"></div></div>
  <div class="target-cell" id="target-cell-2"><div class="target-cell-title"></div><div class="target-cell-pos"></div><div class="target-cell-vel"></div></div>
  <div class="target-cell" id="target-cell-3"><div class="target-cell-title"></div><div class="target-cell-pos"></div><div class="target-cell-vel"></div></div>
  <div class="target-cell" id="target-cell-4"><div class="target-cell-title"></div><div class="target-cell-pos"></div><div class="target-cell-vel"></div></div>
  <div class="target-cell" id="target-cell-5"><div class="target-cell-title"></div><div class="target-cell-pos"></div><div class="target-cell-vel"></div></div>
</div>
<div id="sidebar">
  <div class="tabs">
    <button id="tab-btn-room" class="active">Piece</button>
    <button id="tab-btn-radar">Radar</button>
    <button id="tab-btn-console">Console AT</button>
  </div>
  <div id="tab-room" class="tab-content active">
    <div id="room-form">
      <label>Largeur piece, X (m, max 4.5)</label>
      <input id="room-width" type="number" step="0.1" min="0.5" max="4.5">
      <label>Longueur piece, Y (m, max 4.5)</label>
      <input id="room-length" type="number" step="0.1" min="0.5" max="4.5">
      <label>Hauteur piece, Z (m, max 2.5)</label>
      <input id="room-height" type="number" step="0.1" min="0.5" max="2.5">
      <label>Position de montage du radar</label>
      <select id="room-mount">
        <option value="0">Plafond, centre de la piece</option>
        <option value="1">Mur</option>
      </select>
      <label id="room-radar-height-label">Hauteur du radar sur le mur (m)</label>
      <input id="room-radar-height" type="number" step="0.1" min="0.3" max="2.5">
      <label id="room-wall-offset-label">Distance depuis un coin du mur (m) -- prenez un coin de reference, mesurez jusqu'au radar</label>
      <input id="room-wall-offset" type="number" step="0.1" min="0" max="4.5">
      <div id="room-error"></div>
      <button id="room-save">Enregistrer la piece</button>
    </div>
  </div>
  <div id="tab-radar" class="tab-content">
    <div id="radar-form">
      <div id="radar-live" class="live-status">Etat radar : -</div>
      <div class="slider-row">
        <button id="radar-start">Demarrer</button>
        <button id="radar-stop">Arreter</button>
        <span id="radar-control-status" class="slider-value"></span>
      </div>
      <label>Sensibilite longue distance -- AT+DPKTH (1-9, plus grand = moins sensible)</label>
      <div class="slider-row">
        <input id="radar-dpkth" type="range" min="1" max="9" step="1">
        <span id="radar-dpkth-val" class="slider-value">-</span>
      </div>
      <label>Rayon de detection au sol -- AT+RANGE (m)</label>
      <div class="slider-row">
        <input id="radar-range" type="range" min="0.1" max="5" step="0.1">
        <span id="radar-range-val" class="slider-value">-</span>
      </div>
      <label>Hauteur d'installation -- AT+HEIGHTD (m)</label>
      <div class="slider-row">
        <input id="radar-heightd" type="range" min="0.5" max="5" step="0.1">
        <span id="radar-heightd-val" class="slider-value">-</span>
      </div>
      <div class="zone-row">
        <div class="zone-title"><span>Zone de detection rectangulaire</span>
        <label style="display:inline;margin:0"><input type="checkbox" id="zone-en"> Activer</label></div>
        <div class="zone-coords" style="margin-top:8px;">
          <div>
            <label style="margin-top:0">X- (m, -5.0..-0.2)</label>
            <input id="zone-x-neg" type="number" step="0.1" min="-5" max="-0.2">
          </div>
          <div>
            <label style="margin-top:0">X+ (m, 0.2..5.0)</label>
            <input id="zone-x-pos" type="number" step="0.1" min="0.2" max="5">
          </div>
          <div>
            <label style="margin-top:0">Y- (m, -5.0..-0.2)</label>
            <input id="zone-y-neg" type="number" step="0.1" min="-5" max="-0.2">
          </div>
          <div>
            <label style="margin-top:0">Y+ (m, 0.2..5.0)</label>
            <input id="zone-y-pos" type="number" step="0.1" min="0.2" max="5">
          </div>
        </div>
      </div>
      <div id="radar-error"></div>
      <button id="radar-save">Enregistrer les reglages</button>
    </div>
  </div>
  <div id="tab-console" class="tab-content">
    <div id="console-form">
      <label>Commande AT (sans le "AT+", ex: READ, DEBUG=3, STOP)</label>
      <input id="console-cmd" type="text" placeholder="READ">
      <button id="console-send">Envoyer</button>
      <div id="console-result" style="font-size:12px;margin-top:8px;min-height:16px;"></div>
      <div id="console-log" style="font-size:11px;color:var(--text-dim);margin-top:8px;max-height:200px;overflow-y:auto;"></div>
    </div>
  </div>
</div>
<div id="aux-views">
  <div class="aux-view-panel">
    <div class="aux-view-label">Vue de dessus</div>
    <div class="aux-view-canvas-wrap">
      <canvas id="canvas-top"></canvas>
      <span class="axis-label axis-label-top" id="top-label-top"></span>
      <span class="axis-label axis-label-bottom" id="top-label-bottom"></span>
      <span class="axis-label axis-label-left" id="top-label-left"></span>
      <span class="axis-label axis-label-right" id="top-label-right"></span>
    </div>
  </div>
  <div class="aux-view-panel">
    <div class="aux-view-label">Vue de face</div>
    <div class="aux-view-canvas-wrap">
      <canvas id="canvas-front"></canvas>
    </div>
  </div>
  <div class="aux-view-panel">
    <div class="aux-view-label">Vue de profil</div>
    <div class="aux-view-canvas-wrap">
      <canvas id="canvas-side"></canvas>
    </div>
  </div>
</div>
<script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/three@0.128.0/examples/js/controls/OrbitControls.js"></script>
<script>
function hlkShowBanner(msg) {
  if (document.getElementById('hlk-fatal-banner'))
    return;
  const el = document.createElement('div');
  el.id = 'hlk-fatal-banner';
  el.style.cssText = 'position:fixed;top:0;left:0;right:0;z-index:9999;' +
    'background:#ff5252;color:#fff;padding:10px 16px;' +
    'font:600 13px system-ui,sans-serif;text-align:center;box-shadow:0 2px 8px rgba(0,0,0,0.4);';
  el.textContent = msg;
  document.body.appendChild(el);
}
window.__hlkThreeOk = (typeof THREE !== 'undefined' && typeof THREE.OrbitControls !== 'undefined');
if (!window.__hlkThreeOk) {
  hlkShowBanner('Vue 3D indisponible (echec de chargement depuis le CDN -- verifiez la connexion Internet de cet appareil). Le compteur de personnes reste a jour ci-dessous.');
  (function () {
    async function hlkFallbackRefresh() {
      try {
        const res = await fetch('/hlk_targets.json');
        const data = await res.json();
        document.getElementById('count-value').textContent = data.count;
        document.getElementById('status-dot').classList.add('ok');
        document.getElementById('status-text').textContent = 'En ligne (mode degrade)';
        for (let i = 0; i < 6; i++) {
          const cell = document.getElementById('target-cell-' + i);
          const cellTitle = cell.querySelector('.target-cell-title');
          const cellPos = cell.querySelector('.target-cell-pos');
          if (i < data.targets.length) {
            const t = data.targets[i];
            cell.classList.add('active');
            cellTitle.textContent = 'Cible ' + (i + 1);
            cellPos.textContent = 'x=' + t.x.toFixed(1) + ' y=' + t.y.toFixed(1) + ' z=' + t.z.toFixed(1) + ' m';
          } else {
            cell.classList.remove('active');
            cellTitle.textContent = '';
            cellPos.textContent = '';
          }
        }
      } catch (e) {
        document.getElementById('status-dot').classList.remove('ok');
        document.getElementById('status-text').textContent = 'Deconnecte';
      }
    }
    setInterval(hlkFallbackRefresh, 1000);
    hlkFallbackRefresh();
  })();
}
</script>
<script>
try {
if (window.__hlkThreeOk) {
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0a0e14);
function getHeaderHeight() {
  return document.getElementById('app-header').offsetHeight + document.getElementById('target-table').offsetHeight;
}
function mainViewSize() {
  return {w: window.innerWidth, h: window.innerHeight - getHeaderHeight()};
}
const camera = new THREE.PerspectiveCamera(60, mainViewSize().w / mainViewSize().h, 0.1, 100);
camera.position.set(3, 1.5, 5);
const renderer = new THREE.WebGLRenderer({antialias: true});
renderer.domElement.style.position = 'absolute';
renderer.domElement.style.left = '0';
function layoutMainView() {
  renderer.domElement.style.top = getHeaderHeight() + 'px';
  const {w, h} = mainViewSize();
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  renderer.setSize(w, h);
}
layoutMainView();
document.body.appendChild(renderer.domElement);
window.addEventListener('resize', layoutMainView);

const controls = new THREE.OrbitControls(camera, renderer.domElement);
controls.target.set(0, -1, 0);

const AUX_VIEW_W = 330, AUX_VIEW_H = 270;
const topCamera = new THREE.OrthographicCamera(-2, 2, 2, -2, 0.1, 100);
topCamera.up.set(0, 0, -1);
const topRenderer = new THREE.WebGLRenderer({canvas: document.getElementById('canvas-top'), antialias: true});
topRenderer.setPixelRatio(window.devicePixelRatio);
topRenderer.setSize(AUX_VIEW_W, AUX_VIEW_H, false);

const AUX_FOV_DEG = 40;
const sideCamera = new THREE.PerspectiveCamera(AUX_FOV_DEG, AUX_VIEW_W / AUX_VIEW_H, 0.1, 100);
sideCamera.up.set(0, 1, 0);
sideCamera.layers.enable(1);
const sideRenderer = new THREE.WebGLRenderer({canvas: document.getElementById('canvas-side'), antialias: true});
sideRenderer.setPixelRatio(window.devicePixelRatio);
sideRenderer.setSize(AUX_VIEW_W, AUX_VIEW_H, false);

const frontCamera = new THREE.PerspectiveCamera(AUX_FOV_DEG, AUX_VIEW_W / AUX_VIEW_H, 0.1, 100);
frontCamera.up.set(0, 1, 0);
frontCamera.layers.enable(1);
const frontRenderer = new THREE.WebGLRenderer({canvas: document.getElementById('canvas-front'), antialias: true});
frontRenderer.setPixelRatio(window.devicePixelRatio);
frontRenderer.setSize(AUX_VIEW_W, AUX_VIEW_H, false);

function updateAuxCameras() {
  const w = roomConfig.width, l = roomConfig.length, h = roomConfig.height;
  const {cx, cy, cz} = roomCenter;

  const topHalf = Math.max(w, l) * 0.6;
  const topAspect = AUX_VIEW_W / AUX_VIEW_H;
  topCamera.left = -topHalf * topAspect;
  topCamera.right = topHalf * topAspect;
  topCamera.top = topHalf;
  topCamera.bottom = -topHalf;
  topCamera.position.set(cx, cy + 10, cz);
  topCamera.lookAt(cx, cy, cz);
  topCamera.updateProjectionMatrix();

  const halfFovRad = (AUX_FOV_DEG / 2) * Math.PI / 180;

  const sideHalf = Math.max(h, l) * 0.65;
  const sideDist = sideHalf / Math.tan(halfFovRad);
  sideCamera.position.set(cx + sideDist, cy, cz);
  sideCamera.lookAt(cx, cy, cz);
  sideCamera.updateProjectionMatrix();

  const frontHalf = Math.max(h, w) * 0.65;
  const frontDist = frontHalf / Math.tan(halfFovRad);
  frontCamera.position.set(cx, cy, cz + frontDist);
  frontCamera.lookAt(cx, cy, cz);
  frontCamera.updateProjectionMatrix();

  applyAxisLabels('top', computeAxisLabelPlacement(topCamera));
}

function computeAxisLabelPlacement(cam) {
  cam.updateMatrixWorld(true);
  const right = new THREE.Vector3().setFromMatrixColumn(cam.matrixWorld, 0).normalize();
  const up = new THREE.Vector3().setFromMatrixColumn(cam.matrixWorld, 1).normalize();
  const axes = [
    {name: 'X', dir: mapProtocolToThree(1, 0, 0).normalize()},
    {name: 'Y', dir: mapProtocolToThree(0, 1, 0).normalize()},
    {name: 'Z', dir: mapProtocolToThree(0, 0, 1).normalize()},
  ];
  let horiz = null, horizSign = 1, bestH = 0;
  let vert = null, vertSign = 1, bestV = 0;
  for (const a of axes) {
    const dh = a.dir.dot(right);
    const dv = a.dir.dot(up);
    if (Math.abs(dh) > Math.abs(bestH)) { bestH = dh; horiz = a.name; horizSign = dh >= 0 ? 1 : -1; }
    if (Math.abs(dv) > Math.abs(bestV)) { bestV = dv; vert = a.name; vertSign = dv >= 0 ? 1 : -1; }
  }
  return {horiz, horizSign, vert, vertSign};
}

const AXIS_LABEL_COLORS = {X: '#ff5555', Y: '#55ff55', Z: '#5599ff'};
function applyAxisLabels(prefix, placement) {
  for (const pos of ['top', 'bottom', 'left', 'right']) {
    document.getElementById(prefix + '-label-' + pos).textContent = '';
  }
  const hEl = document.getElementById(prefix + '-label-' + (placement.horizSign > 0 ? 'right' : 'left'));
  hEl.textContent = placement.horiz;
  hEl.style.color = AXIS_LABEL_COLORS[placement.horiz];
  const vEl = document.getElementById(prefix + '-label-' + (placement.vertSign > 0 ? 'top' : 'bottom'));
  vEl.textContent = placement.vert;
  vEl.style.color = AXIS_LABEL_COLORS[placement.vert];
}

scene.add(new THREE.AmbientLight(0xffffff, 0.7));
const dirLight = new THREE.DirectionalLight(0xffffff, 0.5);
dirLight.position.set(3, 5, 2);
scene.add(dirLight);

const radarMesh = new THREE.Mesh(
  new THREE.ConeGeometry(0.15, 0.3, 12),
  new THREE.MeshStandardMaterial({color: 0xff3333})
);
scene.add(radarMesh);
function updateRadarMeshOrientation() {
  radarMesh.rotation.x = roomConfig.mount === 0 ? 0 : Math.PI / 2;
}

// Single source of truth for "radar protocol X/Y/Z" -> "Three.js scene
// X/Y/Z" -- unchanged from the 6001B project (same axis convention per the
// manual: X left/right, Y front/back, Z height).
function mapProtocolToThree(px, py, pz) {
  if (roomConfig.mount === 0) {
    return new THREE.Vector3(px, -pz, py);
  }
  return new THREE.Vector3(py, -px, -pz);
}

function makeAxisLabel(text, color, position, scale) {
  scale = scale || 0.15;
  const canvas = document.createElement('canvas');
  canvas.width = 64;
  canvas.height = 64;
  const ctx = canvas.getContext('2d');
  ctx.font = 'bold 44px sans-serif';
  ctx.fillStyle = color;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText(text, 32, 34);
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({map: new THREE.CanvasTexture(canvas), depthTest: false}));
  sprite.scale.set(scale, scale, 1);
  sprite.position.copy(position);
  return sprite;
}

function disposeGroupChildren(group) {
  for (const child of group.children) {
    if (child.geometry)
      child.geometry.dispose();
    if (child.material) {
      if (child.material.map)
        child.material.map.dispose();
      child.material.dispose();
    }
  }
}

let protocolAxesGroup = new THREE.Group();
scene.add(protocolAxesGroup);
function rebuildProtocolAxes() {
  scene.remove(protocolAxesGroup);
  disposeGroupChildren(protocolAxesGroup);
  protocolAxesGroup = new THREE.Group();
  const axes = [
    {unit: [1, 0, 0], color: 0xff5555, label: 'X'},
    {unit: [0, 1, 0], color: 0x55ff55, label: 'Y'},
    {unit: [0, 0, 1], color: 0x5599ff, label: 'Z'},
  ];
  const length = 0.5;
  for (const a of axes) {
    const dir = mapProtocolToThree(a.unit[0], a.unit[1], a.unit[2]);
    const tip = dir.clone().multiplyScalar(length);
    const geo = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0, 0, 0), tip]);
    protocolAxesGroup.add(new THREE.Line(geo, new THREE.LineBasicMaterial({color: a.color})));
    protocolAxesGroup.add(makeAxisLabel(a.label, '#' + a.color.toString(16).padStart(6, '0'), tip.clone().multiplyScalar(1.15)));
  }
  scene.add(protocolAxesGroup);
}

const COLORS = [0x4caf50, 0x2196f3, 0xff9800, 0xe91e63, 0x9c27b0, 0x009688];
const CSS_COLORS = COLORS.map((c) => '#' + c.toString(16).padStart(6, '0'));
const targetMeshes = COLORS.map((color) => {
  const mesh = new THREE.Mesh(
    new THREE.SphereGeometry(0.15, 16, 16),
    new THREE.MeshStandardMaterial({color})
  );
  mesh.visible = false;
  scene.add(mesh);
  return mesh;
});

function animate() {
  requestAnimationFrame(animate);
  controls.update();
  renderer.render(scene, camera);
  topRenderer.render(scene, topCamera);
  frontRenderer.render(scene, frontCamera);
  sideRenderer.render(scene, sideCamera);
}
animate();

let roomConfig = {width: 4, length: 4, height: 2.5, radarHeight: 1.5, wallOffsetM: 2.0, mount: 0};
let roomBoxMesh = null;
let floorGrid = null;
let roomCenter = {cx: 0, cy: 0, cz: 0};
let graduationGroup = new THREE.Group();
scene.add(graduationGroup);

function rebuildGraduation(cx, cy, cz, w, l, h) {
  scene.remove(graduationGroup);
  disposeGroupChildren(graduationGroup);
  graduationGroup = new THREE.Group();
  const floorY = cy - h / 2;
  const tickH = 0.08;
  const mat = new THREE.LineBasicMaterial({color: 0x4488ff});
  const labelColor = '#4488ff';
  const zEdge = cz - l / 2;
  const xEdge = cx - w / 2;

  for (let x = 0; x <= w + 1e-6; x += 1) {
    const wx = cx - w / 2 + x;
    const geo = new THREE.BufferGeometry().setFromPoints([
      new THREE.Vector3(wx, floorY, zEdge), new THREE.Vector3(wx, floorY + tickH, zEdge),
    ]);
    graduationGroup.add(new THREE.Line(geo, mat));
    graduationGroup.add(makeAxisLabel(Math.round(x) + 'm', labelColor, new THREE.Vector3(wx, floorY + tickH + 0.09, zEdge)));
  }
  for (let z = 0; z <= l + 1e-6; z += 1) {
    const wz = cz - l / 2 + z;
    const geo = new THREE.BufferGeometry().setFromPoints([
      new THREE.Vector3(xEdge, floorY, wz), new THREE.Vector3(xEdge, floorY + tickH, wz),
    ]);
    graduationGroup.add(new THREE.Line(geo, mat));
    graduationGroup.add(makeAxisLabel(Math.round(z) + 'm', labelColor, new THREE.Vector3(xEdge, floorY + tickH + 0.09, wz)));
  }
  for (let y = 0; y <= h + 1e-6; y += 1) {
    const wy = floorY + y;
    const geo = new THREE.BufferGeometry().setFromPoints([
      new THREE.Vector3(xEdge, wy, zEdge), new THREE.Vector3(xEdge + tickH, wy, zEdge),
    ]);
    graduationGroup.add(new THREE.Line(geo, mat));
    graduationGroup.add(makeAxisLabel(Math.round(y) + 'm', labelColor, new THREE.Vector3(xEdge + tickH + 0.09, wy, zEdge)));
  }

  const perspMat = new THREE.LineBasicMaterial({color: 0x4488ff, transparent: true, opacity: 0.35});
  const perspFractions = [0.25, 0.5, 0.75];
  for (const f of perspFractions) {
    const wy = floorY + h * f;
    graduationGroup.add(new THREE.Line(
      new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(xEdge, wy, zEdge), new THREE.Vector3(xEdge, wy, cz + l / 2)]),
      perspMat));
    graduationGroup.add(new THREE.Line(
      new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(xEdge, wy, zEdge), new THREE.Vector3(cx + w / 2, wy, zEdge)]),
      perspMat));
  }

  const yFaceLabel = makeAxisLabel('Y', AXIS_LABEL_COLORS.Y, new THREE.Vector3(xEdge, cy, cz), 0.35);
  const xFaceLabel = makeAxisLabel('X', AXIS_LABEL_COLORS.X, new THREE.Vector3(cx, cy, zEdge), 0.35);
  xFaceLabel.layers.set(1);
  yFaceLabel.layers.set(1);
  graduationGroup.add(xFaceLabel, yFaceLabel);

  scene.add(graduationGroup);
}

function buildRoomBox() {
  updateRadarMeshOrientation();
  rebuildProtocolAxes();
  if (roomBoxMesh) {
    scene.remove(roomBoxMesh);
    roomBoxMesh.geometry.dispose();
    roomBoxMesh.material.dispose();
  }
  if (floorGrid) {
    scene.remove(floorGrid);
    floorGrid.geometry.dispose();
    floorGrid.material.dispose();
  }
  const w = roomConfig.width, l = roomConfig.length, h = roomConfig.height;
  const rh = roomConfig.radarHeight;
  const edges = new THREE.EdgesGeometry(new THREE.BoxGeometry(w, h, l));
  roomBoxMesh = new THREE.LineSegments(edges, new THREE.LineBasicMaterial({color: 0x4488ff}));
  let cx = 0, cz = 0, cy = -h / 2;
  if (roomConfig.mount === 1) { cx = w / 2 - roomConfig.wallOffsetM; cz = -l / 2; cy = h / 2 - rh; }
  roomBoxMesh.position.set(cx, cy, cz);
  scene.add(roomBoxMesh);
  roomCenter = {cx, cy, cz};

  floorGrid = new THREE.GridHelper(Math.max(w, l), 16, 0x666666, 0x333333);
  floorGrid.position.set(cx, cy - h / 2, cz);
  scene.add(floorGrid);

  rebuildGraduation(cx, cy, cz, w, l, h);

  controls.target.set(cx, cy - h * 0.25, cz);
  controls.update();
  updateAuxCameras();
}

function updateRadarHeightVisibility() {
  const isWallMount = roomConfig.mount === 1;
  document.getElementById('room-radar-height-label').hidden = !isWallMount;
  document.getElementById('room-radar-height').hidden = !isWallMount;
  document.getElementById('room-wall-offset-label').hidden = !isWallMount;
  document.getElementById('room-wall-offset').hidden = !isWallMount;
}

async function loadRoomConfig() {
  try {
    const res = await fetch('/room_config');
    roomConfig = await res.json();
  } catch (e) {
    return;
  }
  document.getElementById('room-width').value = roomConfig.width;
  document.getElementById('room-length').value = roomConfig.length;
  document.getElementById('room-height').value = roomConfig.height;
  document.getElementById('room-radar-height').value = roomConfig.radarHeight;
  document.getElementById('room-wall-offset').value = roomConfig.wallOffsetM;
  document.getElementById('room-mount').value = roomConfig.mount;
  updateRadarHeightVisibility();
  buildRoomBox();
}

async function saveRoomConfig() {
  const width = parseFloat(document.getElementById('room-width').value);
  const length = parseFloat(document.getElementById('room-length').value);
  const height = parseFloat(document.getElementById('room-height').value);
  const radarHeight = parseFloat(document.getElementById('room-radar-height').value);
  const wallOffsetM = parseFloat(document.getElementById('room-wall-offset').value);
  const mount = parseInt(document.getElementById('room-mount').value, 10);
  const err = document.getElementById('room-error');
  if (!(width > 0 && width <= 4.5) || !(length > 0 && length <= 4.5) || !(height > 0 && height <= 2.5) ||
      !(radarHeight > 0 && radarHeight <= 2.5) || !(wallOffsetM >= 0 && wallOffsetM <= 4.5)) {
    err.textContent = 'Limites : largeur/longueur <= 4.5m, hauteur/hauteur radar <= 2.5m, distance au coin <= 4.5m';
    return;
  }
  err.textContent = '';
  const body = 'width=' + width + '&length=' + length + '&height=' + height + '&radar_height=' + radarHeight +
               '&wall_offset_m=' + wallOffsetM + '&mount=' + mount;
  try {
    const res = await fetch('/room_config', {method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body});
    roomConfig = await res.json();
    updateRadarHeightVisibility();
    buildRoomBox();
  } catch (e) {
    err.textContent = 'Echec de l\'enregistrement (connexion perdue)';
  }
}
document.getElementById('room-save').addEventListener('click', saveRoomConfig);
document.getElementById('room-mount').addEventListener('change', (e) => {
  roomConfig.mount = parseInt(e.target.value, 10);
  updateRadarHeightVisibility();
  buildRoomBox();
});
loadRoomConfig();

document.getElementById('tab-btn-room').addEventListener('click', () => setTab('room'));
document.getElementById('tab-btn-radar').addEventListener('click', () => setTab('radar'));
document.getElementById('tab-btn-console').addEventListener('click', () => setTab('console'));
function setTab(name) {
  for (const t of ['room', 'radar', 'console']) {
    document.getElementById('tab-' + t).classList.toggle('active', t === name);
    document.getElementById('tab-btn-' + t).classList.toggle('active', t === name);
  }
}

let atCommandInFlight = false;
function setAtButtonsDisabled(disabled) {
  document.getElementById('console-send').disabled = disabled;
  document.getElementById('radar-start').disabled = disabled;
  document.getElementById('radar-stop').disabled = disabled;
}
async function sendAtCommand(cmd) {
  if (atCommandInFlight)
    return {ok: false, timedOut: false, raw: 'une autre commande est deja en cours sur cette page'};
  atCommandInFlight = true;
  setAtButtonsDisabled(true);
  try {
    let postRes;
    try {
      postRes = await fetch('/at_command', {method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                                   body: 'cmd=' + encodeURIComponent(cmd)});
    } catch (e) {
      return {ok: false, timedOut: false, raw: '', networkError: true};
    }
    if (postRes.status === 409)
      return {ok: false, timedOut: false, raw: 'une autre session a deja une commande en cours'};
    const pollDeadlineMs = Date.now() + 5500;
    while (Date.now() < pollDeadlineMs) {
      await new Promise((resolve) => setTimeout(resolve, 150));
      try {
        const res = await fetch('/at_command_result');
        const data = await res.json();
        if (data.done)
          return {ok: data.ok, timedOut: data.timedOut, raw: data.raw};
      } catch (e) {
        return {ok: false, timedOut: false, raw: '', networkError: true};
      }
    }
    return {ok: false, timedOut: true, raw: ''};
  } finally {
    atCommandInFlight = false;
    setAtButtonsDisabled(false);
  }
}

async function sendConsoleCommand() {
  const input = document.getElementById('console-cmd');
  const result = document.getElementById('console-result');
  const log = document.getElementById('console-log');
  const raw = input.value.trim();
  if (!raw)
    return;
  const cmd = raw.toUpperCase().startsWith('AT+') ? raw : 'AT+' + raw;
  result.className = '';
  result.textContent = cmd + '...';
  let entry, ok;
  try {
    const data = await sendAtCommand(cmd);
    if (data.networkError)
      throw new Error('network');
    if (data.timedOut) {
      ok = false;
      entry = cmd + ' : pas de reponse (timeout)';
    } else {
      ok = data.ok;
      entry = cmd + ' : ' + (data.ok ? 'AT+OK' : ('erreur (' + data.raw + ')'));
    }
  } catch (e) {
    ok = false;
    entry = cmd + ' : echec (connexion perdue)';
  }
  result.textContent = (ok ? '✓ ' : '✗ ') + entry;
  result.className = ok ? 'console-result-ok' : 'console-result-fail';
  const line = document.createElement('div');
  line.textContent = new Date().toLocaleTimeString() + ' -- ' + (ok ? '✓ ' : '✗ ') + entry;
  line.className = ok ? 'console-result-ok' : 'console-result-fail';
  log.insertBefore(line, log.firstChild);
  const MAX_CONSOLE_LOG_LINES = 20;
  while (log.children.length > MAX_CONSOLE_LOG_LINES)
    log.removeChild(log.lastChild);
}
document.getElementById('console-send').addEventListener('click', sendConsoleCommand);
document.getElementById('console-cmd').addEventListener('keydown', (e) => {
  if (e.key === 'Enter')
    sendConsoleCommand();
});

// Radar settings -- AT+DPKTH/AT+RANGE/AT+HEIGHTD + single rectangular
// zone (AT+XNega/AT+XPosi/AT+YNega/AT+YPosi), see PROTOCOL.md. No
// scan/monitor/heartbeat intervals here (unlike the 6001B): none of those
// are confirmed AT+ commands for this module.
let radarSettings = {sensitivity: 4, rangeCm: 450, heightDCm: 300,
                      zoneEnabled: false, zoneXNeg: -450, zoneXPos: 450, zoneYNeg: -450, zoneYPos: 450,
                      live: {seen: false, raw: '', ageMs: 0}};
let zoneMesh = null;

// "Etat radar" here shows the raw AT+READ response text verbatim -- no
// field-by-field comparison like the 6001B's heartbeat-based check, since
// this module's AT+READ response format is undocumented/reported to vary
// by firmware version (see PROTOCOL.md, MAINTENANCE.md). A response older
// than 2x the poll interval is flagged stale (radar likely not responding
// to AT+READ any more), not compared against anything.
function updateLiveStatus() {
  const el = document.getElementById('radar-live');
  const l = radarSettings.live;
  if (!l.seen) {
    el.textContent = 'Etat radar : en attente de la premiere reponse AT+READ...';
    el.classList.add('stale');
    return;
  }
  const stale = l.ageMs > 30000;
  el.classList.toggle('stale', stale);
  el.textContent = 'Derniere reponse AT+READ (il y a ' + Math.round(l.ageMs / 1000) + 's' +
    (stale ? ', PEUT-ETRE PERIMEE' : '') + ') : ' + l.raw;
}

function rebuildZoneMesh() {
  if (zoneMesh) {
    scene.remove(zoneMesh);
    zoneMesh.geometry.dispose();
    zoneMesh.material.dispose();
    zoneMesh = null;
  }
  const p1 = mapProtocolToThree(radarSettings.zoneXNeg / 100, radarSettings.zoneYNeg / 100, 0);
  const p2 = mapProtocolToThree(radarSettings.zoneXPos / 100, radarSettings.zoneYPos / 100, 0);
  const size = new THREE.Vector3(Math.abs(p2.x - p1.x), Math.abs(p2.y - p1.y), Math.abs(p2.z - p1.z));
  const center = p1.clone().add(p2).multiplyScalar(0.5);
  const extrudeAxis = size.x < 1e-4 ? 'x' : size.y < 1e-4 ? 'y' : 'z';
  const extrudeSize = {x: roomConfig.width, y: roomConfig.height, z: roomConfig.length}[extrudeAxis];
  const extrudeCenter = {x: roomCenter.cx, y: roomCenter.cy, z: roomCenter.cz}[extrudeAxis];
  size[extrudeAxis] = extrudeSize;
  center[extrudeAxis] = extrudeCenter;
  size.x = Math.max(size.x, 0.1);
  size.y = Math.max(size.y, 0.1);
  size.z = Math.max(size.z, 0.1);
  const geo = new THREE.BoxGeometry(size.x, size.y, size.z);
  const color = radarSettings.zoneEnabled ? 0xffb74d : 0x4488ff;
  const opacity = radarSettings.zoneEnabled ? 0.18 : 0.10;
  zoneMesh = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({color, transparent: true, opacity}));
  zoneMesh.position.copy(center);
  scene.add(zoneMesh);
}

async function loadRadarSettings() {
  try {
    const res = await fetch('/radar_settings');
    radarSettings = await res.json();
  } catch (e) {
    return;
  }
  document.getElementById('radar-dpkth').value = radarSettings.sensitivity;
  document.getElementById('radar-dpkth-val').textContent = radarSettings.sensitivity;
  document.getElementById('radar-range').value = radarSettings.rangeCm / 100;
  document.getElementById('radar-range-val').textContent = (radarSettings.rangeCm / 100).toFixed(1) + 'm';
  document.getElementById('radar-heightd').value = radarSettings.heightDCm / 100;
  document.getElementById('radar-heightd-val').textContent = (radarSettings.heightDCm / 100).toFixed(1) + 'm';
  document.getElementById('zone-en').checked = radarSettings.zoneEnabled;
  document.getElementById('zone-x-neg').value = (radarSettings.zoneXNeg / 100).toFixed(1);
  document.getElementById('zone-x-pos').value = (radarSettings.zoneXPos / 100).toFixed(1);
  document.getElementById('zone-y-neg').value = (radarSettings.zoneYNeg / 100).toFixed(1);
  document.getElementById('zone-y-pos').value = (radarSettings.zoneYPos / 100).toFixed(1);
  updateLiveStatus();
  rebuildZoneMesh();
}

async function saveRadarSettings() {
  const err = document.getElementById('radar-error');
  const sensitivity = parseInt(document.getElementById('radar-dpkth').value, 10);
  const rangeCm = Math.round(parseFloat(document.getElementById('radar-range').value) * 100);
  const heightDCm = Math.round(parseFloat(document.getElementById('radar-heightd').value) * 100);
  const zoneEnabled = document.getElementById('zone-en').checked;
  const zoneXNeg = Math.round(parseFloat(document.getElementById('zone-x-neg').value) * 100);
  const zoneXPos = Math.round(parseFloat(document.getElementById('zone-x-pos').value) * 100);
  const zoneYNeg = Math.round(parseFloat(document.getElementById('zone-y-neg').value) * 100);
  const zoneYPos = Math.round(parseFloat(document.getElementById('zone-y-pos').value) * 100);
  if (!(sensitivity >= 1 && sensitivity <= 9) || !(rangeCm >= 10 && rangeCm <= 500) ||
      !(heightDCm >= 50 && heightDCm <= 500)) {
    err.textContent = 'Une ou plusieurs valeurs sont hors limites (DPKTH 1-9, portee 0.1-5.0m, hauteur 0.5-5.0m).';
    return;
  }
  if (zoneEnabled && (!(zoneXNeg >= -500 && zoneXNeg <= -20) || !(zoneXPos >= 20 && zoneXPos <= 500) ||
      !(zoneYNeg >= -500 && zoneYNeg <= -20) || !(zoneYPos >= 20 && zoneYPos <= 500))) {
    err.textContent = 'Zone hors limites (X-/Y- entre -5.0 et -0.2m, X+/Y+ entre 0.2 et 5.0m).';
    return;
  }
  const body = 'sensitivity=' + sensitivity + '&range_cm=' + rangeCm + '&height_d_cm=' + heightDCm +
               '&zone_en=' + (zoneEnabled ? 1 : 0) + '&zone_x_neg=' + zoneXNeg + '&zone_x_pos=' + zoneXPos +
               '&zone_y_neg=' + zoneYNeg + '&zone_y_pos=' + zoneYPos;
  err.textContent = '';
  try {
    const res = await fetch('/radar_settings', {method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body});
    radarSettings = await res.json();
    updateLiveStatus();
    rebuildZoneMesh();
  } catch (e) {
    err.textContent = 'Echec de l\'enregistrement (connexion perdue)';
  }
}
document.getElementById('radar-save').addEventListener('click', saveRadarSettings);
async function sendRadarControl(cmd) {
  const status = document.getElementById('radar-control-status');
  status.className = 'slider-value';
  status.textContent = cmd + '...';
  let ok = false, text;
  try {
    const data = await sendAtCommand('AT+' + cmd);
    if (data.networkError)
      throw new Error('network');
    if (data.timedOut) {
      text = cmd + ' : pas de reponse (timeout)';
    } else {
      ok = data.ok;
      text = cmd + ' : ' + (data.ok ? 'AT+OK' : ('erreur (' + data.raw + ')'));
    }
  } catch (e) {
    text = 'Echec (connexion perdue)';
  }
  status.textContent = (ok ? '✓ ' : '✗ ') + text;
  status.className = 'slider-value ' + (ok ? 'console-result-ok' : 'console-result-fail');
}
document.getElementById('radar-start').addEventListener('click', () => sendRadarControl('START'));
document.getElementById('radar-stop').addEventListener('click', () => sendRadarControl('STOP'));
document.getElementById('radar-dpkth').addEventListener('input', (e) => {
  document.getElementById('radar-dpkth-val').textContent = e.target.value;
});
document.getElementById('radar-range').addEventListener('input', (e) => {
  document.getElementById('radar-range-val').textContent = parseFloat(e.target.value).toFixed(1) + 'm';
});
document.getElementById('radar-heightd').addEventListener('input', (e) => {
  document.getElementById('radar-heightd-val').textContent = parseFloat(e.target.value).toFixed(1) + 'm';
});
loadRadarSettings();
setInterval(async () => {
  try {
    const res = await fetch('/radar_settings');
    const data = await res.json();
    radarSettings.live = data.live;
    updateLiveStatus();
  } catch (e) { /* ignore, next tick retries */ }
}, 5000);

async function refresh() {
  try {
    const res = await fetch('/hlk_targets.json');
    const data = await res.json();
    document.getElementById('count-value').textContent = data.count;
    document.getElementById('status-dot').classList.add('ok');
    document.getElementById('status-text').textContent = 'En ligne';
    // Single target stream (id+x+y+z+vx+vy+vz per entry) -- unlike the
    // 6001B, no second stream to match by position: this module only
    // ever has one source of truth (AT+DEBUG=3).
    for (let i = 0; i < targetMeshes.length; i++) {
      const cell = document.getElementById('target-cell-' + i);
      const cellTitle = cell.querySelector('.target-cell-title');
      const cellPos = cell.querySelector('.target-cell-pos');
      const cellVel = cell.querySelector('.target-cell-vel');
      if (i < data.targets.length) {
        const t = data.targets[i];
        targetMeshes[i].position.copy(mapProtocolToThree(t.x, t.y, t.z));
        targetMeshes[i].visible = true;
        cell.classList.add('active');
        cell.style.backgroundColor = CSS_COLORS[i];
        cellTitle.textContent = 'Cible ' + (i + 1) + ' (ID ' + t.id + ')';
        cellPos.textContent = 'x=' + t.x.toFixed(1) + ' y=' + t.y.toFixed(1) + ' z=' + t.z.toFixed(1) + ' m';
        cellVel.textContent = 'v=(' + t.vx.toFixed(2) + ',' + t.vy.toFixed(2) + ',' + t.vz.toFixed(2) + ') m/s';
      } else {
        targetMeshes[i].visible = false;
        cell.classList.remove('active');
        cell.style.backgroundColor = '';
        cellTitle.textContent = '';
        cellPos.textContent = '';
        cellVel.textContent = '';
      }
    }
  } catch (e) {
    document.getElementById('status-dot').classList.remove('ok');
    document.getElementById('status-text').textContent = 'Deconnecte';
  }
}
// Same 1000ms interval as the 6001B project, for the same reason
// (CONFIG_LWIP_MAX_SOCKETS headroom, see hlk-ld6001a-xiao.yaml) -- this
// module's claimed <=30ms internal processing cycle doesn't change how
// often the WEB PAGE itself should poll, only how fast the ESP32 could
// theoretically publish, which is separately throttled server-side (see
// PUBLISH_THROTTLE_MS in hlk_ld6001a.h) for the HA-facing sensors.
setInterval(refresh, 1000);
refresh();
} // if (window.__hlkThreeOk)
} catch (e) {
  hlkShowBanner('Erreur inattendue dans la vue 3D : ' + e.message + ' -- rechargez la page.');
}
</script>
</body>
</html>
)HTML";

#ifdef USE_SENSOR
void HlkLd6001aComponent::set_target_x_sensor(uint8_t index, sensor::Sensor *s) {
  if (index < MAX_TARGETS)
    this->target_x_sensors_[index] = s;
}
void HlkLd6001aComponent::set_target_y_sensor(uint8_t index, sensor::Sensor *s) {
  if (index < MAX_TARGETS)
    this->target_y_sensors_[index] = s;
}
void HlkLd6001aComponent::set_target_z_sensor(uint8_t index, sensor::Sensor *s) {
  if (index < MAX_TARGETS)
    this->target_z_sensors_[index] = s;
}
void HlkLd6001aComponent::set_target_vx_sensor(uint8_t index, sensor::Sensor *s) {
  if (index < MAX_TARGETS)
    this->target_vx_sensors_[index] = s;
}
void HlkLd6001aComponent::set_target_vy_sensor(uint8_t index, sensor::Sensor *s) {
  if (index < MAX_TARGETS)
    this->target_vy_sensors_[index] = s;
}
void HlkLd6001aComponent::set_target_vz_sensor(uint8_t index, sensor::Sensor *s) {
  if (index < MAX_TARGETS)
    this->target_vz_sensors_[index] = s;
}
void HlkLd6001aComponent::set_target_id_sensor(uint8_t index, sensor::Sensor *s) {
  if (index < MAX_TARGETS)
    this->target_id_sensors_[index] = s;
}
#endif

void HlkLd6001aComponent::setup() {
  uint32_t now = millis();
  this->last_frame_millis_ = now;
  this->last_recovery_millis_ = now;
  this->last_at_read_poll_millis_ = now;

  this->room_config_pref_ = global_preferences->make_preference<RoomConfig>(fnv1_hash("hlk_ld6001a_room_config"));
  this->room_config_pref_.load(&this->room_config_);

  this->radar_settings_pref_ =
      global_preferences->make_preference<RadarSettings>(fnv1_hash("hlk_ld6001a_radar_settings"));
  this->radar_settings_pref_.load(&this->radar_settings_);

  // Applying saved settings (and AT+DEBUG=3/AT+START) is deferred to
  // loop(), gated on the YAML's own on_boot AT+RESET having actually
  // fired -- see that gate in loop(), same reasoning as the 6001B
  // project (brownout fix: wait for confirmed WiFi before touching the
  // radar at all).
}

void HlkLd6001aComponent::start_http_server_() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;
  if (httpd_start(&this->http_server_, &config) != ESP_OK) {
    ESP_LOGE(TAG, "failed to start 3D viewer HTTP server");
    return;
  }

  httpd_uri_t root_uri = {};
  root_uri.uri = "/";
  root_uri.method = HTTP_GET;
  root_uri.handler = HlkLd6001aComponent::http_handle_root_;
  root_uri.user_ctx = this;
  httpd_register_uri_handler(this->http_server_, &root_uri);

  httpd_uri_t json_uri = {};
  json_uri.uri = "/hlk_targets.json";
  json_uri.method = HTTP_GET;
  json_uri.handler = HlkLd6001aComponent::http_handle_targets_json_;
  json_uri.user_ctx = this;
  httpd_register_uri_handler(this->http_server_, &json_uri);

  httpd_uri_t room_get_uri = {};
  room_get_uri.uri = "/room_config";
  room_get_uri.method = HTTP_GET;
  room_get_uri.handler = HlkLd6001aComponent::http_handle_room_get_;
  room_get_uri.user_ctx = this;
  httpd_register_uri_handler(this->http_server_, &room_get_uri);

  httpd_uri_t room_post_uri = {};
  room_post_uri.uri = "/room_config";
  room_post_uri.method = HTTP_POST;
  room_post_uri.handler = HlkLd6001aComponent::http_handle_room_post_;
  room_post_uri.user_ctx = this;
  httpd_register_uri_handler(this->http_server_, &room_post_uri);

  httpd_uri_t settings_get_uri = {};
  settings_get_uri.uri = "/radar_settings";
  settings_get_uri.method = HTTP_GET;
  settings_get_uri.handler = HlkLd6001aComponent::http_handle_settings_get_;
  settings_get_uri.user_ctx = this;
  httpd_register_uri_handler(this->http_server_, &settings_get_uri);

  httpd_uri_t settings_post_uri = {};
  settings_post_uri.uri = "/radar_settings";
  settings_post_uri.method = HTTP_POST;
  settings_post_uri.handler = HlkLd6001aComponent::http_handle_settings_post_;
  settings_post_uri.user_ctx = this;
  httpd_register_uri_handler(this->http_server_, &settings_post_uri);

  httpd_uri_t at_command_uri = {};
  at_command_uri.uri = "/at_command";
  at_command_uri.method = HTTP_POST;
  at_command_uri.handler = HlkLd6001aComponent::http_handle_at_command_post_;
  at_command_uri.user_ctx = this;
  httpd_register_uri_handler(this->http_server_, &at_command_uri);

  httpd_uri_t at_command_result_uri = {};
  at_command_result_uri.uri = "/at_command_result";
  at_command_result_uri.method = HTTP_GET;
  at_command_result_uri.handler = HlkLd6001aComponent::http_handle_at_command_result_get_;
  at_command_result_uri.user_ctx = this;
  httpd_register_uri_handler(this->http_server_, &at_command_result_uri);

  ESP_LOGI(TAG, "3D viewer: http://<device-ip>/");
}

// httpd_query_key_value() does not decode %XX escapes -- see the 6001B
// project's identical function for the 2026-09-06 finding that made this
// necessary (every AT+ command sent through this endpoint before that fix
// was percent-encoded garbage the radar correctly rejected).
static void drain_remaining_body_(httpd_req_t *req, size_t already_read) {
  size_t remaining = static_cast<size_t>(req->content_len) > already_read
                          ? static_cast<size_t>(req->content_len) - already_read
                          : 0;
  char scratch[64];
  while (remaining > 0) {
    size_t chunk = std::min(remaining, sizeof(scratch));
    int got = httpd_req_recv(req, scratch, chunk);
    if (got <= 0)
      break;
    remaining -= static_cast<size_t>(got);
  }
}

static std::string json_escape_(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '"' || c == '\\')
      out += '\\';
    out += c;
  }
  return out;
}

static void url_decode_(char *s) {
  char *out = s;
  for (char *in = s; *in != '\0'; in++, out++) {
    if (*in == '%' && in[1] != '\0' && in[2] != '\0') {
      char hex[3] = {in[1], in[2], '\0'};
      *out = static_cast<char>(strtol(hex, nullptr, 16));
      in += 2;
    } else {
      *out = *in;
    }
  }
  *out = '\0';
}

// AT+ command queue -- verbatim port of the HLK-LD6001B project's queue
// (itself ported from Devristo's open-source esphome-hlk-ld6001a
// CommandQueue). See that project's hlk_ld6001b.cpp for the full history
// of why this exists (real ack detection, cross-task handoff).
void HlkLd6001aComponent::enqueue_at_command_(
    const std::string &cmd_with_newline, std::function<void(bool ok, const std::string &raw_response)> on_result) {
  if (this->at_command_queue_.size() >= MAX_AT_COMMAND_QUEUE_SIZE) {
    ESP_LOGW(TAG, "AT command queue full (%u), refusing: %s",
             static_cast<unsigned>(MAX_AT_COMMAND_QUEUE_SIZE), cmd_with_newline.c_str());
    if (on_result)
      on_result(false, "");
    return;
  }
  this->at_command_queue_.push_back({cmd_with_newline, std::move(on_result)});
}

void HlkLd6001aComponent::service_at_command_queue_() {
  uint32_t now = millis();
  if (this->at_command_in_flight_ && (now - this->at_command_sent_millis_) > AT_COMMAND_ACK_TIMEOUT_MS) {
    ESP_LOGW(TAG, "AT command timed out waiting for AT+OK/AT+ERR: %s",
             this->at_command_queue_.front().data.c_str());
    auto cb = this->at_command_queue_.front().on_result;
    this->at_command_queue_.pop_front();
    this->at_command_in_flight_ = false;
    if (cb)
      cb(false, "");
  }
  if (!this->at_command_in_flight_ && !this->at_command_queue_.empty()) {
    const auto &cmd = this->at_command_queue_.front();
    ESP_LOGI(TAG, "sending queued AT command: %s", cmd.data.c_str());
    send_command(this, cmd.data.c_str());
    this->at_command_in_flight_ = true;
    this->at_command_sent_millis_ = now;
  }
}

// Switches to AT+DEBUG=3 (not 0/2 like the 6001B) and starts the radar
// once apply_radar_settings_()'s queue has drained -- see PROTOCOL.md,
// "Séquence de démarrage".
void HlkLd6001aComponent::start_after_settings_queue_drains_(uint32_t waited_ms) {
  if (this->at_command_queue_.empty() && !this->at_command_in_flight_) {
    send_command(this, "AT+DEBUG=3\n");
    this->set_timeout("hlk_ld6001a_start_after_debug", 500, [this]() { send_command(this, "AT+START\n"); });
    return;
  }
  if (waited_ms >= 20000) {
    ESP_LOGW(TAG, "settings queue still not drained after 20s, switching to AT+DEBUG=3/AT+START anyway");
    send_command(this, "AT+DEBUG=3\n");
    this->set_timeout("hlk_ld6001a_start_after_debug", 500, [this]() { send_command(this, "AT+START\n"); });
    return;
  }
  this->set_timeout("hlk_ld6001a_wait_settings_then_start", 300,
                     [this, waited_ms]() { this->start_after_settings_queue_drains_(waited_ms + 300); });
}

void HlkLd6001aComponent::handle_at_response_line_(const std::string &line) {
  ESP_LOGI(TAG, "AT response received: %s", line.c_str());
  if (!this->at_command_in_flight_ || this->at_command_queue_.empty())
    return;
  bool ok = line.find(AT_OK_TOKEN) != std::string::npos;
  auto cb = this->at_command_queue_.front().on_result;
  this->at_command_queue_.pop_front();
  this->at_command_in_flight_ = false;
  if (cb)
    cb(ok, line);
  this->service_at_command_queue_();
}

esp_err_t HlkLd6001aComponent::http_handle_at_command_post_(httpd_req_t *req) {
  auto *self = static_cast<HlkLd6001aComponent *>(req->user_ctx);
  if (self->http_at_command_busy_) {
    drain_remaining_body_(req, 0);
    httpd_resp_set_status(req, "409 Conflict");
    httpd_resp_set_type(req, "application/json");
    static const char busy_json[] = "{\"error\":\"busy\"}";
    httpd_resp_send(req, busy_json, sizeof(busy_json) - 1);
    return ESP_OK;
  }

  char body[64] = {};
  int to_read = std::min<int>(req->content_len, sizeof(body) - 1);
  int read_len = httpd_req_recv(req, body, to_read);
  if (read_len <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  body[read_len] = '\0';
  drain_remaining_body_(req, static_cast<size_t>(read_len));
  char cmd_val[48] = {};
  if (httpd_query_key_value(body, "cmd", cmd_val, sizeof(cmd_val)) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  url_decode_(cmd_val);
  ESP_LOGW(TAG, "manual AT command sent via /at_command: %s", cmd_val);
  std::string cmd = cmd_val;
  cmd += "\n";

  self->http_at_command_busy_ = true;
  self->http_at_command_done_ = false;
  self->http_at_command_ok_ = false;
  self->http_at_command_response_.clear();
  self->enqueue_at_command_(cmd, [self](bool ok, const std::string &raw) {
    self->http_at_command_ok_ = ok;
    self->http_at_command_response_ = raw;
    self->http_at_command_response_.erase(
        std::remove_if(self->http_at_command_response_.begin(), self->http_at_command_response_.end(),
                        [](char c) { return c == '\r' || c == '\n'; }),
        self->http_at_command_response_.end());
    self->http_at_command_done_ = true;
    self->http_at_command_busy_ = false;
  });

  httpd_resp_set_type(req, "application/json");
  static const char queued_json[] = "{\"queued\":true}";
  httpd_resp_send(req, queued_json, sizeof(queued_json) - 1);
  return ESP_OK;
}

esp_err_t HlkLd6001aComponent::http_handle_at_command_result_get_(httpd_req_t *req) {
  auto *self = static_cast<HlkLd6001aComponent *>(req->user_ctx);
  bool done = self->http_at_command_done_;
  bool got_real_response = !self->http_at_command_response_.empty();
  std::string resp_json = std::string("{\"done\":") + (done ? "true" : "false") + ",\"ok\":" +
                           (self->http_at_command_ok_ ? "true" : "false") + ",\"timedOut\":" +
                           (done && !got_real_response ? "true" : "false") + ",\"raw\":\"" +
                           json_escape_(self->http_at_command_response_) + "\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp_json.c_str(), resp_json.size());
  return ESP_OK;
}

// Sends every saved radar setting through the ack/timeout-gated AT+
// command queue, one at a time, BEFORE AT+DEBUG=3/AT+START (see
// start_after_settings_queue_drains_() and PROTOCOL.md). Zone commands
// (AT+XNega/AT+XPosi/AT+YNega/AT+YPosi, no "D" suffix -- see PROTOCOL.md's
// note on this decision) are only sent if the zone is enabled; if
// disabled, they're simply never sent (no documented "disable" sentinel
// for this format, unlike the 6001B's AT+WINxRANGE=999999999999).
void HlkLd6001aComponent::apply_radar_settings_() {
  ESP_LOGI(TAG, "applying saved radar settings (dpkth=%u range=%ucm heightd=%ucm zone_enabled=%s)",
           this->radar_settings_.sensitivity, this->radar_settings_.range_cm, this->radar_settings_.height_d_cm,
           this->radar_settings_.zone_enabled ? "true" : "false");
  auto log_result = [](bool ok, const std::string &raw) {
    ESP_LOGI(TAG, "apply_radar_settings_ command result: ok=%s raw=\"%s\"", ok ? "true" : "false", raw.c_str());
  };
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+DPKTH=%u\n", this->radar_settings_.sensitivity);
  this->enqueue_at_command_(cmd, log_result);
  snprintf(cmd, sizeof(cmd), "AT+RANGE=%u\n", this->radar_settings_.range_cm);
  this->enqueue_at_command_(cmd, log_result);
  snprintf(cmd, sizeof(cmd), "AT+HEIGHTD=%u\n", this->radar_settings_.height_d_cm);
  this->enqueue_at_command_(cmd, log_result);

  if (this->radar_settings_.zone_enabled) {
    snprintf(cmd, sizeof(cmd), "AT+XNega=%d\n", this->radar_settings_.zone_x_neg);
    this->enqueue_at_command_(cmd, log_result);
    snprintf(cmd, sizeof(cmd), "AT+XPosi=%d\n", this->radar_settings_.zone_x_pos);
    this->enqueue_at_command_(cmd, log_result);
    snprintf(cmd, sizeof(cmd), "AT+YNega=%d\n", this->radar_settings_.zone_y_neg);
    this->enqueue_at_command_(cmd, log_result);
    snprintf(cmd, sizeof(cmd), "AT+YPosi=%d\n", this->radar_settings_.zone_y_pos);
    this->enqueue_at_command_(cmd, log_result);
  }
  this->radar_settings_applied_ = true;
}

std::string HlkLd6001aComponent::radar_settings_json_() const {
  char buf[384];
  uint32_t age_ms = this->live_read_seen_ ? millis() - this->live_read_millis_ : 0;
  snprintf(buf, sizeof(buf),
           "{\"sensitivity\":%u,\"rangeCm\":%u,\"heightDCm\":%u,"
           "\"zoneEnabled\":%s,\"zoneXNeg\":%d,\"zoneXPos\":%d,\"zoneYNeg\":%d,\"zoneYPos\":%d,"
           "\"live\":{\"seen\":%s,\"raw\":\"%s\",\"ageMs\":%u}}",
           this->radar_settings_.sensitivity, this->radar_settings_.range_cm, this->radar_settings_.height_d_cm,
           this->radar_settings_.zone_enabled ? "true" : "false", this->radar_settings_.zone_x_neg,
           this->radar_settings_.zone_x_pos, this->radar_settings_.zone_y_neg, this->radar_settings_.zone_y_pos,
           this->live_read_seen_ ? "true" : "false", json_escape_(this->live_read_raw_).c_str(),
           static_cast<unsigned>(age_ms));
  return std::string(buf);
}

esp_err_t HlkLd6001aComponent::http_handle_settings_get_(httpd_req_t *req) {
  auto *self = static_cast<HlkLd6001aComponent *>(req->user_ctx);
  httpd_resp_set_type(req, "application/json");
  std::string json = self->radar_settings_json_();
  httpd_resp_send(req, json.c_str(), json.size());
  return ESP_OK;
}

esp_err_t HlkLd6001aComponent::http_handle_settings_post_(httpd_req_t *req) {
  auto *self = static_cast<HlkLd6001aComponent *>(req->user_ctx);

  char body[192] = {};
  int to_read = std::min<int>(req->content_len, sizeof(body) - 1);
  int read_len = httpd_req_recv(req, body, to_read);
  if (read_len <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  body[read_len] = '\0';
  drain_remaining_body_(req, static_cast<size_t>(read_len));

  // Server-side validation against the manual's own documented ranges --
  // never trust the browser alone. Bad/missing fields keep their previous
  // saved value rather than reject the whole request.
  RadarSettings cfg = self->radar_settings_;
  char val[16];
  if (httpd_query_key_value(body, "sensitivity", val, sizeof(val)) == ESP_OK) {
    int v = atoi(val);
    if (v >= RADAR_DPKTH_MIN && v <= RADAR_DPKTH_MAX)
      cfg.sensitivity = static_cast<uint8_t>(v);
  }
  if (httpd_query_key_value(body, "range_cm", val, sizeof(val)) == ESP_OK) {
    int v = atoi(val);
    if (v >= RADAR_RANGE_CM_MIN && v <= RADAR_RANGE_CM_MAX)
      cfg.range_cm = static_cast<uint16_t>(v);
  }
  if (httpd_query_key_value(body, "height_d_cm", val, sizeof(val)) == ESP_OK) {
    int v = atoi(val);
    if (v >= RADAR_HEIGHTD_CM_MIN && v <= RADAR_HEIGHTD_CM_MAX)
      cfg.height_d_cm = static_cast<uint16_t>(v);
  }
  if (httpd_query_key_value(body, "zone_en", val, sizeof(val)) == ESP_OK)
    cfg.zone_enabled = atoi(val) != 0;
  if (httpd_query_key_value(body, "zone_x_neg", val, sizeof(val)) == ESP_OK) {
    int v = atoi(val);
    if (v >= RADAR_ZONE_NEG_MIN_CM && v <= RADAR_ZONE_NEG_MAX_CM)
      cfg.zone_x_neg = static_cast<int16_t>(v);
  }
  if (httpd_query_key_value(body, "zone_x_pos", val, sizeof(val)) == ESP_OK) {
    int v = atoi(val);
    if (v >= RADAR_ZONE_POS_MIN_CM && v <= RADAR_ZONE_POS_MAX_CM)
      cfg.zone_x_pos = static_cast<int16_t>(v);
  }
  if (httpd_query_key_value(body, "zone_y_neg", val, sizeof(val)) == ESP_OK) {
    int v = atoi(val);
    if (v >= RADAR_ZONE_NEG_MIN_CM && v <= RADAR_ZONE_NEG_MAX_CM)
      cfg.zone_y_neg = static_cast<int16_t>(v);
  }
  if (httpd_query_key_value(body, "zone_y_pos", val, sizeof(val)) == ESP_OK) {
    int v = atoi(val);
    if (v >= RADAR_ZONE_POS_MIN_CM && v <= RADAR_ZONE_POS_MAX_CM)
      cfg.zone_y_pos = static_cast<int16_t>(v);
  }

  self->radar_settings_ = cfg;
  self->radar_settings_pref_.save(&self->radar_settings_);
  self->pending_apply_radar_settings_ = true;

  httpd_resp_set_type(req, "application/json");
  std::string json = self->radar_settings_json_();
  httpd_resp_send(req, json.c_str(), json.size());
  return ESP_OK;
}

std::string HlkLd6001aComponent::room_config_json_() const {
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"width\":%.2f,\"length\":%.2f,\"height\":%.2f,\"radarHeight\":%.2f,\"wallOffsetM\":%.2f,\"mount\":%u}",
           this->room_config_.width, this->room_config_.length, this->room_config_.height,
           this->room_config_.radar_height, this->room_config_.wall_offset_m, this->room_config_.mount);
  return std::string(buf);
}

esp_err_t HlkLd6001aComponent::http_handle_room_get_(httpd_req_t *req) {
  auto *self = static_cast<HlkLd6001aComponent *>(req->user_ctx);
  httpd_resp_set_type(req, "application/json");
  std::string json = self->room_config_json_();
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t HlkLd6001aComponent::http_handle_room_post_(httpd_req_t *req) {
  auto *self = static_cast<HlkLd6001aComponent *>(req->user_ctx);

  char body[128] = {};
  int to_read = std::min<int>(req->content_len, sizeof(body) - 1);
  int read_len = httpd_req_recv(req, body, to_read);
  if (read_len <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  body[read_len] = '\0';
  drain_remaining_body_(req, static_cast<size_t>(read_len));

  RoomConfig cfg = self->room_config_;
  char val[16];
  if (httpd_query_key_value(body, "width", val, sizeof(val)) == ESP_OK) {
    float v = strtof(val, nullptr);
    if (v > 0 && v <= ROOM_MAX_WIDTH_LENGTH_M)
      cfg.width = v;
  }
  if (httpd_query_key_value(body, "length", val, sizeof(val)) == ESP_OK) {
    float v = strtof(val, nullptr);
    if (v > 0 && v <= ROOM_MAX_WIDTH_LENGTH_M)
      cfg.length = v;
  }
  if (httpd_query_key_value(body, "height", val, sizeof(val)) == ESP_OK) {
    float v = strtof(val, nullptr);
    if (v > 0 && v <= ROOM_MAX_HEIGHT_M)
      cfg.height = v;
  }
  if (httpd_query_key_value(body, "radar_height", val, sizeof(val)) == ESP_OK) {
    float v = strtof(val, nullptr);
    if (v > 0 && v <= ROOM_MAX_HEIGHT_M)
      cfg.radar_height = v;
  }
  if (httpd_query_key_value(body, "wall_offset_m", val, sizeof(val)) == ESP_OK) {
    float v = strtof(val, nullptr);
    if (v >= 0 && v <= ROOM_MAX_WIDTH_LENGTH_M)
      cfg.wall_offset_m = v;
  }
  if (httpd_query_key_value(body, "mount", val, sizeof(val)) == ESP_OK) {
    int v = atoi(val);
    if (v >= 0 && v <= 1)
      cfg.mount = static_cast<uint8_t>(v);
  }

  self->room_config_ = cfg;
  self->room_config_pref_.save(&self->room_config_);

  httpd_resp_set_type(req, "application/json");
  std::string json = self->room_config_json_();
  httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t HlkLd6001aComponent::http_handle_root_(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, VIEWER_HTML, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t HlkLd6001aComponent::http_handle_targets_json_(httpd_req_t *req) {
  auto *self = static_cast<HlkLd6001aComponent *>(req->user_ctx);
  std::string json = "{\"count\":" + std::to_string(self->latest_num_people_) + ",\"targets\":[";
  uint8_t shown = std::min<uint8_t>(self->latest_num_people_, MAX_TARGETS);
  for (uint8_t i = 0; i < shown; i++) {
    if (i > 0)
      json += ",";
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"id\":%u,\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"vx\":%.2f,\"vy\":%.2f,\"vz\":%.2f}",
             static_cast<unsigned>(self->latest_id_[i]), self->latest_x_[i], self->latest_y_[i], self->latest_z_[i],
             self->latest_vx_[i], self->latest_vy_[i], self->latest_vz_[i]);
    json += buf;
  }
  json += "]}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, json.c_str(), json.size());
  return ESP_OK;
}

void HlkLd6001aComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "HLK-LD6001A mmWave radar:");
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Has Target", this->has_target_binary_sensor_);
#endif
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Target Count", this->target_count_sensor_);
  LOG_SENSOR("  ", "Recovery Count", this->recovery_count_sensor_);
  for (uint8_t i = 0; i < MAX_TARGETS; i++) {
    LOG_SENSOR("  ", "Target X", this->target_x_sensors_[i]);
    LOG_SENSOR("  ", "Target Y", this->target_y_sensors_[i]);
    LOG_SENSOR("  ", "Target Z", this->target_z_sensors_[i]);
    LOG_SENSOR("  ", "Target Vx", this->target_vx_sensors_[i]);
    LOG_SENSOR("  ", "Target Vy", this->target_vy_sensors_[i]);
    LOG_SENSOR("  ", "Target Vz", this->target_vz_sensors_[i]);
    LOG_SENSOR("  ", "Target ID", this->target_id_sensors_[i]);
  }
#endif
}

void HlkLd6001aComponent::send_reinit_sequence_() {
  ESP_LOGW(TAG, "sending radar re-init sequence (AT+RESET, settings, AT+DEBUG=3, AT+START)");
  // Same reasoning as the 6001B project: abandon whatever is already
  // queued (stale by definition once the watchdog has fired) instead of
  // appending behind it -- see that project's identical comment.
  while (!this->at_command_queue_.empty()) {
    auto cb = this->at_command_queue_.front().on_result;
    this->at_command_queue_.pop_front();
    if (cb)
      cb(false, "");
  }
  this->at_command_in_flight_ = false;
  send_command(this, "AT+RESET\n");
  this->set_timeout("hlk_ld6001a_reinit_settings", 3000, [this]() {
    this->apply_radar_settings_();
    this->start_after_settings_queue_drains_(0);
  });
}

void HlkLd6001aComponent::check_watchdog_() {
  uint32_t now = millis();
  if (now - this->last_frame_millis_ < WATCHDOG_TIMEOUT_MS)
    return;
  if (now - this->last_recovery_millis_ < WATCHDOG_COOLDOWN_MS)
    return;

  ESP_LOGW(TAG, "no radar frame received for %u ms -- forcing recovery",
           static_cast<unsigned>(now - this->last_frame_millis_));
  this->last_recovery_millis_ = now;
  this->last_frame_millis_ = now;
  this->recovery_count_++;
#ifdef USE_SENSOR
  if (this->recovery_count_sensor_ != nullptr)
    this->recovery_count_sensor_->publish_state(this->recovery_count_);
#endif
  this->send_reinit_sequence_();
}

// Periodic AT+READ -- the "Etat radar" replacement for a heartbeat frame
// whose byte layout isn't documented for this module (see PROTOCOL.md).
// Enqueued through the same ack/timeout-gated queue as everything else so
// it never collides with a settings/console command in flight.
void HlkLd6001aComponent::poll_at_read_() {
  uint32_t now = millis();
  if (now - this->last_at_read_poll_millis_ < AT_READ_POLL_INTERVAL_MS)
    return;
  this->last_at_read_poll_millis_ = now;
  this->enqueue_at_command_("AT+READ\n", [this](bool ok, const std::string &raw) {
    if (!ok || raw.empty())
      return;
    this->live_read_seen_ = true;
    this->live_read_raw_ = raw;
    this->live_read_millis_ = millis();
  });
}

void HlkLd6001aComponent::loop() {
  // Deferred from setup() -- same crash-on-early-httpd_start() reasoning
  // as the 6001B project (network stack not ready yet at DATA-priority
  // setup()).
  if (!this->http_server_start_attempted_ && network::is_connected()) {
    this->http_server_start_attempted_ = true;
    this->start_http_server_();
  }

  // Gates sending saved radar settings (then AT+DEBUG=3/AT+START) on a
  // confirmed WiFi connection -- same brownout-avoidance reasoning as the
  // 6001B project (see PROTOCOL.md, "Séquence de démarrage").
  if (!this->radar_settings_start_attempted_ && (network::is_connected() || millis() > 32000)) {
    this->radar_settings_start_attempted_ = true;
    this->set_timeout("hlk_ld6001a_apply_settings_boot", 5000, [this]() {
      this->apply_radar_settings_();
      this->start_after_settings_queue_drains_(0);
    });
  }

  this->service_at_command_queue_();

  if (this->pending_apply_radar_settings_) {
    this->pending_apply_radar_settings_ = false;
    this->apply_radar_settings_();
  }

  this->poll_at_read_();

  size_t avail = this->available();
  if (avail == 0) {
    this->check_watchdog_();
    return;
  }

  uint8_t buf[128];
  while (avail > 0) {
    size_t to_read = std::min(avail, sizeof(buf));
    if (!this->read_array(buf, to_read))
      break;
    this->buffer_.insert(this->buffer_.end(), buf, buf + to_read);
    avail -= to_read;
  }

  // Two frame formats share this UART stream: the default AT+DEBUG=0
  // protocol (55 AA, recognized+checksummed but not decoded -- see
  // PROTOCOL.md) and the AT+DEBUG=3 frames this component actually reads
  // (headed by 01 02 03 04 05 06 07 08). Same bounded-resync design as
  // the 6001B project (MAX_RESYNC_STEPS_PER_LOOP) so a long run of line
  // noise can't starve other components' servicing within one loop() call.
  uint16_t resync_steps = 0;
  static const uint16_t MAX_RESYNC_STEPS_PER_LOOP = 64;
  while (true) {
    if (resync_steps >= MAX_RESYNC_STEPS_PER_LOOP)
      break;
    if (this->at_command_in_flight_) {
      auto ok_it = std::search(this->buffer_.begin(), this->buffer_.end(), std::begin(AT_OK_TOKEN),
                                std::end(AT_OK_TOKEN) - 1);
      auto err_it = std::search(this->buffer_.begin(), this->buffer_.end(), std::begin(AT_ERR_TOKEN),
                                 std::end(AT_ERR_TOKEN) - 1);
      if (ok_it != this->buffer_.end() || err_it != this->buffer_.end()) {
        bool is_err = (err_it != this->buffer_.end()) && (ok_it == this->buffer_.end() || err_it < ok_it);
        auto match_it = is_err ? err_it : ok_it;
        size_t match_start = static_cast<size_t>(match_it - this->buffer_.begin());
        size_t line_end = match_start;
        while (line_end < this->buffer_.size() && this->buffer_[line_end] != '\r' && this->buffer_[line_end] != '\n')
          line_end++;
        if (line_end >= this->buffer_.size())
          break;
        while (line_end < this->buffer_.size() && (this->buffer_[line_end] == '\r' || this->buffer_[line_end] == '\n'))
          line_end++;
        std::string line(this->buffer_.begin() + match_start, this->buffer_.begin() + line_end);
        this->buffer_.erase(this->buffer_.begin(), this->buffer_.begin() + line_end);
        this->handle_at_response_line_(line);
        continue;
      }
    }

    auto old_it = std::search(this->buffer_.begin(), this->buffer_.end(), std::begin(OLD_HEADER), std::end(OLD_HEADER));
    auto new_it =
        std::search(this->buffer_.begin(), this->buffer_.end(), std::begin(DEBUG3_HEAD), std::end(DEBUG3_HEAD));
    size_t old_idx = (old_it == this->buffer_.end()) ? SIZE_MAX : static_cast<size_t>(old_it - this->buffer_.begin());
    size_t new_idx = (new_it == this->buffer_.end()) ? SIZE_MAX : static_cast<size_t>(new_it - this->buffer_.begin());

    if (old_idx == SIZE_MAX && new_idx == SIZE_MAX) {
      if (this->buffer_.size() >= sizeof(DEBUG3_HEAD))
        this->buffer_.erase(this->buffer_.begin(), this->buffer_.end() - (sizeof(DEBUG3_HEAD) - 1));
      break;
    }

    bool is_debug3 = new_idx < old_idx;
    size_t idx = is_debug3 ? new_idx : old_idx;
    if (idx > 0)
      this->buffer_.erase(this->buffer_.begin(), this->buffer_.begin() + idx);

    if (is_debug3) {
      if (this->buffer_.size() < 32)
        break;  // wait for the rest of the fixed-size prefix
      uint32_t length;
      memcpy(&length, this->buffer_.data() + 8, 4);
      if (length < 32 || length > MAX_DEBUG3_FRAME_LEN) {
        ESP_LOGW(TAG, "implausible DEBUG3 LENGTH=%u, resyncing", static_cast<unsigned>(length));
        this->buffer_.erase(this->buffer_.begin());
        resync_steps++;
        continue;
      }
      // +1: LENGTH excludes the trailing CHECK byte -- see PROTOCOL.md.
      if (this->buffer_.size() < length + 1)
        break;  // wait for more bytes (including the CHECK byte)

      // Checksum: XOR of FRAME (4 bytes @ offset 12) + all person-record
      // bytes -- computed here (not inside process_debug3_frame_) so a
      // mismatch triggers the same byte-at-a-time resync as a bad
      // checksum on the old 55 AA protocol below, instead of silently
      // accepting corrupted target data. See PROTOCOL.md.
      uint32_t point_len_check;
      memcpy(&point_len_check, this->buffer_.data() + 20, 4);
      bool checksum_ok = false;
      if (point_len_check <= length - 24) {
        size_t tlv2_offset = 24 + point_len_check;
        if (tlv2_offset + 8 <= length) {
          uint32_t track_len_check;
          memcpy(&track_len_check, this->buffer_.data() + tlv2_offset + 4, 4);
          size_t people_start = tlv2_offset + 8;
          if (track_len_check <= length - people_start) {
            uint8_t computed = 0;
            for (size_t i = 12; i < 16; i++)
              computed ^= this->buffer_[i];
            for (size_t i = people_start; i < people_start + track_len_check; i++)
              computed ^= this->buffer_[i];
            checksum_ok = (computed == this->buffer_[length]);
          }
        }
      }
      if (!checksum_ok) {
        ESP_LOGW(TAG, "DEBUG3 checksum mismatch or implausible POINTLEN/TRACKLEN, resyncing");
        this->buffer_.erase(this->buffer_.begin());
        resync_steps++;
        continue;
      }

      this->process_debug3_frame_(this->buffer_.data(), length);
      this->buffer_.erase(this->buffer_.begin(), this->buffer_.begin() + length + 1);
      continue;
    }

    // Old 55 AA / AT+DEBUG=0 protocol: recognized and checksum-validated
    // (same XOR algorithm confirmed against the manual's own worked
    // example, see PROTOCOL.md) so it's consumed cleanly and feeds the
    // watchdog, but never decoded (TYPE=0x04 only carries a redundant
    // person count, no coordinates).
    if (this->buffer_.size() < 3)
      break;
    uint8_t length8 = this->buffer_[2];
    if (length8 < 5) {
      this->buffer_.erase(this->buffer_.begin());
      resync_steps++;
      continue;
    }
    if (this->buffer_.size() < length8)
      break;
    uint8_t computed8 = 0;
    for (size_t i = 2; i < static_cast<size_t>(length8) - 1; i++)
      computed8 ^= this->buffer_[i];
    uint8_t expected8 = this->buffer_[length8 - 1];
    if (computed8 != expected8) {
      ESP_LOGW(TAG, "old-protocol checksum mismatch (got 0x%02X, expected 0x%02X), resyncing", expected8, computed8);
      this->buffer_.erase(this->buffer_.begin());
      resync_steps++;
      continue;
    }
    this->last_frame_millis_ = millis();  // proves the module is alive, even though we don't decode this frame
    this->buffer_.erase(this->buffer_.begin(), this->buffer_.begin() + length8);
  }

  if (this->buffer_.size() > MAX_BUFFER)
    this->buffer_.clear();
}

// Direct port of testing/radar_protocol_debug3.py's parse_debug3_frame()
// -- see that file and PROTOCOL.md for the field layout. `len` here is
// the LENGTH field's own value (checksum already validated by the caller
// in loop(), which also knows where the CHECK byte sits at frame[len]).
void HlkLd6001aComponent::process_debug3_frame_(const uint8_t *frame, size_t len) {
  this->last_frame_millis_ = millis();

  uint32_t point_len;
  memcpy(&point_len, frame + 20, 4);
  size_t tlv2_offset = 24 + point_len;
  uint32_t track_len;
  memcpy(&track_len, frame + tlv2_offset + 4, 4);
  size_t people_start = tlv2_offset + 8;

  uint8_t num_people = static_cast<uint8_t>(track_len / 32);
  this->latest_num_people_ = std::min<uint8_t>(num_people, MAX_TARGETS);

  // Throttled publish to HA (see PUBLISH_THROTTLE_MS) -- the raw
  // latest_*_ arrays backing /hlk_targets.json are still updated on every
  // valid frame regardless (3D viewer stays a live, unfiltered view, same
  // "never debounced" principle as the 6001B project).
  uint32_t now = millis();
  bool do_publish = (now - this->last_publish_millis_) >= PUBLISH_THROTTLE_MS;

  for (uint8_t i = 0; i < MAX_TARGETS; i++) {
    uint32_t id = 0;
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
    if (i < num_people) {
      size_t off = people_start + i * 32;
      memcpy(&id, frame + off + 4, 4);
      memcpy(&x, frame + off + 8, 4);
      memcpy(&y, frame + off + 12, 4);
      memcpy(&z, frame + off + 16, 4);
      memcpy(&vx, frame + off + 20, 4);
      memcpy(&vy, frame + off + 24, 4);
      memcpy(&vz, frame + off + 28, 4);

      this->latest_id_[i] = id;
      this->latest_x_[i] = x;
      this->latest_y_[i] = y;
      this->latest_z_[i] = z;
      this->latest_vx_[i] = vx;
      this->latest_vy_[i] = vy;
      this->latest_vz_[i] = vz;
    }
#ifdef USE_SENSOR
    if (!do_publish)
      continue;
    if (this->target_id_sensors_[i] != nullptr)
      this->target_id_sensors_[i]->publish_state(id);
    if (this->target_x_sensors_[i] != nullptr)
      this->target_x_sensors_[i]->publish_state(x);
    if (this->target_y_sensors_[i] != nullptr)
      this->target_y_sensors_[i]->publish_state(y);
    if (this->target_z_sensors_[i] != nullptr)
      this->target_z_sensors_[i]->publish_state(z);
    if (this->target_vx_sensors_[i] != nullptr)
      this->target_vx_sensors_[i]->publish_state(vx);
    if (this->target_vy_sensors_[i] != nullptr)
      this->target_vy_sensors_[i]->publish_state(vy);
    if (this->target_vz_sensors_[i] != nullptr)
      this->target_vz_sensors_[i]->publish_state(vz);
#endif
  }

  // Installation geometry filter + presence/count -- same reasoning as the
  // 6001B project's process_monitoring_(), simplified: no debounce here
  // (that was specifically for a multipath-ghost pattern observed on the
  // 6001B's own protocol; not re-added here without an equivalent
  // real-hardware observation on the 6001A -- see PLAN.md, "reportés à une
  // itération future" only lists items with an explicit decision, this
  // one simply has no evidence yet either way).
  uint8_t real_people = 0;
  for (uint8_t i = 0; i < std::min<uint8_t>(num_people, MAX_TARGETS); i++) {
    float distance = std::sqrt(this->latest_x_[i] * this->latest_x_[i] + this->latest_y_[i] * this->latest_y_[i] +
                                this->latest_z_[i] * this->latest_z_[i]);
    if (distance >= MIN_TARGET_DISTANCE_M)
      real_people++;
  }
  uint8_t untracked_extra = num_people > MAX_TARGETS ? num_people - MAX_TARGETS : 0;
  real_people += untracked_extra;

#ifdef USE_SENSOR
  if (do_publish) {
    if (this->target_count_sensor_ != nullptr)
      this->target_count_sensor_->publish_state(real_people);
  }
#endif
#ifdef USE_BINARY_SENSOR
  // has_target stays instant, never throttled -- same reasoning as the
  // 6001B project: presence must never lag behind reality, only the
  // higher-cardinality per-target sensors are rate-limited.
  if (this->has_target_binary_sensor_ != nullptr)
    this->has_target_binary_sensor_->publish_state(real_people > 0);
#endif

  if (do_publish)
    this->last_publish_millis_ = now;
}

}  // namespace esphome::hlk_ld6001a
