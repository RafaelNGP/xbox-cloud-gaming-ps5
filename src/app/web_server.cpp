// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/web_server.h"
#include "app/report.h"
#include "app/settings.h"
#include "input/key_map.h"
#include "input/pad_map.h"
#include "platform/platform.h"
#include "ui/app_ui.h"
#include "util/json.h"
#include "util/log.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace xc::app {

static WebServerHooks s_hooks;

namespace {

const char* kIndexHtml = R"html(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>PSBox Cloud Gaming - Web Control</title>
<style>
:root {
  --bg: #0b0d13;
  --surface: #141722;
  --surface-alt: #1c2130;
  --border: #282f44;
  --primary: #107C41;
  --primary-hover: #169c53;
  --accent: #0070d8;
  --text: #f0f3f8;
  --text-dim: #9aa4b8;
  --danger: #d93838;
}
* { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
html, body { width: 100%; max-width: 100%; overflow-x: hidden; background: var(--bg); color: var(--text); padding-bottom: 40px; overscroll-behavior: none; }
header { box-sizing: border-box; background: var(--surface); border-bottom: 1px solid var(--border); padding: 12px 14px; display: flex; align-items: center; justify-content: space-between; gap: 10px; width: 100%; max-width: 100%; overflow-x: hidden; }
.brand { display: flex; align-items: center; gap: 10px; min-width: 0; flex: 1 1 auto; }
.logo-icon { flex-shrink: 0; width: 32px; height: 32px; border-radius: 50%; box-shadow: 0 0 10px rgba(16, 124, 65, 0.45); }
.brand-text { min-width: 0; flex: 1 1 auto; }
.brand-text h1 { font-size: clamp(0.76rem, 3.6vw, 0.96rem); font-weight: 700; letter-spacing: 0.5px; color: #fff; text-transform: uppercase; line-height: 1.2; white-space: nowrap; }
.brand-subtitle { font-size: 0.70rem; color: var(--text-dim); display: block; font-weight: 500; white-space: nowrap; }
header .badge { background: var(--primary); color: #fff; font-size: 0.70rem; padding: 3px 7px; border-radius: 12px; font-weight: 600; text-transform: uppercase; flex-shrink: 0; }
.nav-tabs { display: flex; background: var(--surface); border-bottom: 1px solid var(--border); width: 100%; max-width: 100%; overflow-x: hidden; }
.nav-tab { flex: 1 1 0; min-width: 0; text-align: center; padding: 12px 6px; font-size: clamp(0.78rem, 3.2vw, 0.88rem); font-weight: 600; color: var(--text-dim); border-bottom: 2px solid transparent; cursor: pointer; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; user-select: none; }
.nav-tab.active { color: #fff; border-bottom-color: var(--primary); background: var(--surface-alt); }
.content { max-width: 680px; width: 100%; margin: 14px auto; padding: 0 12px; overflow-x: hidden; }
.subnav { display: flex; gap: 8px; margin-bottom: 12px; flex-wrap: wrap; width: 100%; max-width: 100%; }
.subnav .btn { flex: 1 1 120px; }
.subnav .btn.active { background: var(--primary); border-color: var(--primary); }
.card { background: var(--surface); border: 1px solid var(--border); border-radius: 14px; padding: 14px; margin-bottom: 14px; width: 100%; max-width: 100%; overflow-x: hidden; }
.card h2 { font-size: 1.0rem; margin-bottom: 8px; font-weight: 600; display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.card p { font-size: 0.82rem; color: var(--text-dim); margin-bottom: 12px; line-height: 1.4; word-break: break-word; }

/* Search tabs & chips */
.search-tabs { display: flex; gap: 8px; margin-bottom: 12px; width: 100%; max-width: 100%; }
.search-tab { flex: 1 1 0; min-width: 0; padding: 10px 8px; background: var(--surface-alt); border: 1px solid var(--border); border-radius: 10px; color: var(--text-dim); font-size: clamp(0.82rem, 3.4vw, 0.88rem); font-weight: 600; cursor: pointer; transition: all 0.2s; text-align: center; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; outline: none; }
.search-tab:hover { border-color: var(--primary); color: #fff; }
.search-tab.active { background: var(--primary); border-color: var(--primary); color: #fff; box-shadow: 0 2px 8px rgba(16, 124, 65, 0.4); }
.filter-section { margin-top: 12px; margin-bottom: 12px; width: 100%; max-width: 100%; }
.filter-label { font-size: 0.76rem; font-weight: 600; color: var(--text-dim); text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 8px; }
.filter-chips { display: flex; flex-wrap: wrap; gap: 8px; width: 100%; max-width: 100%; }
.chip { display: inline-flex; align-items: center; justify-content: center; gap: 6px; padding: 6px 11px; border-radius: 20px; font-size: 0.80rem; font-weight: 600; background: var(--surface-alt); border: 1px solid var(--border); color: var(--text-dim); cursor: pointer; transition: all 0.2s; user-select: none; max-width: 100%; outline: none; white-space: nowrap; }
.chip:hover { border-color: var(--primary-hover); color: var(--text); }
.chip.active { background: rgba(16, 124, 65, 0.28); border-color: var(--primary); color: #4ade80; }
.chip-select { display: inline-flex; align-items: center; gap: 5px; padding: 4px 9px; border-radius: 20px; background: var(--surface-alt); border: 1px solid var(--border); font-size: 0.80rem; color: var(--text-dim); font-weight: 600; max-width: 100%; }
.chip-select.active { border-color: var(--primary); background: rgba(16, 124, 65, 0.28); color: #4ade80; }
.chip-select select { background: transparent; border: none; color: #fff; font-size: 0.80rem; font-weight: 600; outline: none; cursor: pointer; padding: 2px 0; max-width: 100%; }
.chip-select select option { background: var(--surface); color: #fff; }

.input-group { display: flex; gap: 8px; margin-bottom: 12px; width: 100%; max-width: 100%; }
input[type="text"] { flex: 1 1 auto; min-width: 0; background: var(--surface-alt); border: 1px solid var(--border); border-radius: 10px; padding: 10px 12px; font-size: 0.92rem; color: #fff; outline: none; transition: border-color 0.2s; }
input[type="text"]:focus { border-color: var(--primary); }
.btn { display: inline-flex; align-items: center; justify-content: center; background: var(--surface-alt); color: #fff; border: 1px solid var(--border); border-radius: 10px; padding: 10px 12px; font-size: 0.86rem; font-weight: 600; cursor: pointer; text-decoration: none; transition: background 0.2s, border-color 0.2s; max-width: 100%; white-space: normal; text-align: center; }
.btn:hover { background: var(--border); }
.btn-primary { background: var(--primary); border-color: var(--primary); color: #fff; }
.btn-primary:hover { background: var(--primary-hover); }
.status-pill { display: inline-flex; align-items: center; gap: 6px; padding: 6px 12px; border-radius: 20px; font-size: 0.80rem; background: rgba(16, 124, 65, 0.2); color: #4ade80; border: 1px solid rgba(16, 124, 65, 0.4); margin-top: 4px; max-width: 100%; word-break: break-word; }
.status-dot { width: 8px; height: 8px; border-radius: 50%; background: #4ade80; flex-shrink: 0; }
.grid-map { display: grid; grid-template-columns: 1fr; gap: 8px; margin-top: 12px; width: 100%; max-width: 100%; }
@media (min-width: 580px) {
  .grid-map { grid-template-columns: repeat(2, 1fr); }
}
.map-item { background: var(--surface-alt); border: 1px solid var(--border); border-radius: 10px; padding: 9px 11px; display: flex; align-items: center; justify-content: space-between; gap: 8px; width: 100%; max-width: 100%; min-width: 0; flex-wrap: wrap; }
.map-item label { font-size: 0.84rem; font-weight: 600; flex: 1 1 110px; min-width: 0; word-break: break-word; }
.selects-wrap { display: flex; gap: 6px; flex-wrap: wrap; flex: 1 1 180px; min-width: 0; justify-content: flex-end; }
.selects-wrap select { flex: 1 1 calc(50% - 4px); min-width: 85px; max-width: 100%; }
.map-item > select { flex: 0 1 auto; max-width: 100%; min-width: 0; }
select { background: var(--surface); color: #fff; border: 1px solid var(--border); border-radius: 8px; padding: 7px 9px; font-size: 0.80rem; outline: none; min-width: 0; }
.btn-row { margin-top: 16px; display: flex; gap: 8px; flex-wrap: wrap; width: 100%; max-width: 100%; }
.btn-row .btn { flex: 1 1 130px; }
pre { background: #07080b; border: 1px solid var(--border); border-radius: 10px; padding: 12px; font-family: monospace; font-size: 0.78rem; color: #c4ccdc; overflow-x: auto; max-height: 380px; white-space: pre-wrap; word-break: break-all; max-width: 100%; }
.toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #169c53; color: #fff; padding: 10px 20px; border-radius: 24px; font-size: 0.88rem; font-weight: 600; box-shadow: 0 4px 16px rgba(0,0,0,0.5); opacity: 0; pointer-events: none; transition: opacity 0.3s; z-index: 1000; max-width: 90%; text-align: center; }
.toast.show { opacity: 1; }

/* Telemetry & interactive controls */
.telemetry-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-bottom: 14px; width: 100%; }
.tel-item { background: var(--surface-alt); border: 1px solid var(--border); border-radius: 10px; padding: 9px 6px; text-align: center; }
.tel-val { font-size: 1.10rem; font-weight: 700; color: #4ade80; }
.tel-lbl { font-size: 0.70rem; color: var(--text-dim); text-transform: uppercase; margin-top: 3px; }
.slider-group { display: flex; flex-direction: column; gap: 4px; margin-bottom: 12px; width: 100%; }
.slider-header { display: flex; justify-content: space-between; font-size: 0.84rem; font-weight: 600; }
.slider-val { color: #4ade80; }
input[type="range"] { width: 100%; accent-color: var(--primary); margin: 6px 0; }
.toggle-row { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid var(--border); }
.toggle-label { font-size: 0.86rem; font-weight: 600; }
.switch { position: relative; display: inline-block; width: 44px; height: 24px; flex-shrink: 0; }
.switch input { opacity: 0; width: 0; height: 0; }
.slider-sw { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: var(--surface-alt); border: 1px solid var(--border); border-radius: 24px; transition: .25s; }
.slider-sw:before { position: absolute; content: ""; height: 16px; width: 16px; left: 3px; bottom: 3px; background-color: white; border-radius: 50%; transition: .25s; }
input:checked + .slider-sw { background-color: var(--primary); border-color: var(--primary); }
input:checked + .slider-sw:before { transform: translateX(20px); }
.btn-danger { background: var(--danger); border-color: var(--danger); color: #fff; }
.btn-danger:hover { background: #b82626; }


/* Search overlay when streaming */
.search-disabled-overlay {
  position: absolute;
  top: 0; left: 0; right: 0; bottom: 0;
  background: rgba(11, 13, 19, 0.85);
  backdrop-filter: blur(8px);
  -webkit-backdrop-filter: blur(8px);
  display: none;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  border-radius: 14px;
  z-index: 50;
  color: #fff;
  text-align: center;
  padding: 20px;
}
.search-disabled-overlay .msg {
  font-size: 0.95rem;
  font-weight: 700;
  letter-spacing: 0.6px;
  background: rgba(20, 23, 34, 0.95);
  border: 1px solid var(--border);
  padding: 12px 22px;
  border-radius: 20px;
  box-shadow: 0 4px 16px rgba(0,0,0,0.6);
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

/* Virtual Gamepad */
.gamepad-container {
  user-select: none;
  -webkit-user-select: none;
  touch-action: none;
  width: 100%;
  max-width: 100%;
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.player-select-row {
  display: flex;
  gap: 8px;
  width: 100%;
  margin-bottom: 6px;
}
.player-btn {
  flex: 1 1 0;
  padding: 10px 8px;
  border-radius: 10px;
  border: 1px solid var(--border);
  background: var(--surface-alt);
  color: var(--text-dim);
  font-size: clamp(0.78rem, 3.2vw, 0.88rem);
  font-weight: 700;
  cursor: pointer;
  text-align: center;
  transition: all 0.2s;
}
.player-btn.active {
  background: var(--primary);
  border-color: var(--primary);
  color: #fff;
  box-shadow: 0 2px 8px rgba(16, 124, 65, 0.4);
}
.pad-top-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  width: 100%;
}
.pad-bumper-group {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.pad-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: var(--surface-alt);
  border: 1px solid var(--border);
  border-radius: 10px;
  color: #fff;
  font-weight: 700;
  cursor: pointer;
  touch-action: none;
  user-select: none;
  -webkit-user-select: none;
  transition: transform 0.08s, background 0.08s, border-color 0.08s;
  box-shadow: 0 2px 5px rgba(0,0,0,0.3);
}
.pad-btn:active, .pad-btn.pressed {
  transform: scale(0.92);
  background: var(--primary) !important;
  border-color: #4ade80 !important;
  box-shadow: 0 0 10px rgba(16, 124, 65, 0.6) !important;
}
.pad-btn-sm {
  width: 68px;
  height: 38px;
  font-size: 0.80rem;
}
.pad-center-group {
  display: flex;
  gap: 6px;
  align-items: center;
}
.pad-btn-center {
  padding: 8px 12px;
  font-size: 0.72rem;
  border-radius: 8px;
  background: #1e2433;
}
.pad-btn-nexus {
  width: 44px;
  height: 44px;
  border-radius: 50%;
  background: #107c41;
  border: 2px solid #fff;
  font-size: 0.80rem;
  font-weight: 900;
}
.pad-main-row {
  display: flex;
  justify-content: space-around;
  align-items: center;
  gap: 12px;
  margin-top: 8px;
  width: 100%;
}
.pad-side {
  flex: 1 1 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 14px;
}
.dpad-cross {
  position: relative;
  width: 136px;
  height: 136px;
}
.dpad-btn {
  position: absolute;
  width: 44px;
  height: 44px;
  border-radius: 8px;
  font-size: 1.1rem;
}
.dpad-up { top: 0; left: 46px; }
.dpad-down { bottom: 0; left: 46px; }
.dpad-left { top: 46px; left: 0; }
.dpad-right { top: 46px; right: 0; }
.dpad-center {
  position: absolute;
  top: 46px; left: 46px;
  width: 44px; height: 44px;
  background: var(--surface);
}
.abxy-group {
  position: relative;
  width: 136px;
  height: 136px;
}
.abxy-btn {
  position: absolute;
  width: 44px;
  height: 44px;
  border-radius: 50%;
  font-size: 1.15rem;
  font-weight: 900;
  color: #fff;
}
.btn-y { top: 0; left: 46px; background: #c68900; border-color: #ffc400; }
.btn-a { bottom: 0; left: 46px; background: #0e6b37; border-color: #107c41; }
.btn-x { top: 46px; left: 0; background: #006eb8; border-color: #00a2ed; }
.btn-b { top: 46px; right: 0; background: #b80d1b; border-color: #e81123; }
.stick-zone {
  position: relative;
  width: 110px;
  height: 110px;
  border-radius: 50%;
  background: var(--surface-alt);
  border: 2px solid var(--border);
  touch-action: none;
  display: flex;
  align-items: center;
  justify-content: center;
}
.stick-head {
  width: 50px;
  height: 50px;
  border-radius: 50%;
  background: radial-gradient(circle at 35% 35%, #3a445e, #1c2130);
  border: 2px solid #58688a;
  box-shadow: 0 4px 8px rgba(0,0,0,0.5);
  pointer-events: none;
  transform: translate(0px, 0px);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 0.70rem;
  font-weight: 800;
  color: var(--text-dim);
}
</style>
</head>
<body>
<header>
  <div class="brand">
    <svg class="logo-icon" viewBox="0 0 100 100" fill="none" xmlns="http://www.w3.org/2000/svg">
      <circle cx="50" cy="50" r="50" fill="#ffffff"/>
      <path fill="#107C41" d="M 50.00 40.10 s 0.12 0.00 0.00 0.00 c 15.07 11.46 40.60 39.60 32.88 47.57 -8.84 7.72 -20.30 12.33 -32.88 12.33 s -24.16 -4.61 -32.88 -12.33 c -7.85 -7.97 17.81 -36.11 32.75 -47.45 0.00 -0.12 0.12 -0.12 0.12 -0.12 z m 26.03 -32.75 C 68.43 2.74 60.09 0.00 50.00 0.00 s -18.43 2.74 -26.03 7.35 c -0.12 0.00 -0.12 0.12 -0.12 0.25 s 0.12 0.12 0.25 0.12 c 9.71 -2.12 24.41 6.23 25.78 7.10 h 0.25 c 1.37 -0.87 16.06 -9.22 25.78 -7.10 0.12 0.00 0.25 0.00 0.25 -0.12 s 0.00 -0.25 -0.12 -0.25 z M 15.01 14.32 c -0.12 0.00 -0.12 0.12 -0.25 0.12 -9.09 9.09 -14.69 21.67 -14.69 35.49 0.00 11.33 3.86 21.92 10.21 30.26 0.00 0.12 0.12 0.12 0.25 0.12 s 0.12 -0.12 0.00 -0.25 c -3.86 -11.83 15.69 -40.35 25.78 -52.30 l 0.12 -0.12 c 0.00 -0.12 0.00 -0.12 -0.12 -0.12 -15.32 -15.19 -20.42 -13.57 -21.30 -13.20 z m 48.69 13.08 l -0.12 0.12 s 0.00 0.12 0.12 0.12 c 10.09 11.96 29.51 40.47 25.78 52.30 v 0.25 c 0.12 0.00 0.25 0.00 0.25 -0.12 6.35 -8.34 10.21 -18.93 10.21 -30.26 0.00 -13.82 -5.60 -26.40 -14.82 -35.49 -0.12 -0.12 -0.12 -0.12 -0.25 -0.12 -0.75 -0.25 -5.85 -1.87 -21.17 13.20 z"/>
    </svg>
    <div class="brand-text">
      <h1>PSBOX CLOUD GAMING</h1>
      <span class="brand-subtitle">Painel Web</span>
    </div>
  </div>
  <span class="badge">v1.4.0</span>
</header>

<div class="nav-tabs">
  <div class="nav-tab active" id="navTab-search" onclick="switchTab('search')">🔍 Busca</div>
  <div class="nav-tab" id="navTab-settings" onclick="switchTab('settings')">⚙️ Configurações</div>
  <div class="nav-tab" id="navTab-stream" onclick="switchTab('stream')">📺 Jogo Atual</div>
  <div class="nav-tab" id="navTab-remap" onclick="switchTab('remap')">🎮 Mapeamento</div>
  <div class="nav-tab" id="navTab-gamepad" onclick="switchTab('gamepad')">🕹️ Controle</div>
  <div class="nav-tab" id="navTab-logs" onclick="switchTab('logs')">📋 Logs</div>
</div>

<div class="content">
  <!-- TAB: SETTINGS -->
  <div id="tab-settings" class="tab-pane" style="display:none;">
    <div class="card">
      <h2>⚙️ Configurações Gerais</h2>
      <p>Ajuste opções gráficas, de controle e comportamento do aplicativo.</p>
      
      <div class="slider-group">
        <div class="slider-header">
          <span>Bitrate Máximo</span>
          <span class="slider-val" id="valBitrate">Auto</span>
        </div>
        <input type="range" id="inputBitrate" min="0" max="50" step="5" value="0" oninput="onBitrateInput(this.value)">
      </div>

      <div class="toggle-row">
        <span class="toggle-label">Modo 120 Hz (High Refresh / VRR)</span>
        <label class="switch">
          <input type="checkbox" id="check120Hz" onchange="markSettingsChanged()">
          <span class="slider-sw"></span>
        </label>
      </div>

      <div class="slider-group" style="margin-top: 10px;">
        <div class="slider-header">
          <span>Zona Morta - Analógico Esquerdo (L)</span>
          <span class="slider-val" id="valDeadzoneL">15%</span>
        </div>
        <input type="range" id="inputDeadzoneL" min="0" max="50" step="1" value="15" oninput="document.getElementById('valDeadzoneL').textContent=this.value+'%'; markSettingsChanged()">
      </div>

      <div class="slider-group">
        <div class="slider-header">
          <span>Zona Morta - Analógico Direito (R)</span>
          <span class="slider-val" id="valDeadzoneR">15%</span>
        </div>
        <input type="range" id="inputDeadzoneR" min="0" max="50" step="1" value="15" oninput="document.getElementById('valDeadzoneR').textContent=this.value+'%'; markSettingsChanged()">
      </div>

      <div class="slider-group">
        <div class="slider-header">
          <span>Vibração dos Gatilhos</span>
          <span class="slider-val" id="valTriggerStrength">2</span>
        </div>
        <input type="range" id="inputTriggerStrength" min="0" max="4" step="1" value="2" oninput="document.getElementById('valTriggerStrength').textContent=this.value; markSettingsChanged()">
      </div>

      <div class="map-item" style="margin-bottom: 10px;">
        <label>Barra de Luz DualSense</label>
        <select id="selectLightBarMode" onchange="onLightBarModeChange()">
          <option value="0">Cor do Jogo</option>
          <option value="1">Cor Personalizada</option>
          <option value="2">Desativada</option>
        </select>
        <input type="color" id="inputLightBarColor" value="#0070dc" style="margin-left: 8px; height: 32px; width: 44px; border: none; border-radius: 6px; cursor: pointer; display: none;" onchange="markSettingsChanged()">
      </div>

      <div class="toggle-row">
        <span class="toggle-label">Aviso Sonoro de Fila (Queue Chime)</span>
        <label class="switch">
          <input type="checkbox" id="checkQueueAlert" onchange="markSettingsChanged()">
          <span class="slider-sw"></span>
        </label>
      </div>

      <div class="map-item" style="margin-top: 10px;">
        <label>Decodificador de Vídeo</label>
        <select id="selectVideoDecoder" onchange="markSettingsChanged()">
          <option value="0">Hardware (libSceVideodec2 / HWAccel)</option>
          <option value="1">Software (FFmpeg CPU)</option>
        </select>
      </div>

      <div class="map-item" style="margin-top: 10px;">
        <label>Saída do Chat de Voz</label>
        <select id="selectVoiceRouting" onchange="markSettingsChanged()">
          <option value="0">Automático (Alto-falante DualSense se disponível)</option>
          <option value="1">Sempre TV / Headset Principal</option>
        </select>
      </div>

      <div class="btn-row" style="margin-top: 16px;">
        <button class="btn btn-primary" onclick="saveGeneralSettings()">Salvar Configurações</button>
      </div>
    </div>
  </div>

  <!-- TAB: STREAM (JOGO ATUAL) -->
  <div id="tab-stream" class="tab-pane" style="display:none;">
    <div class="card">
      <h2>📺 Jogo Atual & Telemetria</h2>
      <p>Métricas de transmissão em tempo real e ajustes do menu de jogo (StreamMenu).</p>

      <div class="telemetry-grid">
        <div class="tel-item"><div class="tel-val" id="telFps">--</div><div class="tel-lbl">FPS</div></div>
        <div class="tel-item"><div class="tel-val" id="telRtt">-- ms</div><div class="tel-lbl">RTT (Ping)</div></div>
        <div class="tel-item"><div class="tel-val" id="telMbps">-- Mbps</div><div class="tel-lbl">Bitrate</div></div>
        <div class="tel-item"><div class="tel-val" id="telLoss">0.0%</div><div class="tel-lbl">Perda Pkts</div></div>
        <div class="tel-item"><div class="tel-val" id="telQp">--</div><div class="tel-lbl">QP (Qualidade)</div></div>
        <div class="tel-item"><div class="tel-val" id="telDec">-- ms</div><div class="tel-lbl">Decodificação</div></div>
      </div>

      <div class="slider-group" style="margin-top: 10px;">
        <div class="slider-header">
          <span>Nitidez CAS (FidelityFX RCAS)</span>
          <span class="slider-val" id="valCas">Média</span>
        </div>
        <input type="range" id="inputCas" min="0" max="3" step="1" value="2" oninput="sendStreamParam('sharpness', parseInt(this.value)); updateCasLabel(this.value);">
      </div>

      <div class="map-item" style="margin-bottom: 10px;">
        <label>Upscaler de Imagem</label>
        <select id="selectUpscaler" onchange="sendStreamParam('upscaler', parseInt(this.value))">
          <option value="0">FSR 1.0</option>
          <option value="1">Anime4K</option>
          <option value="2">FSR 1.0 + Clean-up</option>
          <option value="3">Anime4K + Clean-up</option>
        </select>
      </div>

      <div class="map-item" style="margin-bottom: 10px;">
        <label>Suavização de Blocos (Deband)</label>
        <select id="selectDeband" onchange="sendStreamParam('deband', parseInt(this.value))">
          <option value="0">Desativada</option>
          <option value="1">Baixa</option>
          <option value="2">Alta</option>
          <option value="3">Automática (Bitrate-adaptive)</option>
        </select>
      </div>

      <div class="slider-group">
        <div class="slider-header">
          <span>Volume Alto-falante DualSense</span>
          <span class="slider-val" id="valPadSpeaker">100%</span>
        </div>
        <input type="range" id="inputPadSpeaker" min="0" max="100" step="5" value="100" oninput="document.getElementById('valPadSpeaker').textContent=this.value+'%'; sendStreamParam('padSpeakerVolume', parseInt(this.value));">
      </div>

      <div class="map-item" style="margin-bottom: 14px;">
        <label>Perfis de Gatilho Inteligente</label>
        <select id="selectSmartTriggers" onchange="sendStreamParam('triggerProfile', parseInt(this.value))">
          <option value="0">Padrão (Standard Haptics)</option>
          <option value="1">Tiro (Shooter - Hair Trigger)</option>
          <option value="2">Corrida (Racing - Progressive Resistance)</option>
        </select>
      </div>

      <div class="btn-row">
        <button class="btn btn-primary" onclick="sendStreamAction('xbox_guide')">Abrir Guia Xbox</button>
        <button class="btn btn-danger" onclick="sendStreamAction('end_session')">Encerrar Sessão</button>
      </div>
    </div>
  </div>

  <!-- TAB: SEARCH -->
  <div id="tab-search" class="tab-pane">
    <div class="card" id="searchCard" style="position: relative;">
      <div id="searchOverlay" class="search-disabled-overlay" style="display: none;">
        <div class="msg">🔒 Desativado enquanto jogando</div>
      </div>
      <h2>🔍 Teclado Remoto para Busca</h2>
      <p>Digite no celular para atualizar a pesquisa ativa na TV em tempo real sem precisar do controle.</p>
      
      <!-- Search destination tabs -->
      <div class="search-tabs">
        <button class="search-tab active" id="tabBtnGamePass" type="button" onclick="setSearchTab('gamepass')">🎮 Game Pass</button>
        <button class="search-tab" id="tabBtnLibrary" type="button" onclick="setSearchTab('library')">📚 Meus jogos</button>
      </div>

      <div class="input-group">
        <input type="text" id="searchInput" placeholder="Digite o nome de um jogo..." autocomplete="off">
        <button class="btn" onclick="clearSearch()">Limpar</button>
      </div>

      <!-- Search filters -->
      <div class="filter-section">
        <div class="filter-label">Filtros da pesquisa:</div>
        
        <!-- Game Pass filters -->
        <div id="filtersGamePass" class="filter-chips">
          <div class="chip-select" id="chipSelectPlan">
            <label for="filterPlan">👑 Plano:</label>
            <select id="filterPlan" onchange="onFilterChange()">
              <option value="0">Meu plano</option>
              <option value="1">Ultimate</option>
              <option value="2">Premium</option>
              <option value="3">Essential</option>
              <option value="4">Todos os planos</option>
            </select>
          </div>
          <div class="chip-select" id="chipSelectConsoleGP">
            <label for="filterConsoleGP">🎮 Console:</label>
            <select id="filterConsoleGP" onchange="onFilterChange()">
              <option value="0">Todos</option>
              <option value="1">Series X|S</option>
              <option value="2">Xbox One</option>
              <option value="3">Xbox 360</option>
            </select>
          </div>
          <button class="chip" id="chipMouseGP" type="button" onclick="toggleChip('chipMouseGP')">
            ⌨️🖱️ Teclado e mouse
          </button>
          <div class="chip-select" id="chipSelectModeGP">
            <label for="filterModeGP">👤 Modo:</label>
            <select id="filterModeGP" onchange="onFilterChange()">
              <option value="0">Todos</option>
              <option value="1">Um jogador</option>
              <option value="2">Multijogador online</option>
              <option value="3">Cooperativo online</option>
              <option value="4">Multijogador local</option>
            </select>
          </div>
          <div class="chip-select" id="chipSelectLangGP">
            <label for="filterLangGP">🌐 Idioma:</label>
            <select id="filterLangGP" onchange="onFilterChange()">
              <option value="0">Qualquer</option>
              <option value="1">Legendas</option>
              <option value="2">Dublado</option>
            </select>
          </div>
        </div>

        <!-- Meus Jogos (Library) filters -->
        <div id="filtersLibrary" class="filter-chips" style="display:none;">
          <button class="chip" id="chipWishlist" type="button" onclick="toggleChip('chipWishlist')">
            💖 Lista de desejos
          </button>
          <button class="chip" id="chipFree" type="button" onclick="toggleChip('chipFree')">
            🎁 Grátis
          </button>
          <button class="chip" id="chipCheapest" type="button" onclick="toggleChip('chipCheapest')">
            🏷️ Menor preço
          </button>
          <div class="chip-select" id="chipSelectConsoleLib">
            <label for="filterConsoleLib">🎮 Console:</label>
            <select id="filterConsoleLib" onchange="onFilterChange()">
              <option value="0">Todos</option>
              <option value="1">Series X|S</option>
              <option value="2">Xbox One</option>
              <option value="3">Xbox 360</option>
            </select>
          </div>
          <button class="chip" id="chipMouseLib" type="button" onclick="toggleChip('chipMouseLib')">
            ⌨️🖱️ Teclado e mouse
          </button>
          <div class="chip-select" id="chipSelectModeLib">
            <label for="filterModeLib">👤 Modo:</label>
            <select id="filterModeLib" onchange="onFilterChange()">
              <option value="0">Todos</option>
              <option value="1">Um jogador</option>
              <option value="2">Multijogador online</option>
              <option value="3">Cooperativo online</option>
              <option value="4">Multijogador local</option>
            </select>
          </div>
          <div class="chip-select" id="chipSelectLangLib">
            <label for="filterLangLib">🌐 Idioma:</label>
            <select id="filterLangLib" onchange="onFilterChange()">
              <option value="0">Qualquer</option>
              <option value="1">Legendas</option>
              <option value="2">Dublado</option>
            </select>
          </div>
        </div>
      </div>

      <div class="status-pill">
        <div class="status-dot"></div>
        <span id="searchStatus">Pronto para pesquisar</span>
      </div>
    </div>
  </div>

  <!-- TAB: REMAP -->
  <div id="tab-remap" class="tab-pane" style="display:none;">
    <div class="subnav">
      <button class="btn active" id="btnSubPad" onclick="switchRemapSub('pad')">🎮 DualSense</button>
      <button class="btn" id="btnSubKey" onclick="switchRemapSub('key')">⌨️ Teclado</button>
    </div>

    <div id="subRemapPad">
      <div class="card">
        <h2>🎮 Remapeamento de Botões (DualSense)</h2>
        <p>Ajuste os botões do controle para o jogo na nuvem. Suas alterações são sincronizadas e salvas automaticamente.</p>
        <div class="grid-map" id="padGrid"></div>
        <div class="btn-row">
          <button class="btn btn-primary" onclick="saveSettings()">Salvar no Console</button>
          <button class="btn" onclick="resetPadMap()">Restaurar Controle Padrão</button>
        </div>
      </div>
    </div>

    <div id="subRemapKey" style="display:none;">
      <div class="card">
        <h2>⌨️ Remapeamento de Teclado (WASD / Teclas)</h2>
        <p>Mapeie teclas e botões do mouse para os comandos do Xbox na nuvem. Cada ação possui tecla Primária e Secundária.</p>
        <div class="grid-map" id="keyGrid"></div>
        <div class="btn-row">
          <button class="btn btn-primary" onclick="saveSettings()">Salvar no Console</button>
          <button class="btn" onclick="resetKeyMap()">Restaurar Teclado Padrão</button>
        </div>
      </div>
    </div>
  </div>

  <!-- TAB: GAMEPAD (CONTROLE VIRTUAL) -->
  <div id="tab-gamepad" class="tab-pane" style="display:none;">
    <div class="card">
      <h2>🎮 Controle Virtual (Smartphone)</h2>
      <p>Use o celular como controle tátil responsivo. Alterne entre Jogador 1 (principal) e Jogador 2 (co-op local)!</p>

      <div class="player-select-row">
        <button type="button" class="player-btn active" id="btnPlayer1" onclick="setVirtualPlayer(1)">
          👤 Jogador 1 (Principal)
        </button>
        <button type="button" class="player-btn" id="btnPlayer2" onclick="setVirtualPlayer(2)">
          👥 Jogador 2 (Co-op Local)
        </button>
      </div>

      <div class="gamepad-container" id="gamepadArea">
        <!-- Top row: Bumpers / Triggers / Center -->
        <div class="pad-top-row">
          <div class="pad-bumper-group">
            <button type="button" class="pad-btn pad-btn-sm" id="btn_lt" data-btn="triggerL2">LT</button>
            <button type="button" class="pad-btn pad-btn-sm" id="btn_lb" data-btn="btnL1">LB</button>
            <button type="button" class="pad-btn pad-btn-sm" id="btn_ls" data-btn="btnL3">LS (L3)</button>
          </div>
          <div class="pad-center-group">
            <button type="button" class="pad-btn pad-btn-center" id="btn_view" data-btn="btnView">VIEW</button>
            <button type="button" class="pad-btn pad-btn-nexus" id="btn_nexus" data-btn="btnNexus">X</button>
            <button type="button" class="pad-btn pad-btn-center" id="btn_menu" data-btn="btnMenu">MENU</button>
          </div>
          <div class="pad-bumper-group">
            <button type="button" class="pad-btn pad-btn-sm" id="btn_rt" data-btn="triggerR2">RT</button>
            <button type="button" class="pad-btn pad-btn-sm" id="btn_rb" data-btn="btnR1">RB</button>
            <button type="button" class="pad-btn pad-btn-sm" id="btn_rs" data-btn="btnR3">RS (R3)</button>
          </div>
        </div>

        <!-- Main row: Left (D-pad & Left Stick) and Right (ABXY & Right Stick) -->
        <div class="pad-main-row">
          <div class="pad-side">
            <!-- Left Stick -->
            <div class="stick-zone" id="leftStickZone">
              <div class="stick-head" id="leftStickHead">L3</div>
            </div>
            <!-- D-Pad -->
            <div class="dpad-cross">
              <button type="button" class="pad-btn dpad-btn dpad-up" data-btn="dpadUp">▲</button>
              <button type="button" class="pad-btn dpad-btn dpad-left" data-btn="dpadLeft">◀</button>
              <div class="dpad-center"></div>
              <button type="button" class="pad-btn dpad-btn dpad-right" data-btn="dpadRight">▶</button>
              <button type="button" class="pad-btn dpad-btn dpad-down" data-btn="dpadDown">▼</button>
            </div>
          </div>

          <div class="pad-side">
            <!-- ABXY Buttons -->
            <div class="abxy-group">
              <button type="button" class="pad-btn abxy-btn btn-y" data-btn="btnY">Y</button>
              <button type="button" class="pad-btn abxy-btn btn-x" data-btn="btnX">X</button>
              <button type="button" class="pad-btn abxy-btn btn-b" data-btn="btnB">B</button>
              <button type="button" class="pad-btn abxy-btn btn-a" data-btn="btnA">A</button>
            </div>
            <!-- Right Stick -->
            <div class="stick-zone" id="rightStickZone">
              <div class="stick-head" id="rightStickHead">R3</div>
            </div>
          </div>
        </div>
      </div>

      <div class="status-pill" style="margin-top: 14px;">
        <div class="status-dot"></div>
        <span id="gamepadStatus">Controle ativo para Jogador 1 (Principal)</span>
      </div>
    </div>
  </div>

  <!-- TAB: LOGS -->
  <div id="tab-logs" class="tab-pane" style="display:none;">
    <div class="card">
      <h2>📋 Download de Logs</h2>
      <p>Baixe o arquivo <code>xcloud.log</code> sanitizado (sem tokens ou dados privados) para anexar em relatórios de erros no GitHub.</p>
      <div class="btn-row" style="margin-top: 0; margin-bottom: 14px;">
        <a href="/api/log" download="xcloud.log" class="btn btn-primary">⬇️ Baixar xcloud.log</a>
        <button class="btn" onclick="loadLogPreview()">Visualizar Últimas Linhas</button>
      </div>
      <pre id="logPreview">Clique em "Visualizar Últimas Linhas" para carregar o log...</pre>
    </div>
  </div>
</div>

<div id="toast" class="toast">Alterações salvas com sucesso!</div>

<script>
const padFunctions = [
  "A (Sul / Cruz)", "B (Leste / Círculo)", "X (Oeste / Quadrado)", "Y (Norte / Triângulo)",
  "LB (L1)", "RB (R1)", "LT (L2)", "RT (R2)", "LS (L3)", "RS (R3)",
  "View (Touchpad / Share)", "Menu (Options)", "D-pad Cima", "D-pad Baixo", "D-pad Esquerda", "D-pad Direita"
];
const padSources = [
  {id: 0, name: "Cruz (South)"}, {id: 1, name: "Círculo (East)"}, {id: 2, name: "Quadrado (West)"}, {id: 3, name: "Triângulo (North)"},
  {id: 4, name: "L1 (LB)"}, {id: 5, name: "R1 (RB)"}, {id: 6, name: "L2 (LT)"}, {id: 7, name: "R2 (RT)"},
  {id: 8, name: "L3 (LS)"}, {id: 9, name: "R3 (RS)"}, {id: 10, name: "Touchpad (View)"}, {id: 11, name: "Options (Menu)"},
  {id: 12, name: "D-pad Cima"}, {id: 13, name: "D-pad Baixo"}, {id: 14, name: "D-pad Esquerda"}, {id: 15, name: "D-pad Direita"}
];

const keyActionNames = [
  "Analógico Esq. Cima", "Analógico Esq. Baixo", "Analógico Esq. Esquerda", "Analógico Esq. Direita",
  "D-pad Cima", "D-pad Baixo", "D-pad Esquerda", "D-pad Direita",
  "A (Cruz)", "B (Círculo)", "X (Quadrado)", "Y (Triângulo)",
  "LB (L1)", "RB (R1)", "LT (L2)", "RT (R2)", "LS (L3)", "RS (R3)",
  "View (Share / Touch)", "Menu (Options)"
];

const keyList = [
  {id: 0, name: "(Nenhuma)"},
  {id: 87, name: "W"}, {id: 65, name: "A"}, {id: 83, name: "S"}, {id: 68, name: "D"},
  {id: 81, name: "Q"}, {id: 69, name: "E"}, {id: 82, name: "R"}, {id: 70, name: "F"},
  {id: 67, name: "C"}, {id: 86, name: "V"}, {id: 90, name: "Z"}, {id: 88, name: "X"},
  {id: 66, name: "B"}, {id: 71, name: "G"}, {id: 84, name: "T"}, {id: 89, name: "Y"},
  {id: 72, name: "H"}, {id: 74, name: "J"}, {id: 75, name: "K"}, {id: 76, name: "L"},
  {id: 77, name: "M"}, {id: 78, name: "N"}, {id: 79, name: "O"}, {id: 80, name: "P"},
  {id: 85, name: "U"}, {id: 73, name: "I"},
  {id: 48, name: "0"}, {id: 49, name: "1"}, {id: 50, name: "2"}, {id: 51, name: "3"},
  {id: 52, name: "4"}, {id: 53, name: "5"}, {id: 54, name: "6"}, {id: 55, name: "7"},
  {id: 56, name: "8"}, {id: 57, name: "9"},
  {id: 32, name: "Espaço (Space)"},
  {id: 16, name: "Shift"}, {id: 17, name: "Ctrl"}, {id: 18, name: "Alt"},
  {id: 9, name: "Tab"}, {id: 27, name: "Esc"}, {id: 13, name: "Enter"}, {id: 8, name: "Backspace"},
  {id: 38, name: "Seta Cima"}, {id: 40, name: "Seta Baixo"},
  {id: 37, name: "Seta Esquerda"}, {id: 39, name: "Seta Direita"},
  {id: 241, name: "Mouse L (Esq)"}, {id: 242, name: "Mouse R (Dir)"},
  {id: 243, name: "Mouse M (Meio)"}, {id: 244, name: "Mouse 4"}, {id: 245, name: "Mouse 5"}
];

const defaultKeyMap = [
  87, 0,  83, 0,  65, 0,  68, 0,
  38, 0,  40, 0,  37, 0,  39, 0,
  32, 0,  67, 0,  82, 0,  69, 0,
  81, 0,  70, 0, 242, 0, 241, 0,
  16, 0,  86, 243, 9, 0,  27, 0
];

let currentSettings = {
  padMap: [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15],
  keyMap: [...defaultKeyMap]
};

let currentSearchTab = 'gamepass';
let currentFilters = {
  plan: 0,
  consoleGP: 0,
  consoleLib: 0,
  modeGP: 0,
  modeLib: 0,
  langGP: 0,
  langLib: 0,
  mouseGP: false,
  mouseLib: false,
  wishlist: false,
  free: false,
  cheapest: false
};

function switchTab(name) {
  document.querySelectorAll('.tab-pane').forEach(el => el.style.display = 'none');
  document.querySelectorAll('.nav-tab').forEach(el => el.classList.remove('active'));
  const pane = document.getElementById('tab-' + name);
  if (pane) pane.style.display = 'block';
  const tabBtn = document.getElementById('navTab-' + name);
  if (tabBtn) tabBtn.classList.add('active');
  if (name === 'search') pollStreamingState();
  if (name === 'gamepad') sendPadState();
  if (name === 'stream') {
    loadStreamMenuState();
    pollStats();
  }
}

function switchRemapSub(mode) {
  document.getElementById('subRemapPad').style.display = mode === 'pad' ? 'block' : 'none';
  document.getElementById('subRemapKey').style.display = mode === 'key' ? 'block' : 'none';
  document.getElementById('btnSubPad').classList.toggle('active', mode === 'pad');
  document.getElementById('btnSubKey').classList.toggle('active', mode === 'key');
}

function showToast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 2500);
}

const searchInput = document.getElementById('searchInput');
let searchDebounce = null;

function applySearchTabUI(tab) {
  currentSearchTab = tab;
  document.getElementById('tabBtnGamePass').classList.toggle('active', tab === 'gamepass');
  document.getElementById('tabBtnLibrary').classList.toggle('active', tab === 'library');
  document.getElementById('filtersGamePass').style.display = tab === 'gamepass' ? 'flex' : 'none';
  document.getElementById('filtersLibrary').style.display = tab === 'library' ? 'flex' : 'none';
}

function setSearchTab(tab) {
  applySearchTabUI(tab);
  sendSearch(true);
}

function toggleChip(id) {
  const el = document.getElementById(id);
  el.classList.toggle('active');
  const active = el.classList.contains('active');
  if (id === 'chipWishlist') currentFilters.wishlist = active;
  if (id === 'chipFree') currentFilters.free = active;
  if (id === 'chipCheapest') currentFilters.cheapest = active;
  if (id === 'chipMouseGP') currentFilters.mouseGP = active;
  if (id === 'chipMouseLib') currentFilters.mouseLib = active;
  sendSearch(true);
}

function onFilterChange() {
  currentFilters.plan = parseInt(document.getElementById('filterPlan').value, 10);
  currentFilters.consoleGP = parseInt(document.getElementById('filterConsoleGP').value, 10);
  currentFilters.consoleLib = parseInt(document.getElementById('filterConsoleLib').value, 10);
  currentFilters.modeGP = parseInt(document.getElementById('filterModeGP').value, 10);
  currentFilters.modeLib = parseInt(document.getElementById('filterModeLib').value, 10);
  currentFilters.langGP = parseInt(document.getElementById('filterLangGP').value, 10);
  currentFilters.langLib = parseInt(document.getElementById('filterLangLib').value, 10);

  document.getElementById('chipSelectPlan').classList.toggle('active', currentFilters.plan > 0);
  document.getElementById('chipSelectConsoleGP').classList.toggle('active', currentFilters.consoleGP > 0);
  document.getElementById('chipSelectConsoleLib').classList.toggle('active', currentFilters.consoleLib > 0);
  document.getElementById('chipSelectModeGP').classList.toggle('active', currentFilters.modeGP > 0);
  document.getElementById('chipSelectModeLib').classList.toggle('active', currentFilters.modeLib > 0);
  document.getElementById('chipSelectLangGP').classList.toggle('active', currentFilters.langGP > 0);
  document.getElementById('chipSelectLangLib').classList.toggle('active', currentFilters.langLib > 0);
  sendSearch(true);
}

function updateSearchStatusText() {
  const q = searchInput.value;
  const isLib = currentSearchTab === 'library';
  let status = isLib ? 'Meus jogos' : 'Game Pass';
  if (q) status += ' • "' + q + '"';
  const filterNames = [];
  if (isLib) {
    if (currentFilters.wishlist) filterNames.push('Lista de desejos');
    if (currentFilters.free) filterNames.push('Grátis');
    if (currentFilters.cheapest) filterNames.push('Menor preço');
    if (currentFilters.mouseLib) filterNames.push('Teclado e mouse');
    if (currentFilters.consoleLib > 0) {
      const cNames = ['Todos', 'Series X|S', 'Xbox One', 'Xbox 360'];
      filterNames.push(cNames[currentFilters.consoleLib]);
    }
    if (currentFilters.modeLib > 0) {
      const mNames = ['Todos', 'Um jogador', 'Multijogador online', 'Cooperativo online', 'Multijogador local'];
      filterNames.push(mNames[currentFilters.modeLib]);
    }
    if (currentFilters.langLib > 0) {
      const lNames = ['Qualquer', 'Legendas', 'Dublado'];
      filterNames.push(lNames[currentFilters.langLib]);
    }
  } else {
    if (currentFilters.plan > 0) {
      const pNames = ['Meu plano', 'Ultimate', 'Premium', 'Essential', 'Todos os planos'];
      filterNames.push(pNames[currentFilters.plan]);
    }
    if (currentFilters.mouseGP) filterNames.push('Teclado e mouse');
    if (currentFilters.consoleGP > 0) {
      const cNames = ['Todos', 'Series X|S', 'Xbox One', 'Xbox 360'];
      filterNames.push(cNames[currentFilters.consoleGP]);
    }
    if (currentFilters.modeGP > 0) {
      const mNames = ['Todos', 'Um jogador', 'Multijogador online', 'Cooperativo online', 'Multijogador local'];
      filterNames.push(mNames[currentFilters.modeGP]);
    }
    if (currentFilters.langGP > 0) {
      const lNames = ['Qualquer', 'Legendas', 'Dublado'];
      filterNames.push(lNames[currentFilters.langGP]);
    }
  }
  if (filterNames.length > 0) {
    status += ' [' + filterNames.join(', ') + ']';
  }
  document.getElementById('searchStatus').textContent = status;
}

function sendSearch(immediate = false) {
  clearTimeout(searchDebounce);
  const doFetch = () => {
    const q = searchInput.value;
    const isLib = currentSearchTab === 'library';
    const activeFilters = {
      plan: isLib ? 0 : currentFilters.plan,
      console: isLib ? currentFilters.consoleLib : currentFilters.consoleGP,
      mode: isLib ? currentFilters.modeLib : currentFilters.modeGP,
      language: isLib ? currentFilters.langLib : currentFilters.langGP,
      mouseKeyboard: isLib ? currentFilters.mouseLib : currentFilters.mouseGP,
      wishlist: isLib ? currentFilters.wishlist : false,
      free: isLib ? currentFilters.free : false,
      cheapest: isLib ? currentFilters.cheapest : false
    };

    updateSearchStatusText();

    fetch('/api/search', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({
        query: q,
        tab: currentSearchTab,
        filters: activeFilters
      })
    }).catch(e => console.error(e));
  };

  if (immediate) {
    doFetch();
  } else {
    searchDebounce = setTimeout(doFetch, 120);
  }
}

searchInput.addEventListener('input', () => {
  sendSearch(false);
});

function clearSearch() {
  searchInput.value = '';
  sendSearch(true);
}

function renderPadMap() {
  const grid = document.getElementById('padGrid');
  grid.innerHTML = '';
  const map = currentSettings.padMap || [];
  padFunctions.forEach((fnName, idx) => {
    const item = document.createElement('div');
    item.className = 'map-item';
    const label = document.createElement('label');
    label.textContent = fnName;
    const select = document.createElement('select');
    select.id = 'pad_fn_' + idx;
    padSources.forEach(s => {
      const opt = document.createElement('option');
      opt.value = s.id;
      opt.textContent = s.name;
      if (map[idx] === s.id) opt.selected = true;
      select.appendChild(opt);
    });
    item.appendChild(label);
    item.appendChild(select);
    grid.appendChild(item);
  });
}

function renderKeyMap() {
  const grid = document.getElementById('keyGrid');
  grid.innerHTML = '';
  const kmap = (currentSettings.keyMap && currentSettings.keyMap.length === 40) ? currentSettings.keyMap : defaultKeyMap;
  keyActionNames.forEach((actName, idx) => {
    const item = document.createElement('div');
    item.className = 'map-item';
    const label = document.createElement('label');
    label.textContent = actName;
    const selectsWrap = document.createElement('div');
    selectsWrap.className = 'selects-wrap';
    for (let slot = 0; slot < 2; slot++) {
      const select = document.createElement('select');
      select.id = 'key_act_' + idx + '_' + slot;
      const code = kmap[idx * 2 + slot];
      let hasMatch = false;
      keyList.forEach(k => {
        const opt = document.createElement('option');
        opt.value = k.id;
        opt.textContent = k.name;
        if (code === k.id) { opt.selected = true; hasMatch = true; }
        select.appendChild(opt);
      });
      if (!hasMatch && code > 0) {
        const opt = document.createElement('option');
        opt.value = code;
        opt.textContent = "Key #" + code;
        opt.selected = true;
        select.appendChild(opt);
      }
      selectsWrap.appendChild(select);
    }
    item.appendChild(label);
    item.appendChild(selectsWrap);
    grid.appendChild(item);
  });
}

function resetPadMap() {
  currentSettings.padMap = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15];
  renderPadMap();
  saveSettings();
}

function resetKeyMap() {
  currentSettings.keyMap = [...defaultKeyMap];
  renderKeyMap();
  saveSettings();
}

function saveSettings() {
  const padMap = [];
  for (let i = 0; i < 16; i++) {
    const sel = document.getElementById('pad_fn_' + i);
    padMap.push(sel ? parseInt(sel.value, 10) : i);
  }
  const keyMap = [];
  for (let i = 0; i < 20; i++) {
    const s0 = document.getElementById('key_act_' + i + '_0');
    const s1 = document.getElementById('key_act_' + i + '_1');
    keyMap.push(s0 ? parseInt(s0.value, 10) : 0);
    keyMap.push(s1 ? parseInt(s1.value, 10) : 0);
  }
  fetch('/api/settings', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ padMap: padMap, keyMap: keyMap })
  }).then(r => r.json()).then(res => {
    if (res.ok) {
      showToast('Configurações salvas no console!');
    }
  }).catch(e => showToast('Erro ao salvar configurações'));
}

function onBitrateInput(val) {
  const v = parseInt(val, 10);
  const el = document.getElementById('valBitrate');
  if (el) el.textContent = (v === 0) ? 'Auto' : v + ' Mbps';
}

function onLightBarModeChange() {
  const sel = document.getElementById('selectLightBarMode');
  const colorInput = document.getElementById('inputLightBarColor');
  if (sel && colorInput) {
    colorInput.style.display = (sel.value === '1') ? 'inline-block' : 'none';
  }
}

function markSettingsChanged() {
}

function saveGeneralSettings() {
  const payload = {
    maxBitrateMbps: parseInt(document.getElementById('inputBitrate').value, 10),
    highRefresh: document.getElementById('check120Hz').checked,
    deadzoneLeft: parseInt(document.getElementById('inputDeadzoneL').value, 10),
    deadzoneRight: parseInt(document.getElementById('inputDeadzoneR').value, 10),
    triggerStrength: parseInt(document.getElementById('inputTriggerStrength').value, 10),
    lightBarMode: parseInt(document.getElementById('selectLightBarMode').value, 10),
    lightBarColour: document.getElementById('inputLightBarColor').value,
    queueReadyAlert: document.getElementById('checkQueueAlert').checked,
    videoDecoder: parseInt(document.getElementById('selectVideoDecoder').value, 10),
    voiceRouting: parseInt(document.getElementById('selectVoiceRouting').value, 10)
  };
  fetch('/api/settings', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(payload)
  }).then(r => r.json()).then(res => {
    if (res.ok) {
      showToast('Configurações salvas no console!');
    } else {
      showToast('Erro ao salvar configurações');
    }
  }).catch(() => showToast('Erro ao salvar configurações'));
}

function updateCasLabel(val) {
  const labels = ['Desativada', 'Baixa', 'Média', 'Alta'];
  const el = document.getElementById('valCas');
  if (el) el.textContent = labels[val] !== undefined ? labels[val] : 'Média';
}

function sendStreamParam(param, val) {
  const body = {};
  body[param] = val;
  fetch('/api/stream', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(body)
  }).catch(() => {});
}

function sendStreamAction(action) {
  fetch('/api/stream', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ action: action })
  }).then(r => r.json()).then(res => {
    if (action === 'xbox_guide') showToast('Guia Xbox acionado');
    if (action === 'end_session') showToast('Sessão encerrada');
  }).catch(() => {});
}

function loadStreamMenuState() {
  fetch('/api/stream').then(r => r.json()).then(s => {
    if (s.sharpness !== undefined) {
      const el = document.getElementById('inputCas');
      if (el) { el.value = s.sharpness; updateCasLabel(s.sharpness); }
    }
    if (s.upscaler !== undefined) {
      const el = document.getElementById('selectUpscaler');
      if (el) el.value = s.upscaler;
    }
    if (s.deband !== undefined) {
      const el = document.getElementById('selectDeband');
      if (el) el.value = s.deband;
    }
    if (s.padSpeakerVolume !== undefined) {
      const el = document.getElementById('inputPadSpeaker');
      if (el) el.value = s.padSpeakerVolume;
      const vlbl = document.getElementById('valPadSpeaker');
      if (vlbl) vlbl.textContent = s.padSpeakerVolume + '%';
    }
    if (s.triggerProfile !== undefined) {
      const el = document.getElementById('selectSmartTriggers');
      if (el) el.value = s.triggerProfile;
    }
  }).catch(() => {});
}

function renderTelemetry(data) {
  if (!data) return;
  if (data.fps !== undefined) document.getElementById('telFps').textContent = data.fps;
  if (data.rttMs !== undefined) document.getElementById('telRtt').textContent = data.rttMs + ' ms';
  if (data.mbps !== undefined) document.getElementById('telMbps').textContent = (typeof data.mbps === 'number' ? data.mbps.toFixed(1) : data.mbps) + ' Mbps';
  if (data.lossPct !== undefined) document.getElementById('telLoss').textContent = (typeof data.lossPct === 'number' ? data.lossPct.toFixed(1) : data.lossPct) + '%';
  if (data.qp !== undefined) document.getElementById('telQp').textContent = data.qp;
  if (data.decodeMs !== undefined) document.getElementById('telDec').textContent = (typeof data.decodeMs === 'number' ? data.decodeMs.toFixed(1) : data.decodeMs) + ' ms';
}

let ws = null;
function initWs() {
  try {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(proto + '//' + location.host + '/ws');
    ws.onmessage = (ev) => {
      try {
        const msg = JSON.parse(ev.data);
        if (msg.type === 'telemetry' && msg.stats) {
          renderTelemetry(msg.stats);
        } else if (msg.fps !== undefined) {
          renderTelemetry(msg);
        }
      } catch (_) {}
    };
    ws.onclose = () => {
      ws = null;
      setTimeout(initWs, 2000);
    };
    ws.onerror = () => {
      try { ws.close(); } catch(_) {}
    };
  } catch (_) {
    ws = null;
  }
}
initWs();

function pollStats() {
  if (ws && ws.readyState === WebSocket.OPEN) return;
  const streamTab = document.getElementById('tab-stream');
  if (!streamTab || streamTab.style.display === 'none') return;
  fetch('/api/stats').then(r => r.json()).then(renderTelemetry).catch(() => {});
}
setInterval(pollStats, 1000);

function loadLogPreview() {
  const p = document.getElementById('logPreview');
  p.textContent = 'Carregando log...';
  fetch('/api/log').then(r => r.text()).then(text => {
    const lines = text.split('\n');
    p.textContent = lines.slice(-50).join('\n') || 'Log vazio.';
  }).catch(e => {
    p.textContent = 'Erro ao carregar log: ' + e;
  });
}

fetch('/api/settings').then(r => r.json()).then(s => {
  currentSettings = s;
  if (s.query) searchInput.value = s.query;
  if (s.tab) {
    currentSearchTab = s.tab;
  }
  if (s.filters) {
    if (s.filters.wishlist) {
      currentFilters.wishlist = true;
      const el = document.getElementById('chipWishlist');
      if (el) el.classList.add('active');
    }
    if (s.filters.free) {
      currentFilters.free = true;
      const el = document.getElementById('chipFree');
      if (el) el.classList.add('active');
    }
    if (s.filters.cheapest) {
      currentFilters.cheapest = true;
      const el = document.getElementById('chipCheapest');
      if (el) el.classList.add('active');
    }
    if (s.filters.mouseKeyboard) {
      currentFilters.mouseGP = true;
      currentFilters.mouseLib = true;
      const elGP = document.getElementById('chipMouseGP');
      if (elGP) elGP.classList.add('active');
      const elLib = document.getElementById('chipMouseLib');
      if (elLib) elLib.classList.add('active');
    }
    if (s.filters.plan !== undefined && s.filters.plan > 0) {
      currentFilters.plan = s.filters.plan;
      const sel = document.getElementById('filterPlan');
      if (sel) sel.value = s.filters.plan;
      const wrap = document.getElementById('chipSelectPlan');
      if (wrap) wrap.classList.add('active');
    }
    if (s.filters.console !== undefined && s.filters.console > 0) {
      currentFilters.consoleGP = s.filters.console;
      currentFilters.consoleLib = s.filters.console;
      const selGP = document.getElementById('filterConsoleGP');
      if (selGP) selGP.value = s.filters.console;
      const selLib = document.getElementById('filterConsoleLib');
      if (selLib) selLib.value = s.filters.console;
      const wrapGP = document.getElementById('chipSelectConsoleGP');
      if (wrapGP) wrapGP.classList.add('active');
      const wrapLib = document.getElementById('chipSelectConsoleLib');
      if (wrapLib) wrapLib.classList.add('active');
    }
    if (s.filters.mode !== undefined && s.filters.mode > 0) {
      currentFilters.modeGP = s.filters.mode;
      currentFilters.modeLib = s.filters.mode;
      const selGP = document.getElementById('filterModeGP');
      if (selGP) selGP.value = s.filters.mode;
      const selLib = document.getElementById('filterModeLib');
      if (selLib) selLib.value = s.filters.mode;
      const wrapGP = document.getElementById('chipSelectModeGP');
      if (wrapGP) wrapGP.classList.add('active');
      const wrapLib = document.getElementById('chipSelectModeLib');
      if (wrapLib) wrapLib.classList.add('active');
    }
    if (s.filters.language !== undefined && s.filters.language > 0) {
      currentFilters.langGP = s.filters.language;
      currentFilters.langLib = s.filters.language;
      const selGP = document.getElementById('filterLangGP');
      if (selGP) selGP.value = s.filters.language;
      const selLib = document.getElementById('filterLangLib');
      if (selLib) selLib.value = s.filters.language;
      const wrapGP = document.getElementById('chipSelectLangGP');
      if (wrapGP) wrapGP.classList.add('active');
      const wrapLib = document.getElementById('chipSelectLangLib');
      if (wrapLib) wrapLib.classList.add('active');
    }
  }
  if (s.maxBitrateMbps !== undefined) {
    const el = document.getElementById('inputBitrate');
    if (el) { el.value = s.maxBitrateMbps; onBitrateInput(s.maxBitrateMbps); }
  }
  if (s.highRefresh !== undefined) {
    const el = document.getElementById('check120Hz');
    if (el) el.checked = !!s.highRefresh;
  }
  if (s.deadzoneLeft !== undefined) {
    const el = document.getElementById('inputDeadzoneL');
    if (el) { el.value = s.deadzoneLeft; document.getElementById('valDeadzoneL').textContent = s.deadzoneLeft + '%'; }
  }
  if (s.deadzoneRight !== undefined) {
    const el = document.getElementById('inputDeadzoneR');
    if (el) { el.value = s.deadzoneRight; document.getElementById('valDeadzoneR').textContent = s.deadzoneRight + '%'; }
  }
  if (s.triggerStrength !== undefined) {
    const el = document.getElementById('inputTriggerStrength');
    if (el) { el.value = s.triggerStrength; document.getElementById('valTriggerStrength').textContent = s.triggerStrength; }
  }
  if (s.lightBarMode !== undefined) {
    const el = document.getElementById('selectLightBarMode');
    if (el) el.value = s.lightBarMode;
    onLightBarModeChange();
  }
  if (s.lightBarColour) {
    const el = document.getElementById('inputLightBarColor');
    if (el) el.value = s.lightBarColour;
  }
  if (s.queueReadyAlert !== undefined) {
    const el = document.getElementById('checkQueueAlert');
    if (el) el.checked = !!s.queueReadyAlert;
  }
  if (s.videoDecoder !== undefined) {
    const el = document.getElementById('selectVideoDecoder');
    if (el) el.value = s.videoDecoder;
  }
  if (s.voiceRouting !== undefined) {
    const el = document.getElementById('selectVoiceRouting');
    if (el) el.value = s.voiceRouting;
  }
  applySearchTabUI(currentSearchTab);
  updateSearchStatusText();
  renderPadMap();
  renderKeyMap();
}).catch(e => {
  renderPadMap();
  renderKeyMap();
});

// --- Virtual Gamepad & Streaming Polling ---
let activePlayer = 1;
const padState = {
  btnA: false, btnB: false, btnX: false, btnY: false,
  dpadUp: false, dpadDown: false, dpadLeft: false, dpadRight: false,
  btnL1: false, btnR1: false, btnL3: false, btnR3: false,
  btnView: false, btnMenu: false, btnNexus: false,
  triggerL2: 0.0, triggerR2: 0.0,
  leftStickX: 0.0, leftStickY: 0.0,
  rightStickX: 0.0, rightStickY: 0.0
};
let padSendScheduled = false;

function setVirtualPlayer(p) {
  activePlayer = p;
  document.getElementById('btnPlayer1').classList.toggle('active', p === 1);
  document.getElementById('btnPlayer2').classList.toggle('active', p === 2);
  const statusEl = document.getElementById('gamepadStatus');
  if (statusEl) {
    statusEl.textContent = 'Controle ativo para Jogador ' + p + (p === 2 ? ' (Co-op Local)' : ' (Principal)');
  }
  sendPadState();
}

function sendPadState() {
  const payload = { player: activePlayer, ...padState };
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload));
  } else {
    fetch('/api/gamepad', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    }).catch(() => {});
  }
}

function schedulePadSend() {
  if (padSendScheduled) return;
  padSendScheduled = true;
  requestAnimationFrame(() => {
    padSendScheduled = false;
    sendPadState();
  });
}

function bindPadButtons() {
  document.querySelectorAll('[data-btn]').forEach(el => {
    const key = el.getAttribute('data-btn');
    const isTrigger = key === 'triggerL2' || key === 'triggerR2';

    const onDown = (e) => {
      e.preventDefault();
      if (isTrigger) padState[key] = 1.0;
      else padState[key] = true;
      el.classList.add('pressed');
      if (navigator.vibrate) {
        try { navigator.vibrate(14); } catch (_) {}
      }
      sendPadState();
    };

    const onUp = (e) => {
      e.preventDefault();
      if (isTrigger) padState[key] = 0.0;
      else padState[key] = false;
      el.classList.remove('pressed');
      sendPadState();
    };

    el.addEventListener('touchstart', onDown, { passive: false });
    el.addEventListener('touchend', onUp, { passive: false });
    el.addEventListener('touchcancel', onUp, { passive: false });
    el.addEventListener('mousedown', onDown);
    el.addEventListener('mouseup', onUp);
    el.addEventListener('mouseleave', onUp);
  });
}

function setupStick(zoneId, headId, stickKeyX, stickKeyY, l3r3Key) {
  const zone = document.getElementById(zoneId);
  const head = document.getElementById(headId);
  if (!zone || !head) return;

  let activeTouchId = null;
  const maxR = 34;

  let touchStartMs = 0;
  let startX = 0, startY = 0;

  function updateStick(clientX, clientY) {
    const rect = zone.getBoundingClientRect();
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    let dx = clientX - cx;
    let dy = clientY - cy;
    const dist = Math.hypot(dx, dy);
    if (dist > maxR) {
      dx = (dx / dist) * maxR;
      dy = (dy / dist) * maxR;
    }
    head.style.transform = `translate(${dx}px, ${dy}px)`;
    padState[stickKeyX] = Math.round((dx / maxR) * 100) / 100;
    padState[stickKeyY] = Math.round((dy / maxR) * 100) / 100;
    schedulePadSend();
  }

  function resetStick() {
    head.style.transform = 'translate(0px, 0px)';
    padState[stickKeyX] = 0.0;
    padState[stickKeyY] = 0.0;
    activeTouchId = null;
    sendPadState();
  }

  zone.addEventListener('touchstart', (e) => {
    e.preventDefault();
    if (activeTouchId === null && e.changedTouches.length > 0) {
      activeTouchId = e.changedTouches[0].identifier;
      touchStartMs = Date.now();
      startX = e.changedTouches[0].clientX;
      startY = e.changedTouches[0].clientY;
      updateStick(e.changedTouches[0].clientX, e.changedTouches[0].clientY);
    }
  }, { passive: false });

  zone.addEventListener('touchmove', (e) => {
    e.preventDefault();
    for (let i = 0; i < e.changedTouches.length; i++) {
      if (e.changedTouches[i].identifier === activeTouchId) {
        updateStick(e.changedTouches[i].clientX, e.changedTouches[i].clientY);
        break;
      }
    }
  }, { passive: false });

  const onTouchEnd = (e) => {
    for (let i = 0; i < e.changedTouches.length; i++) {
      if (e.changedTouches[i].identifier === activeTouchId) {
        const duration = Date.now() - touchStartMs;
        const moveDist = Math.hypot(e.changedTouches[i].clientX - startX, e.changedTouches[i].clientY - startY);
        resetStick();
        if (duration < 300 && moveDist < 12) {
          padState[l3r3Key] = true;
          sendPadState();
          setTimeout(() => {
            padState[l3r3Key] = false;
            sendPadState();
          }, 150);
        }
        break;
      }
    }
  };
  zone.addEventListener('touchend', onTouchEnd, { passive: false });
  zone.addEventListener('touchcancel', onTouchEnd, { passive: false });

  // Mouse fallback
  let isMouseDown = false;
  zone.addEventListener('mousedown', (e) => {
    isMouseDown = true;
    updateStick(e.clientX, e.clientY);
  });
  window.addEventListener('mousemove', (e) => {
    if (isMouseDown) updateStick(e.clientX, e.clientY);
  });
  window.addEventListener('mouseup', () => {
    if (isMouseDown) {
      isMouseDown = false;
      resetStick();
    }
  });
}

bindPadButtons();
setupStick('leftStickZone', 'leftStickHead', 'leftStickX', 'leftStickY', 'btnL3');
setupStick('rightStickZone', 'rightStickHead', 'rightStickX', 'rightStickY', 'btnR3');

// Periodic streaming state poll and search overlay lock
let isCurrentlyStreaming = false;
function pollStreamingState() {
  fetch('/api/status').then(r => r.json()).then(data => {
    const streaming = !!data.streaming;
    if (streaming !== isCurrentlyStreaming) {
      isCurrentlyStreaming = streaming;
      const overlay = document.getElementById('searchOverlay');
      if (overlay) overlay.style.display = streaming ? 'flex' : 'none';
      if (searchInput) searchInput.disabled = streaming;
    }
  }).catch(() => {});
}
setInterval(pollStreamingState, 1500);
pollStreamingState();

// Gamepad keepalive while gamepad tab is open (300ms ensures held buttons never drop)
setInterval(() => {
  const gpPane = document.getElementById('tab-gamepad');
  if (gpPane && gpPane.style.display !== 'none') {
    sendPadState();
  }
}, 300);
</script>
</body>
</html>)html";

std::string extractJsonField(const std::string& body, const std::string& field) {
    auto j = json::parse(body);
    if (j && j->isObject()) {
        auto val = (*j)[field];
        if (val.isString()) return val.str();
    }
    // Fallback: simple urlencoded or key=value
    std::string needle = field + "=";
    auto pos = body.find(needle);
    if (pos != std::string::npos) {
        auto end = body.find('&', pos);
        if (end == std::string::npos) end = body.size();
        return body.substr(pos + needle.size(), end - (pos + needle.size()));
    }
    return "";
}

}  // namespace

std::unique_ptr<WebServer> g_webServer;

WebServer::WebServer() = default;

WebServer::~WebServer() {
    stop();
}

std::string WebServer::localIp() const {
    return platform::localIPv4();
}

std::string WebServer::url() const {
    return "http://" + localIp() + ":" + std::to_string(port_);
}

bool WebServer::start(uint16_t port) {
    if (running_.load()) return true;
    port_ = port;

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        XC_LOGW("web server: socket() failed (errno %d)", errno);
        return false;
    }

    int one = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        XC_LOGW("web server: bind(%u) failed (errno %d)", port_, errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (::listen(listenFd_, 8) < 0) {
        XC_LOGW("web server: listen() failed (errno %d)", errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    running_.store(true);
    if (!platform::startThread(thread_, [this] { threadLoop(); })) {
        XC_LOGW("web server: failed to start thread");
        running_.store(false);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    XC_LOGI("web server: running at %s", url().c_str());
    return true;
}

namespace ws {

std::string computeAccept(const std::string& key) {
    std::string trimmed = key;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r' || trimmed.front() == '\n')) {
        trimmed.erase(trimmed.begin());
    }
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n')) {
        trimmed.pop_back();
    }
    trimmed += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char sha[20];
    mbedtls_sha1(reinterpret_cast<const unsigned char*>(trimmed.data()), trimmed.size(), sha);
    unsigned char b64[64];
    size_t olen = 0;
    mbedtls_base64_encode(b64, sizeof(b64), &olen, sha, sizeof(sha));
    return std::string(reinterpret_cast<char*>(b64), olen);
}

std::vector<uint8_t> makeFrame(Opcode opcode, const uint8_t* data, size_t len, bool fin) {
    std::vector<uint8_t> frame;
    uint8_t b0 = (fin ? 0x80 : 0x00) | (static_cast<uint8_t>(opcode) & 0x0F);
    frame.push_back(b0);
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
        }
    }
    if (data && len > 0) {
        frame.insert(frame.end(), data, data + len);
    }
    return frame;
}

std::vector<uint8_t> makeTextFrame(const std::string& text) {
    return makeFrame(Opcode::Text, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

std::vector<uint8_t> makeBinaryFrame(const uint8_t* data, size_t len) {
    return makeFrame(Opcode::Binary, data, len);
}

std::vector<uint8_t> makePongFrame(const std::vector<uint8_t>& pingPayload) {
    return makeFrame(Opcode::Pong, pingPayload.data(), pingPayload.size());
}

std::vector<uint8_t> makeCloseFrame(uint16_t code) {
    uint8_t payload[2];
    payload[0] = static_cast<uint8_t>((code >> 8) & 0xFF);
    payload[1] = static_cast<uint8_t>(code & 0xFF);
    return makeFrame(Opcode::Close, payload, sizeof(payload));
}

bool parseFrame(const uint8_t* data, size_t len, Frame& outFrame, size_t& bytesConsumed) {
    if (!data || len < 2) return false;
    bool fin = (data[0] & 0x80) != 0;
    Opcode opcode = static_cast<Opcode>(data[0] & 0x0F);
    bool masked = (data[1] & 0x80) != 0;
    uint8_t lenField = data[1] & 0x7F;

    size_t headerSize = 2;
    uint64_t payloadLen = 0;
    if (lenField < 126) {
        payloadLen = lenField;
    } else if (lenField == 126) {
        if (len < headerSize + 2) return false;
        payloadLen = (static_cast<uint64_t>(data[2]) << 8) | data[3];
        headerSize += 2;
    } else {
        if (len < headerSize + 8) return false;
        payloadLen = 0;
        for (int i = 0; i < 8; ++i) {
            payloadLen = (payloadLen << 8) | data[headerSize + i];
        }
        headerSize += 8;
    }
    if (payloadLen > 1048576) return false;

    uint8_t maskKey[4] = {0, 0, 0, 0};
    if (masked) {
        if (len < headerSize + 4) return false;
        std::memcpy(maskKey, data + headerSize, 4);
        headerSize += 4;
    }

    if (len < headerSize + payloadLen) return false;

    outFrame.fin = fin;
    outFrame.opcode = opcode;
    outFrame.payload.resize(static_cast<size_t>(payloadLen));
    for (size_t i = 0; i < payloadLen; ++i) {
        uint8_t b = data[headerSize + i];
        if (masked) b ^= maskKey[i % 4];
        outFrame.payload[i] = b;
    }
    bytesConsumed = headerSize + static_cast<size_t>(payloadLen);
    return true;
}

}  // namespace ws

bool parseGamepadJson(const json::Value& val, int& outSlot, input::ControllerState& outState) {
    if (!val.isObject()) return false;
    int slot = 0;
    if (val.has("playerIndex")) {
        slot = static_cast<int>(val["playerIndex"].asInt(0));
    } else if (val.has("player")) {
        int p = static_cast<int>(val["player"].asInt(1));
        slot = (p == 2) ? 1 : 0;
    }
    if (slot < 0) slot = 0;
    if (slot > 1) slot = 1;
    outSlot = slot;

    outState = input::ControllerState{};
    outState.connected = true;

    auto readButton = [](const json::Value& b) -> bool {
        if (b.isBool()) return b.asBool();
        if (b.isNumber()) return b.asNumber() > 0.5;
        if (b.isObject()) {
            if (b.has("pressed")) return b["pressed"].asBool();
            if (b.has("value")) return b["value"].asNumber() > 0.5;
        }
        return false;
    };

    auto readTrigger = [](const json::Value& b) -> float {
        if (b.isNumber()) return static_cast<float>(b.asNumber(0.0));
        if (b.isBool()) return b.asBool() ? 1.0f : 0.0f;
        if (b.isObject() && b.has("value")) return static_cast<float>(b["value"].asNumber(0.0));
        if (b.isObject() && b.has("pressed")) return b["pressed"].asBool() ? 1.0f : 0.0f;
        return 0.0f;
    };

    // 1. W3C Gamepad API standard arrays
    if (val.has("buttons") && val["buttons"].isArray()) {
        const auto& arr = val["buttons"];
        size_t n = arr.size();
        if (n > 0) outState.btnA = readButton(arr[0]);
        if (n > 1) outState.btnB = readButton(arr[1]);
        if (n > 2) outState.btnX = readButton(arr[2]);
        if (n > 3) outState.btnY = readButton(arr[3]);
        if (n > 4) outState.btnL1 = readButton(arr[4]);
        if (n > 5) outState.btnR1 = readButton(arr[5]);
        if (n > 6) outState.triggerL2 = readTrigger(arr[6]);
        if (n > 7) outState.triggerR2 = readTrigger(arr[7]);
        if (n > 8) outState.btnTouchpad = readButton(arr[8]); // Select / View
        if (n > 9) outState.btnOptions = readButton(arr[9]);   // Start / Menu
        if (n > 10) outState.btnL3 = readButton(arr[10]);
        if (n > 11) outState.btnR3 = readButton(arr[11]);
        if (n > 12) outState.dpadUp = readButton(arr[12]);
        if (n > 13) outState.dpadDown = readButton(arr[13]);
        if (n > 14) outState.dpadLeft = readButton(arr[14]);
        if (n > 15) outState.dpadRight = readButton(arr[15]);
        if (n > 16) outState.btnNexus = readButton(arr[16]);   // Xbox / Guide
    }

    if (val.has("axes") && val["axes"].isArray()) {
        const auto& arr = val["axes"];
        size_t n = arr.size();
        if (n > 0) outState.leftStickX = static_cast<float>(arr[0].asNumber(0.0));
        if (n > 1) outState.leftStickY = static_cast<float>(arr[1].asNumber(0.0));
        if (n > 2) outState.rightStickX = static_cast<float>(arr[2].asNumber(0.0));
        if (n > 3) outState.rightStickY = static_cast<float>(arr[3].asNumber(0.0));
    }

    // 2. Named / object mappings and flat fields
    auto getBool = [&](const char* k1, const char* k2 = nullptr, const char* k3 = nullptr) -> bool {
        if (val.has(k1)) return val[k1].asBool(false);
        if (k2 && val.has(k2)) return val[k2].asBool(false);
        if (k3 && val.has(k3)) return val[k3].asBool(false);
        if (val.has("buttons") && val["buttons"].isObject()) {
            const auto& b = val["buttons"];
            if (b.has(k1)) return b[k1].asBool(false);
            if (k2 && b.has(k2)) return b[k2].asBool(false);
            if (k3 && b.has(k3)) return b[k3].asBool(false);
        }
        return false;
    };

    auto getFloat = [&](const char* k1, const char* k2 = nullptr) -> float {
        if (val.has(k1)) return static_cast<float>(val[k1].asNumber(0.0));
        if (k2 && val.has(k2)) return static_cast<float>(val[k2].asNumber(0.0));
        if (val.has("axes") && val["axes"].isObject()) {
            const auto& a = val["axes"];
            if (a.has(k1)) return static_cast<float>(a[k1].asNumber(0.0));
            if (k2 && a.has(k2)) return static_cast<float>(a[k2].asNumber(0.0));
        }
        return 0.0f;
    };

    if (getBool("btnA", "a")) outState.btnA = true;
    if (getBool("btnB", "b")) outState.btnB = true;
    if (getBool("btnX", "x")) outState.btnX = true;
    if (getBool("btnY", "y")) outState.btnY = true;

    if (getBool("dpadUp", "dpad_up", "up")) outState.dpadUp = true;
    if (getBool("dpadDown", "dpad_down", "down")) outState.dpadDown = true;
    if (getBool("dpadLeft", "dpad_left", "left")) outState.dpadLeft = true;
    if (getBool("dpadRight", "dpad_right", "right")) outState.dpadRight = true;

    if (getBool("btnL1", "lb", "l1")) outState.btnL1 = true;
    if (getBool("btnR1", "rb", "r1")) outState.btnR1 = true;
    if (getBool("btnL3", "ls", "l3")) outState.btnL3 = true;
    if (getBool("btnR3", "rs", "r3")) outState.btnR3 = true;

    if (getBool("btnTouchpad", "btnView", "view")) outState.btnTouchpad = true;
    if (getBool("btnOptions", "btnMenu", "menu")) outState.btnOptions = true;
    if (getBool("btnNexus", "xbox", "guide")) outState.btnNexus = true;

    float tL = getFloat("triggerL2", "lt");
    if (tL > 0.0f) outState.triggerL2 = tL;
    float tR = getFloat("triggerR2", "rt");
    if (tR > 0.0f) outState.triggerR2 = tR;

    if (val.has("leftStickX") || val.has("lx") || (val.has("axes") && val["axes"].isObject())) {
        float lx = getFloat("leftStickX", "lx");
        if (std::abs(lx) > 0.001f) outState.leftStickX = lx;
    }
    if (val.has("leftStickY") || val.has("ly") || (val.has("axes") && val["axes"].isObject())) {
        float ly = getFloat("leftStickY", "ly");
        if (std::abs(ly) > 0.001f) outState.leftStickY = ly;
    }
    if (val.has("rightStickX") || val.has("rx") || (val.has("axes") && val["axes"].isObject())) {
        float rx = getFloat("rightStickX", "rx");
        if (std::abs(rx) > 0.001f) outState.rightStickX = rx;
    }
    if (val.has("rightStickY") || val.has("ry") || (val.has("axes") && val["axes"].isObject())) {
        float ry = getFloat("rightStickY", "ry");
        if (std::abs(ry) > 0.001f) outState.rightStickY = ry;
    }

    outState.rawLeftX = outState.leftStickX;
    outState.rawLeftY = outState.leftStickY;
    outState.rawRightX = outState.rightStickX;
    outState.rawRightY = outState.rightStickY;
    return true;
}

void WebServer::stop() {
    if (!running_.exchange(false)) return;
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        for (int fd : wsClients_) {
            ::shutdown(fd, SHUT_RDWR);
        }
        wsClients_.clear();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    XC_LOGI("web server: stopped");
}

void WebServer::threadLoop() {
    while (running_.load()) {
        pollfd pfd{listenFd_, POLLIN, 0};
        int pr = ::poll(&pfd, 1, 250);
        if (pr <= 0) continue;
        if (pfd.revents & POLLIN) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int clientFd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientFd >= 0) {
                std::thread([this, clientFd]() {
                    handleClient(clientFd);
                    ::close(clientFd);
                }).detach();
            }
        }
    }
}

void WebServer::handleClient(int clientFd) {
    timeval tv{2, 0};  // 2s timeout prevents hung clients from deadlocking server thread
    ::setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::string req;
    char buf[8192];
    size_t contentLength = 0;
    bool headersComplete = false;

    while (req.size() < 65536) {
        ssize_t n = ::recv(clientFd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, static_cast<size_t>(n));

        if (!headersComplete) {
            auto hdrEnd = req.find("\r\n\r\n");
            if (hdrEnd != std::string::npos) {
                headersComplete = true;
                std::string lowerHdr = req.substr(0, hdrEnd);
                for (char& c : lowerHdr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                auto clPos = lowerHdr.find("content-length:");
                if (clPos != std::string::npos) {
                    size_t valStart = clPos + 15;
                    while (valStart < hdrEnd && (lowerHdr[valStart] == ' ' || lowerHdr[valStart] == '\t')) valStart++;
                    size_t valEnd = lowerHdr.find("\r\n", valStart);
                    if (valEnd != std::string::npos) {
                        try {
                            contentLength = std::stoul(lowerHdr.substr(valStart, valEnd - valStart));
                        } catch (...) {}
                    }
                }
            }
        }

        if (headersComplete) {
            auto hdrEnd = req.find("\r\n\r\n");
            size_t bodyReceived = req.size() - (hdrEnd + 4);
            if (bodyReceived >= contentLength) {
                break;
            }
        }
    }

    if (req.empty()) return;

    auto lineEnd = req.find("\r\n");
    if (lineEnd == std::string::npos) lineEnd = req.find('\n');
    if (lineEnd == std::string::npos) return;

    std::string firstLine = req.substr(0, lineEnd);
    auto sp1 = firstLine.find(' ');
    auto sp2 = firstLine.rfind(' ');
    if (sp1 == std::string::npos || sp2 == std::string::npos || sp1 >= sp2) return;

    std::string method = firstLine.substr(0, sp1);
    std::string path = firstLine.substr(sp1 + 1, sp2 - (sp1 + 1));

    // Strip query string if any from path
    auto qpos = path.find('?');
    if (qpos != std::string::npos) path = path.substr(0, qpos);

    // WebSocket upgrade check
    auto hdrEnd = req.find("\r\n\r\n");
    if (hdrEnd != std::string::npos) {
        std::string lowerHdr = req.substr(0, hdrEnd);
        for (char& c : lowerHdr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool isWs = (lowerHdr.find("upgrade: websocket") != std::string::npos ||
                     (lowerHdr.find("upgrade:") != std::string::npos && lowerHdr.find("websocket") != std::string::npos));
        if (isWs && (path == "/ws" || path == "/api/ws")) {
            auto keyPos = lowerHdr.find("sec-websocket-key:");
            if (keyPos != std::string::npos) {
                size_t valStart = keyPos + 18;
                while (valStart < hdrEnd && (req[valStart] == ' ' || req[valStart] == '\t')) valStart++;
                size_t valEnd = req.find("\r\n", valStart);
                if (valEnd != std::string::npos) {
                    std::string secKey = req.substr(valStart, valEnd - valStart);
                    std::string accept = ws::computeAccept(secKey);
                    std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                                       "Upgrade: websocket\r\n"
                                       "Connection: Upgrade\r\n"
                                       "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
                    ::send(clientFd, resp.data(), resp.size(), MSG_NOSIGNAL);
                    handleWebSocket(clientFd);
                    return;
                }
            }
        }
    }

    // Find body if present
    std::string body;
    if (hdrEnd != std::string::npos) {
        body = req.substr(hdrEnd + 4);
        if (contentLength > 0 && body.size() > contentLength) {
            body.resize(contentLength);
        }
    }

    HttpResponse res = handleRequest(method, path, body);

    std::string statusText = res.status == 200   ? "200 OK"
                             : res.status == 204 ? "204 No Content"
                             : res.status == 400 ? "400 Bad Request"
                             : res.status == 404 ? "404 Not Found"
                                                 : "500 Internal Server Error";

    std::string response = "HTTP/1.1 " + statusText + "\r\n";
    response += "Content-Type: " + res.contentType + "\r\n";
    response += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type\r\n";
    if (!res.extraHeaders.empty()) response += res.extraHeaders;
    response += "Connection: close\r\n\r\n";
    response += res.body;

    size_t sent = 0;
    while (sent < response.size()) {
        ssize_t w = ::send(clientFd, response.data() + sent, response.size() - sent, 0);
        if (w <= 0) break;
        sent += static_cast<size_t>(w);
    }
}

void WebServer::handleWebSocket(int wsFd) {
    int nodelay = 1;
    ::setsockopt(wsFd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        wsClients_.push_back(wsFd);
    }

    std::vector<uint8_t> inBuf;
    uint8_t recvBuf[4096];
    uint64_t lastTelMs = 0;

    while (running_.load()) {
        pollfd pfd{wsFd, POLLIN, 0};
        int pr = ::poll(&pfd, 1, 30);  // ~33 Hz tick
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }
        if (pfd.revents & POLLIN) {
            ssize_t n = ::recv(wsFd, recvBuf, sizeof(recvBuf), 0);
            if (n <= 0) break;
            if (inBuf.size() + n > 65536) inBuf.clear();
            inBuf.insert(inBuf.end(), recvBuf, recvBuf + n);

            ws::Frame frame;
            size_t consumed = 0;
            while (ws::parseFrame(inBuf.data(), inBuf.size(), frame, consumed)) {
                inBuf.erase(inBuf.begin(), inBuf.begin() + consumed);
                if (frame.opcode == ws::Opcode::Close) {
                    auto closeResp = ws::makeCloseFrame();
                    ::send(wsFd, closeResp.data(), closeResp.size(), MSG_NOSIGNAL);
                    goto done;
                } else if (frame.opcode == ws::Opcode::Ping) {
                    auto pong = ws::makePongFrame(frame.payload);
                    ::send(wsFd, pong.data(), pong.size(), MSG_NOSIGNAL);
                } else if (frame.opcode == ws::Opcode::Text) {
                    std::string jsonStr(reinterpret_cast<const char*>(frame.payload.data()), frame.payload.size());
                    auto j = json::parse(jsonStr);
                    if (j && j->isObject()) {
                        int slot = 0;
                        input::ControllerState s{};
                        if (parseGamepadJson(*j, slot, s)) {
                            setVirtualGamepad(slot, s);
                            if (s_hooks.injectGamepad) s_hooks.injectGamepad(slot, s);
                        }
                    }
                } else if (frame.opcode == ws::Opcode::Binary) {
                    if (frame.payload.size() >= 13) {
                        int slot = frame.payload[0] & 0x01;
                        uint16_t btns = static_cast<uint16_t>(frame.payload[1]) | (static_cast<uint16_t>(frame.payload[2]) << 8);
                        input::ControllerState s{};
                        s.connected = true;
                        s.btnA = (btns & 0x0001) != 0;
                        s.btnB = (btns & 0x0002) != 0;
                        s.btnX = (btns & 0x0004) != 0;
                        s.btnY = (btns & 0x0008) != 0;
                        s.dpadUp = (btns & 0x0010) != 0;
                        s.dpadDown = (btns & 0x0020) != 0;
                        s.dpadLeft = (btns & 0x0040) != 0;
                        s.dpadRight = (btns & 0x0080) != 0;
                        s.btnL1 = (btns & 0x0100) != 0;
                        s.btnR1 = (btns & 0x0200) != 0;
                        s.btnL3 = (btns & 0x0400) != 0;
                        s.btnR3 = (btns & 0x0800) != 0;
                        s.btnTouchpad = (btns & 0x1000) != 0;
                        s.btnOptions = (btns & 0x2000) != 0;
                        s.btnNexus = (btns & 0x4000) != 0;
                        s.triggerL2 = static_cast<float>(frame.payload[3]) / 255.0f;
                        s.triggerR2 = static_cast<float>(frame.payload[4]) / 255.0f;
                        int16_t lx = static_cast<int16_t>(frame.payload[5] | (frame.payload[6] << 8));
                        int16_t ly = static_cast<int16_t>(frame.payload[7] | (frame.payload[8] << 8));
                        int16_t rx = static_cast<int16_t>(frame.payload[9] | (frame.payload[10] << 8));
                        int16_t ry = static_cast<int16_t>(frame.payload[11] | (frame.payload[12] << 8));
                        s.leftStickX = std::clamp(static_cast<float>(lx) / 32768.0f, -1.0f, 1.0f);
                        s.leftStickY = std::clamp(static_cast<float>(ly) / 32768.0f, -1.0f, 1.0f);
                        s.rightStickX = std::clamp(static_cast<float>(rx) / 32768.0f, -1.0f, 1.0f);
                        s.rightStickY = std::clamp(static_cast<float>(ry) / 32768.0f, -1.0f, 1.0f);
                        s.rawLeftX = s.leftStickX;
                        s.rawLeftY = s.leftStickY;
                        s.rawRightX = s.rightStickX;
                        s.rawRightY = s.rightStickY;
                        setVirtualGamepad(slot, s);
                        if (s_hooks.injectGamepad) s_hooks.injectGamepad(slot, s);
                    }
                }
            }
        }

        uint64_t now = platform::nowMs();
        if (now - lastTelMs >= 30) {  // ~33 fps telemetry
            lastTelMs = now;
            json::Value tel = json::Value::object();
            tel.set("type", "telemetry");
            json::Value stats = json::Value::object();
            if (s_hooks.getTelemetry) s_hooks.getTelemetry(stats);
            tel.set("stats", stats);
            auto f = ws::makeTextFrame(tel.dump());
            ssize_t sent = ::send(wsFd, f.data(), f.size(), MSG_NOSIGNAL);
            if (sent < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                break;
            }
        }
    }

done:
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        auto it = std::find(wsClients_.begin(), wsClients_.end(), wsFd);
        if (it != wsClients_.end()) wsClients_.erase(it);
    }
}

namespace {
struct VirtualGamepadSlot {
    input::ControllerState state{};
    uint64_t lastActiveMs = 0;
};
std::mutex s_vpadMutex;
VirtualGamepadSlot s_virtualPads[2];
}  // namespace

bool virtualGamepadState(int playerIndex, input::ControllerState& outState, bool& isConnected) {
    if (playerIndex < 0 || playerIndex > 1) {
        isConnected = false;
        outState = {};
        return false;
    }
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    uint64_t now = platform::nowMs();
    uint64_t diff = now > s_virtualPads[playerIndex].lastActiveMs ? now - s_virtualPads[playerIndex].lastActiveMs : 0;
    isConnected = (s_virtualPads[playerIndex].lastActiveMs > 0 && diff < 4000);
    if (diff < 2000) {
        outState = s_virtualPads[playerIndex].state;
    } else {
        outState = {};
    }
    return isConnected;
}

void setVirtualGamepad(int playerIndex, const input::ControllerState& state) {
    if (playerIndex < 0 || playerIndex > 1) return;
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    s_virtualPads[playerIndex].state = state;
    s_virtualPads[playerIndex].lastActiveMs = platform::nowMs();
}

void resetVirtualGamepads() {
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    s_virtualPads[0] = {};
    s_virtualPads[1] = {};
}

void setWebServerHooks(WebServerHooks hooks) {
    s_hooks = std::move(hooks);
}

WebServer::HttpResponse WebServer::handleRequest(const std::string& method, const std::string& path,
                                                 const std::string& body) {
    if (method == "OPTIONS") {
        return {204, "text/plain", "", ""};
    }

    if (method == "GET") {
        if (path == "/" || path == "/index.html") {
            return {200, "text/html; charset=utf-8", "", kIndexHtml};
        }
        if (path == "/api/settings" || path == "/api/status" || path == "/api/state") {
            json::Value root = json::Value::object();
            root.set("ok", true);
            bool streaming = s_hooks.isStreaming ? s_hooks.isStreaming() : false;
            root.set("streaming", streaming);
            if (s_hooks.getSettings) {
                s_hooks.getSettings(root);
            } else {
                root.set("language", "en");
                root.set("resolution", "1080p");
                root.set("padMap", input::padMapJson(input::defaultPadMap()));
                root.set("keyMap", keyMapToJson(input::defaultKeyMap()));
            }
            if (s_hooks.getSearchParams) {
                std::string q, tab;
                json::Value filters = json::Value::object();
                s_hooks.getSearchParams(q, tab, filters);
                root.set("query", q);
                root.set("tab", tab);
                root.set("filters", filters);
            } else if (s_hooks.getSearchQuery) {
                root.set("query", s_hooks.getSearchQuery());
                root.set("tab", "gamepass");
                root.set("filters", json::Value::object());
            } else {
                root.set("query", "");
                root.set("tab", "gamepass");
                root.set("filters", json::Value::object());
            }
            return {200, "application/json", "", root.dump()};
        }
        if (path == "/api/log") {
            std::string rawLog;
            platform::readFile(platform::dataDir() + "/xcloud.log", rawLog);
            std::string clean = report::sanitize(rawLog, {});
            return {200, "text/plain; charset=utf-8", "Content-Disposition: attachment; filename=\"xcloud.log\"\r\n", clean};
        }
        if (path == "/api/stats") {
            json::Value stats = json::Value::object();
            if (s_hooks.getTelemetry) {
                s_hooks.getTelemetry(stats);
            }
            return {200, "application/json", "", stats.dump()};
        }
        if (path == "/api/stream") {
            json::Value menu = json::Value::object();
            if (s_hooks.getStreamMenuState) {
                s_hooks.getStreamMenuState(menu);
            }
            return {200, "application/json", "", menu.dump()};
        }
        if (path == "/ws" || path == "/api/ws") {
            return {101, "application/json", "", "{\"ok\":true,\"endpoint\":\"websocket\"}"};
        }
    }

    if (method == "POST") {
        if (path == "/api/gamepad" || path == "/api/input") {
            auto j = json::parse(body);
            if (!j || !j->isObject()) {
                return {400, "application/json", "", "{\"ok\":false,\"error\":\"invalid json\"}"};
            }
            int slot = 0;
            input::ControllerState s{};
            if (!parseGamepadJson(*j, slot, s)) {
                return {400, "application/json", "", "{\"ok\":false,\"error\":\"invalid state\"}"};
            }

            setVirtualGamepad(slot, s);
            if (s_hooks.injectGamepad) {
                s_hooks.injectGamepad(slot, s);
            }

            json::Value res = json::Value::object();
            res.set("ok", true);
            res.set("player", slot + 1);
            return {200, "application/json", "", res.dump()};
        }
        if (path == "/api/search") {
            bool streaming = s_hooks.isStreaming ? s_hooks.isStreaming() : false;
            if (streaming) {
                json::Value res = json::Value::object();
                res.set("ok", false);
                res.set("error", "streaming_active");
                res.set("message", "Desativado enquanto jogando");
                return {409, "application/json", "", res.dump()};
            }
            auto j = json::parse(body);
            std::string q;
            std::string tab = "gamepass";
            json::Value filtersObj = json::Value::object();
            bool hasQuery = false;
            bool hasTab = false;
            bool hasFilters = false;

            if (j && j->isObject()) {
                if ((*j).has("query") && (*j)["query"].isString()) {
                    q = (*j)["query"].str();
                    hasQuery = true;
                }
                if ((*j).has("tab") && (*j)["tab"].isString()) {
                    tab = (*j)["tab"].str();
                    hasTab = true;
                }
                if ((*j).has("filters") && (*j)["filters"].isObject()) {
                    filtersObj = (*j)["filters"];
                    hasFilters = true;
                }
                for (const char* k : {"wishlist", "free", "cheapest", "mouseKeyboard", "plan", "console", "mode", "language",
                                      "filterWishlist", "filterFree", "filterCheapest", "filterMouseKeyboard", "filterPlan", "filterConsole",
                                      "filterMode", "filterLanguage"}) {
                    if ((*j).has(k)) {
                        filtersObj.set(k, (*j)[k]);
                        hasFilters = true;
                    }
                }
            } else {
                q = extractJsonField(body, "query");
                hasQuery = !q.empty() || body.find("query=") != std::string::npos;
                std::string t = extractJsonField(body, "tab");
                if (!t.empty()) {
                    tab = t;
                    hasTab = true;
                }
            }

            if (s_hooks.setSearchParams) {
                s_hooks.setSearchParams(hasQuery ? &q : nullptr, hasTab ? &tab : nullptr, hasFilters ? &filtersObj : nullptr);
            } else if (s_hooks.setSearchQuery && hasQuery) {
                s_hooks.setSearchQuery(q);
            }

            if (!hasTab && s_hooks.getSearchParams) {
                std::string curQ, curTab;
                json::Value curF = json::Value::object();
                s_hooks.getSearchParams(curQ, curTab, curF);
                if (!curTab.empty()) tab = curTab;
            }

            json::Value res = json::Value::object();
            res.set("ok", true);
            res.set("query", q);
            res.set("tab", tab);
            res.set("filters", filtersObj);
            return {200, "application/json", "", res.dump()};
        }
        if (path == "/api/settings") {
            auto j = json::parse(body);
            if (!j || !j->isObject()) {
                return {400, "application/json", "", "{\"ok\":false,\"error\":\"invalid json\"}"};
            }
            bool ok = true;
            if (s_hooks.updateSettings) {
                ok = s_hooks.updateSettings(*j);
            }
            return {ok ? 200 : 400, "application/json", "", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"update failed\"}"};
        }
        if (path == "/api/stream") {
            auto j = json::parse(body);
            if (!j || !j->isObject()) {
                return {400, "application/json", "", "{\"ok\":false,\"error\":\"invalid json\"}"};
            }
            if ((*j).has("action") && (*j)["action"].isString()) {
                std::string act = (*j)["action"].str();
                if (s_hooks.streamAction) {
                    s_hooks.streamAction(act);
                }
            }
            bool ok = true;
            if (s_hooks.updateStreamMenu) {
                ok = s_hooks.updateStreamMenu(*j);
            }
            json::Value res = json::Value::object();
            res.set("ok", ok);
            return {ok ? 200 : 400, "application/json", "", res.dump()};
        }
    }

    return {404, "text/plain", "", "404 Not Found\n"};
}

void startWebServer(uint16_t port) {
    if (!g_webServer) {
        g_webServer = std::make_unique<WebServer>();
    }
    g_webServer->start(port);
}

void stopWebServer() {
    if (g_webServer) {
        g_webServer->stop();
    }
}

std::string webServerUrl() {
    return g_webServer ? g_webServer->url() : "http://" + platform::localIPv4() + ":8080";
}

}  // namespace xc::app
