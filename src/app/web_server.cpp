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
#include <csignal>
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
<script>
(function() {
  function applyInitScale() {
    var w = window.innerWidth || (document.documentElement ? document.documentElement.clientWidth : 360);
    var h = window.innerHeight || (document.documentElement ? document.documentElement.clientHeight : 640);
    var isLand = w > h;
    var pScale = Math.min(1, Math.max(0.4, (w - 28) / 412));
    var lsScale = Math.min(1, Math.max(0.35, Math.min((w - 24) / 915, (h - 78) / 412)));
    var fsScale = Math.min(1.2, Math.max(0.4, Math.min((w - 28) / 915, (h - 20) / 412)));
    document.documentElement.style.setProperty('--gp-scale', (isLand ? lsScale : pScale).toFixed(3));
    document.documentElement.style.setProperty('--gp-fs-scale', fsScale.toFixed(3));
  }
  applyInitScale();
  window.addEventListener('resize', applyInitScale);
})();
</script>
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
html { width: 100%; max-width: 100%; background: var(--bg); }
body { width: 100%; max-width: 100%; margin: 0; padding: 0; overflow-x: hidden; overflow-y: auto; background: var(--bg); color: var(--text); -webkit-overflow-scrolling: touch; overscroll-behavior-y: none; }
header { box-sizing: border-box; background: var(--surface); border-bottom: 1px solid var(--border); padding-top: max(18px, env(safe-area-inset-top, 18px)); padding-bottom: 12px; padding-left: max(16px, env(safe-area-inset-left, 16px)); padding-right: max(16px, env(safe-area-inset-right, 16px)); display: flex; align-items: center; justify-content: space-between; gap: 10px; width: 100%; max-width: 100%; overflow-x: hidden; }
.brand { display: flex; align-items: center; gap: 10px; min-width: 0; flex: 1 1 auto; }
.logo-icon { flex-shrink: 0; width: 32px; height: 32px; border-radius: 50%; box-shadow: 0 0 10px rgba(16, 124, 65, 0.45); }
.brand-text { min-width: 0; flex: 1 1 auto; }
.brand-text h1 { font-size: clamp(0.76rem, 3.6vw, 0.96rem); font-weight: 700; letter-spacing: 0.5px; color: #fff; text-transform: uppercase; line-height: 1.2; white-space: nowrap; }
.brand-subtitle { font-size: 0.70rem; color: var(--text-dim); display: block; font-weight: 500; white-space: nowrap; }
header .badge { background: var(--primary); color: #fff; font-size: 0.70rem; padding: 3px 7px; border-radius: 12px; font-weight: 600; text-transform: uppercase; flex-shrink: 0; }
.nav-tabs { display: grid; grid-template-columns: repeat(3, 1fr); background: var(--surface); border-bottom: 1px solid var(--border); width: 100%; max-width: 100%; overflow: hidden; }
.nav-tab { min-width: 0; text-align: center; padding: 11px 4px; min-height: 44px; display: flex; align-items: center; justify-content: center; font-size: clamp(0.74rem, 2.8vw, 0.86rem); font-weight: 600; color: var(--text-dim); border-bottom: 2px solid transparent; border-right: 1px solid rgba(40, 47, 68, 0.45); border-top: 1px solid rgba(40, 47, 68, 0.45); cursor: pointer; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; user-select: none; -webkit-user-select: none; touch-action: manipulation; -webkit-tap-highlight-color: transparent; }
.nav-tab.active { color: #fff; border-bottom-color: var(--primary); background: var(--surface-alt); }
.tab-pane { display: none !important; height: 0 !important; max-height: 0 !important; overflow: hidden !important; width: 100%; }
.tab-pane.active { display: block !important; height: auto !important; max-height: none !important; overflow: visible !important; }
.content { max-width: 680px; width: 100%; margin: 12px auto 0 auto; padding: 0 12px 20px 12px; overflow-x: hidden; }
.subnav { display: flex; gap: 8px; margin-bottom: 12px; flex-wrap: wrap; width: 100%; max-width: 100%; }
.subnav .btn { flex: 1 1 120px; }
.subnav .btn.active { background: var(--primary); border-color: var(--primary); }
.card { background: var(--surface); border: 1px solid var(--border); border-radius: 14px; padding: 14px; margin-bottom: 14px; width: 100%; max-width: 100%; overflow-x: hidden; }
.card:last-child { margin-bottom: 0 !important; }
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
.btn { display: inline-flex; align-items: center; justify-content: center; background: var(--surface-alt); color: #fff; border: 1px solid var(--border); border-radius: 10px; padding: 10px 14px; min-height: 44px; font-size: 0.86rem; font-weight: 600; cursor: pointer; text-decoration: none; transition: background 0.15s, border-color 0.15s; max-width: 100%; white-space: normal; text-align: center; touch-action: manipulation; user-select: none; -webkit-user-select: none; -webkit-tap-highlight-color: transparent; }
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
.toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #169c53; color: #fff; padding: 10px 20px; border-radius: 24px; font-size: 0.88rem; font-weight: 600; box-shadow: 0 4px 16px rgba(0,0,0,0.5); opacity: 0; pointer-events: none; transition: opacity 0.3s; z-index: 10000; max-width: 90%; text-align: center; }
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
.section-sub { font-size: 0.86rem; font-weight: 700; color: #4ade80; margin: 14px 0 6px 0; display: flex; align-items: center; justify-content: space-between; }

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
.gamepad-toolbar {
  display: flex;
  gap: 8px;
  align-items: center;
  justify-content: flex-end;
  flex-wrap: wrap;
  margin-bottom: 8px;
}
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
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 6px;
  width: 100%;
  margin-bottom: 6px;
}
.player-btn {
  padding: 8px 6px;
  min-height: 42px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 10px;
  border: 1px solid var(--border);
  background: var(--surface-alt);
  color: var(--text-dim);
  font-size: clamp(0.74rem, 2.8vw, 0.84rem);
  font-weight: 700;
  cursor: pointer;
  text-align: center;
  transition: all 0.15s;
  touch-action: manipulation;
  user-select: none;
  -webkit-user-select: none;
  -webkit-tap-highlight-color: transparent;
}
.player-btn.active {
  background: var(--primary);
  border-color: var(--primary);
  color: #fff;
  box-shadow: 0 2px 8px rgba(16, 124, 65, 0.4);
}
/* Virtual Gamepad Viewport & Modules (Exact Layout Studio Match) */
:root {
  --gp-scale: 1;
  --gp-fs-scale: 1;
}
#tab-gamepad #gamepadCard {
  padding-left: 4px;
  padding-right: 4px;
}

.gamepad-viewport-wrapper {
  width: 100%;
  display: flex;
  justify-content: center;
  align-items: flex-start;
  overflow: hidden;
}
.gamepad-container {
  position: relative;
  background: transparent;
  user-select: none;
  -webkit-user-select: none;
  touch-action: none !important;
}
.gamepad-module {
  position: absolute;
  transform-origin: center center;
  display: flex;
  align-items: center;
  justify-content: center;
  touch-action: none !important;
  box-sizing: border-box;
}

/* Stick Styling (Matching Layout Studio) */
.stick-base {
  position: relative;
  width: 146px;
  height: 146px;
  border-radius: 50%;
  background: radial-gradient(circle at 50% 50%, #1a2233 0%, #0d121c 100%);
  border: 2px solid #334155;
  box-shadow: inset 0 3px 10px rgba(0,0,0,0.8), 0 3px 8px rgba(0,0,0,0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  touch-action: none;
}
.stick-head {
  width: 62px;
  height: 62px;
  border-radius: 50%;
  background: radial-gradient(circle at 35% 35%, #475569, #1e293b);
  border: 2px solid #64748b;
  box-shadow: 0 4px 12px rgba(0,0,0,0.7);
  pointer-events: none;
  transform: translate(0px, 0px);
  display: flex;
  align-items: center;
  justify-content: center;
}
.stick-head::after {
  content: '';
  width: 45%;
  height: 45%;
  border-radius: 50%;
  border: 1.5px solid rgba(255, 255, 255, 0.2);
  background: radial-gradient(circle, #1e293b 0%, #334155 100%);
  box-shadow: inset 0 2px 4px rgba(0, 0, 0, 0.7);
}

/* D-Pad Styling (Matching Layout Studio) */
.dpad-container {
  position: relative;
  width: 150px;
  height: 150px;
}
.dpad-arm {
  position: absolute;
  border-radius: 10px;
  background: #1e2538;
  border: 1.5px solid #3e4d70;
  color: #94a3b8;
  font-weight: 900;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 3px 6px rgba(0,0,0,0.4);
  transition: transform 0.06s, background 0.06s;
  cursor: pointer;
  touch-action: none;
  user-select: none;
}
.dpad-arm:active, .dpad-arm.pressed {
  transform: scale(0.92);
  background: #107c41 !important;
  color: #fff !important;
  border-color: #4ade80 !important;
  box-shadow: 0 0 12px rgba(16, 124, 65, 0.8) !important;
}
.dpad-arm-up { top: 0; left: 33.33%; width: 33.33%; height: 35%; }
.dpad-arm-down { bottom: 0; left: 33.33%; width: 33.33%; height: 35%; }
.dpad-arm-left { top: 33.33%; left: 0; width: 35%; height: 33.33%; }
.dpad-arm-right { top: 33.33%; right: 0; width: 35%; height: 33.33%; }
.dpad-mid {
  position: absolute;
  top: 33.33%; left: 33.33%;
  width: 33.33%; height: 33.33%;
  background: #151a27;
  border-radius: 4px;
  pointer-events: none;
}

/* ABXY Styling (Matching Layout Studio) */
.abxy-container {
  position: relative;
  width: 156px;
  height: 156px;
}
.abxy-key {
  position: absolute;
  width: 52px;
  height: 52px;
  border-radius: 50%;
  font-weight: 900;
  font-size: 1.3rem;
  color: #fff;
  display: flex;
  align-items: center;
  justify-content: center;
  border-width: 2px;
  box-shadow: 0 3px 8px rgba(0,0,0,0.5);
  transition: filter 0.06s, box-shadow 0.06s;
  cursor: pointer;
  touch-action: none;
  user-select: none;
  -webkit-user-select: none;
  -webkit-tap-highlight-color: transparent;
}
.abxy-key:active, .abxy-key.pressed {
  box-shadow: 0 0 16px rgba(255,255,255,0.85) !important;
  filter: brightness(1.35) !important;
}
.btn-y { top: 0; left: 52px; background: #c68900; border-color: #ffd000; }
.btn-a { bottom: 0; left: 52px; background: #0e6b37; border-color: #22c55e; }
.btn-x { top: 52px; left: 0; background: #006eb8; border-color: #38bdf8; }
.btn-b { top: 52px; right: 0; background: #b80d1b; border-color: #f87171; }

/* Buttons & System Styling */
.pad-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: #1e2538;
  border: 1.5px solid #3e4d70;
  border-radius: 10px;
  color: #fff;
  font-weight: 700;
  box-shadow: 0 2px 6px rgba(0,0,0,0.3);
  transition: transform 0.06s, background 0.06s;
  cursor: pointer;
  touch-action: none;
  user-select: none;
  width: 100%;
  height: 100%;
}
.pad-btn:active, .pad-btn.pressed {
  transform: scale(0.92);
  background: #107c41 !important;
  border-color: #4ade80 !important;
  box-shadow: 0 0 12px rgba(16, 124, 65, 0.7) !important;
}
.pad-btn-trigger {
  font-weight: 800;
  font-size: 1.05rem;
  background: linear-gradient(180deg, #2e3a54, #182030);
  border: 1.5px solid #4a5c82;
  border-radius: 12px;
}
.pad-btn-bumper {
  font-weight: 700;
  font-size: 0.92rem;
  border-radius: 10px;
}
.pad-btn-stick-click {
  font-size: 0.82rem;
  color: #94a3b8;
  border-radius: 8px;
}
.center-sys-container {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  width: 100%;
  height: 100%;
}
.pad-btn-sys {
  padding: 4px 10px;
  font-size: 11px;
  font-weight: 800;
  height: 36px;
  border-radius: 8px;
}
.pad-btn-nexus-btn {
  width: 38px;
  height: 38px;
  border-radius: 50%;
  background: #107c41;
  border: 2px solid #ffffff;
  font-weight: 900;
  font-size: 12px;
  color: #ffffff;
  box-shadow: 0 4px 10px rgba(0,0,0,0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  touch-action: none;
  flex-shrink: 0;
}
.pad-btn-nexus-btn:active, .pad-btn-nexus-btn.pressed {
  transform: scale(0.92);
  filter: brightness(1.2);
}

/* Player Bar Styling (Clean minimalist capsule matching studio) */
.player-bar-container {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 5px;
  background: rgba(15, 23, 42, 0.9);
  padding: 3px 6px;
  border-radius: 12px;
  border: 1px solid #1e293b;
  width: 100%;
  height: 100%;
}
.player-pill {
  padding: 3px 8px;
  font-size: 11px;
  font-weight: 800;
  border-radius: 8px;
  background: transparent;
  color: #94a3b8;
  border: none;
  cursor: pointer;
  transition: all 0.1s;
}
.player-pill.active {
  background: #107c41;
  color: #ffffff;
  box-shadow: 0 0 8px rgba(16, 124, 65, 0.6);
}
.fs-only-exit { display: none; }

/* Portrait Layout Positions (Exact Layout Studio Alignment) */
#gamepadArea {
  width: 412px !important;
  min-width: 412px !important;
  max-width: 412px !important;
  flex-shrink: 0 !important;
  height: 680px;
}
#gamepadArea #mod_player_bar { left: 130px; top: 10px; width: 152px !important; height: 44px; transform: scale(1.0); }
#gamepadArea #mod_center_sys { left: 115px; top: 62px; width: 182px; height: 56px; transform: scale(1.0); }
#gamepadArea #mod_btn_ls { left: 16px; top: 132px; width: 56px; height: 52px; transform: scale(1.25); }
#gamepadArea #mod_btn_rs { left: 340px; top: 132px; width: 56px; height: 52px; transform: scale(1.25); }
#gamepadArea #mod_btn_lt { left: 10px; top: 206px; width: 98px; height: 56px; transform: scale(1.0); }
#gamepadArea #mod_btn_lb { left: 116px; top: 207px; width: 78px; height: 52px; transform: scale(1.0); }
#gamepadArea #mod_btn_rb { left: 218px; top: 207px; width: 78px; height: 52px; transform: scale(1.0); }
#gamepadArea #mod_btn_rt { left: 304px; top: 206px; width: 98px; height: 56px; transform: scale(1.0); }
#gamepadArea #mod_stick_left { left: 21px; top: 282px; width: 166px; height: 166px; transform: scale(1.15); }
#gamepadArea #mod_abxy { left: 218px; top: 282px; width: 176px; height: 176px; transform: scale(1.15); }
#gamepadArea #mod_dpad { left: 19px; top: 488px; width: 160px !important; height: 160px; transform: scale(1.0); }
#gamepadArea #mod_stick_right { left: 225px; top: 490px; width: 166px; height: 166px; transform: scale(1.0); }

/* Responsive scale wrapper for Portrait */
@media (orientation: portrait), (max-width: 600px) and (orientation: portrait) {
  .gamepad-viewport-wrapper {
    height: calc(680px * var(--gp-scale, 0.82));
  }
  #gamepadArea {
    transform: scale(var(--gp-scale, 0.82));
    transform-origin: top center;
  }
}

/* Landscape Layout Positions (Exact Layout Studio Alignment) */
@media (orientation: landscape), (max-height: 520px) {
  .gamepad-status-bar {
    display: none !important;
  }
  .gamepad-viewport-wrapper {
    height: calc(412px * var(--gp-scale, 0.8));
    min-height: calc(412px * var(--gp-scale, 0.8));
    display: flex;
    justify-content: center;
    align-items: flex-start;
    overflow: hidden;
  }
  #gamepadArea {
    width: 915px !important;
    min-width: 915px !important;
    max-width: 915px !important;
    flex-shrink: 0 !important;
    height: 412px !important;
    transform: scale(var(--gp-scale, 0.8));
    transform-origin: top center !important;
  }
  #gamepadArea #mod_btn_lt { left: 10px; top: 10px; width: 94px; height: 54px; transform: scale(1.0); }
  #gamepadArea #mod_btn_lb { left: 110px; top: 11px; width: 76px; height: 52px; transform: scale(1.0); }
  #gamepadArea #mod_btn_ls { left: 192px; top: 11px; width: 54px; height: 52px; transform: scale(1.0); }
  #gamepadArea #mod_player_bar { left: 381px; top: 12px; width: 152px !important; height: 44px; transform: scale(1.0); }
  #gamepadArea #mod_center_sys { left: 342px; top: 66px; width: 230px; height: 56px; transform: scale(1.0); }
  #gamepadArea #mod_btn_rs { left: 669px; top: 11px; width: 54px; height: 52px; transform: scale(1.0); }
  #gamepadArea #mod_btn_rb { left: 729px; top: 11px; width: 76px; height: 52px; transform: scale(1.0); }
  #gamepadArea #mod_btn_rt { left: 811px; top: 10px; width: 94px; height: 54px; transform: scale(1.0); }
  #gamepadArea #mod_stick_left { left: 32px; top: 97px; width: 166px; height: 166px; transform: scale(1.3); }
  #gamepadArea #mod_abxy { left: 712px; top: 97px; width: 176px; height: 176px; transform: scale(1.3); }
  #gamepadArea #mod_dpad { left: 205px; top: 242px; width: 160px !important; height: 160px; transform: scale(1.05); }
  #gamepadArea #mod_stick_right { left: 545px; top: 240px; width: 166px; height: 166px; transform: scale(1.2); }
}

/* Fullscreen Mode (Landscape) */
#gamepadCard.gamepad-fullscreen, #gamepadCard:fullscreen, #gamepadCard:-webkit-full-screen {
  position: fixed !important;
  inset: 0 !important;
  width: 100vw !important;
  height: 100dvh !important;
  max-width: 100vw !important;
  margin: 0 !important;
  border-radius: 0 !important;
  border: none !important;
  z-index: 9999 !important;
  background: radial-gradient(circle at 50% 35%, #161c2c 0%, #090b11 100%) !important;
  padding: env(safe-area-inset-top, 0) env(safe-area-inset-right, 14px) env(safe-area-inset-bottom, 8px) env(safe-area-inset-left, 14px) !important;
  display: flex !important;
  align-items: center !important;
  justify-content: center !important;
  overflow: hidden !important;
}
#gamepadCard.gamepad-fullscreen .gamepad-intro,
#gamepadCard:fullscreen .gamepad-intro,
#gamepadCard:-webkit-full-screen .gamepad-intro,
#gamepadCard.gamepad-fullscreen .gamepad-status-bar,
#gamepadCard:fullscreen .gamepad-status-bar,
#gamepadCard:-webkit-full-screen .gamepad-status-bar {
  display: none !important;
}
#gamepadCard.gamepad-fullscreen .fs-only-exit,
#gamepadCard:fullscreen .fs-only-exit,
#gamepadCard:-webkit-full-screen .fs-only-exit {
  display: inline-flex !important;
}
#gamepadCard.gamepad-fullscreen .gamepad-viewport-wrapper,
#gamepadCard:fullscreen .gamepad-viewport-wrapper,
#gamepadCard:-webkit-full-screen .gamepad-viewport-wrapper {
  width: calc(915px * var(--gp-fs-scale, 0.85)) !important;
  height: calc(412px * var(--gp-fs-scale, 0.85)) !important;
  max-width: 100% !important;
  max-height: 100% !important;
  display: flex !important;
  align-items: flex-start !important;
  justify-content: center !important;
  overflow: hidden !important;
  position: relative !important;
}
#gamepadCard.gamepad-fullscreen #gamepadArea,
#gamepadCard:fullscreen #gamepadArea,
#gamepadCard:-webkit-full-screen #gamepadArea {
  width: 915px !important;
  min-width: 915px !important;
  max-width: 915px !important;
  flex-shrink: 0 !important;
  height: 412px !important;
  transform: scale(var(--gp-fs-scale, 0.85)) !important;
  transform-origin: top center !important;
}
#gamepadCard.gamepad-fullscreen #mod_btn_lt { left: 10px !important; top: 10px !important; width: 94px !important; height: 54px !important; transform: scale(1.0) !important; }
#gamepadCard.gamepad-fullscreen #mod_btn_lb { left: 110px !important; top: 11px !important; width: 76px !important; height: 52px !important; transform: scale(1.0) !important; }
#gamepadCard.gamepad-fullscreen #mod_btn_ls { left: 192px !important; top: 11px !important; width: 54px !important; height: 52px !important; transform: scale(1.0) !important; }
#gamepadCard.gamepad-fullscreen #mod_player_bar { left: 381px !important; top: 12px !important; width: 152px !important; height: 44px !important; transform: scale(1.0) !important; }
#gamepadCard.gamepad-fullscreen #mod_center_sys { left: 342px !important; top: 66px !important; width: 230px !important; height: 56px !important; transform: scale(1.0) !important; }
#gamepadCard.gamepad-fullscreen #mod_btn_rs { left: 669px !important; top: 11px !important; width: 54px !important; height: 52px !important; transform: scale(1.0) !important; }
#gamepadCard.gamepad-fullscreen #mod_btn_rb { left: 729px !important; top: 11px !important; width: 76px !important; height: 52px !important; transform: scale(1.0) !important; }
#gamepadCard.gamepad-fullscreen #mod_btn_rt { left: 811px !important; top: 10px !important; width: 94px !important; height: 54px !important; transform: scale(1.0) !important; }
#gamepadCard.gamepad-fullscreen #mod_stick_left { left: 32px !important; top: 97px !important; width: 166px !important; height: 166px !important; transform: scale(1.3) !important; }
#gamepadCard.gamepad-fullscreen #mod_abxy { left: 712px !important; top: 97px !important; width: 176px !important; height: 176px !important; transform: scale(1.3) !important; }
#gamepadCard.gamepad-fullscreen #mod_dpad { left: 205px !important; top: 242px !important; width: 160px !important; height: 160px !important; transform: scale(1.05) !important; }
#gamepadCard.gamepad-fullscreen #mod_stick_right { left: 545px !important; top: 240px !important; width: 166px !important; height: 166px !important; transform: scale(1.2) !important; }

@media (max-width: 600px) {
  .nav-tabs { grid-template-columns: repeat(3, 1fr); }
  .nav-tab { padding: 9px 4px; font-size: 0.74rem; }
  .telemetry-grid { grid-template-columns: repeat(2, 1fr); }
  .content { padding-left: 6px; padding-right: 6px; }
  #tab-gamepad #gamepadCard { padding-left: 2px; padding-right: 2px; }
}

@media (orientation: landscape), (max-height: 520px) {
  .nav-tabs {
    grid-template-columns: repeat(6, 1fr);
    gap: 4px;
  }
  .nav-tab {
    min-height: 36px;
    padding: 5px 2px;
    font-size: 0.72rem;
  }
  header {
    display: none !important;
  }
  .content {
    max-width: 960px;
    margin: 2px auto 0 auto;
    padding: 0 4px 6px 4px;
  }
  .card {
    padding: 4px 8px;
    margin-bottom: 0 !important;
  }
  .card h2 {
    font-size: 0.88rem;
    margin-bottom: 4px;
  }
  .card p {
    font-size: 0.76rem;
    margin-bottom: 6px;
  }
  .gamepad-intro {
    display: block;
    margin-bottom: 2px;
  }
  .gamepad-toolbar {
    margin-bottom: 2px;
    justify-content: flex-end;
  }
  .gamepad-status-bar {
    display: none !important;
  }
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
  <div id="tab-search" class="tab-pane active">
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
      <button class="btn" id="btnSubTouch" onclick="switchRemapSub('touch')">👆 Touchpad</button>
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

    <div id="subRemapTouch" style="display:none;">
      <div class="card">
        <h2>👆 Atalhos do Touchpad (DualSense)</h2>
        <p>Configure o que cada gesto (deslizar) ou clique físico (lado esquerdo/direito) no touchpad faz durante o streaming. <strong>Touchpad + Options</strong> permanece sempre fixo para abrir o Menu do Jogo.</p>
        <div class="grid-map" id="remapTouchpadShortcutsGrid"></div>
        <div class="btn-row">
          <button class="btn btn-primary" onclick="saveSettings()">Salvar no Console</button>
          <button class="btn" onclick="resetTouchpadShortcuts()">Restaurar Touchpad Padrão</button>
        </div>
      </div>
    </div>
  </div>

  <!-- TAB: GAMEPAD (CONTROLE VIRTUAL) -->
  <div id="tab-gamepad" class="tab-pane" style="display:none;">
    <div class="card" id="gamepadCard">
      <div class="gamepad-intro">
        <div class="gamepad-toolbar">
          <button type="button" class="btn btn-primary" id="btnFullscreenPad" onclick="toggleGamepadFullscreen()" style="padding: 6px 12px; font-size: 0.78rem;">
            ⛶ Tela Cheia
          </button>
        </div>
      </div>

      <div class="gamepad-viewport-wrapper" id="gamepadScaleContainer">
        <div class="gamepad-container" id="gamepadArea">

          <!-- MODULE 1: Left Stick -->
          <div id="mod_stick_left" class="gamepad-module">
            <div class="stick-base" id="leftStickZone">
              <div class="stick-head" id="leftStickHead"></div>
            </div>
          </div>

          <!-- MODULE 2: D-Pad -->
          <div id="mod_dpad" class="gamepad-module">
            <div class="dpad-container" id="box_dpad">
              <div class="dpad-arm dpad-arm-up" data-btn="dpadUp">▲</div>
              <div class="dpad-arm dpad-arm-left" data-btn="dpadLeft">◀</div>
              <div class="dpad-mid"></div>
              <div class="dpad-arm dpad-arm-right" data-btn="dpadRight">▶</div>
              <div class="dpad-arm dpad-arm-down" data-btn="dpadDown">▼</div>
            </div>
          </div>

          <!-- MODULE 3: ABXY Diamond -->
          <div id="mod_abxy" class="gamepad-module">
            <div class="abxy-container" id="box_abxy">
              <div class="abxy-key btn-y" data-btn="btnY">Y</div>
              <div class="abxy-key btn-x" data-btn="btnX">X</div>
              <div class="abxy-key btn-b" data-btn="btnB">B</div>
              <div class="abxy-key btn-a" data-btn="btnA">A</div>
            </div>
          </div>

          <!-- MODULE 4: Right Stick -->
          <div id="mod_stick_right" class="gamepad-module">
            <div class="stick-base" id="rightStickZone">
              <div class="stick-head" id="rightStickHead"></div>
            </div>
          </div>

          <!-- INDIVIDUAL SHOULDER & TRIGGER BUTTONS -->
          <!-- MODULE 5: LT Trigger -->
          <div id="mod_btn_lt" class="gamepad-module pad-bumper-left">
            <button type="button" class="pad-btn pad-btn-trigger" id="btn_lt" data-btn="triggerL2">LT</button>
          </div>

          <!-- MODULE 6: LB Bumper -->
          <div id="mod_btn_lb" class="gamepad-module pad-bumper-left">
            <button type="button" class="pad-btn pad-btn-bumper" id="btn_lb" data-btn="btnL1">LB</button>
          </div>

          <!-- MODULE 7: LS Button -->
          <div id="mod_btn_ls" class="gamepad-module pad-bumper-left">
            <button type="button" class="pad-btn pad-btn-stick-click pad-btn-stick-btn" id="btn_ls" data-btn="btnL3">LS</button>
          </div>

          <!-- MODULE 8: RS Button -->
          <div id="mod_btn_rs" class="gamepad-module pad-bumper-right">
            <button type="button" class="pad-btn pad-btn-stick-click pad-btn-stick-btn" id="btn_rs" data-btn="btnR3">RS</button>
          </div>

          <!-- MODULE 9: RB Bumper -->
          <div id="mod_btn_rb" class="gamepad-module pad-bumper-right">
            <button type="button" class="pad-btn pad-btn-bumper" id="btn_rb" data-btn="btnR1">RB</button>
          </div>

          <!-- MODULE 10: RT Trigger -->
          <div id="mod_btn_rt" class="gamepad-module pad-bumper-right">
            <button type="button" class="pad-btn pad-btn-trigger" id="btn_rt" data-btn="triggerR2">RT</button>
          </div>

          <!-- MODULE 11: Center System Buttons (VIEW, X, MENU) -->
          <div id="mod_center_sys" class="gamepad-module">
            <div class="center-sys-container">
              <button type="button" class="pad-btn pad-btn-sys" id="btn_view" data-btn="btnView">VIEW</button>
              <button type="button" class="pad-btn-nexus-btn" id="btn_nexus" data-btn="btnNexus">X</button>
              <button type="button" class="pad-btn pad-btn-sys" id="btn_menu" data-btn="btnMenu">MENU</button>
              <button type="button" class="pad-btn pad-btn-sys fs-only-exit" id="btn_exit_fs" onclick="toggleGamepadFullscreen()" style="background:#451a1a;border-color:#b91c1c;color:#fca5a5;">✕ Sair</button>
            </div>
          </div>

          <!-- MODULE 12: Player Selector (P1..P4) -->
          <div id="mod_player_bar" class="gamepad-module">
            <div class="player-bar-container">
              <button type="button" class="player-pill active" id="btnPlayer1" onclick="setVirtualPlayer(1)">P1</button>
              <button type="button" class="player-pill" id="btnPlayer2" onclick="setVirtualPlayer(2)">P2</button>
              <button type="button" class="player-pill" id="btnPlayer3" onclick="setVirtualPlayer(3)">P3</button>
              <button type="button" class="player-pill" id="btnPlayer4" onclick="setVirtualPlayer(4)">P4</button>
            </div>
          </div>

        </div>
      </div>

      <div class="status-pill gamepad-status-bar" style="margin-top: 10px;">
        <div class="status-dot"></div>
        <span id="gamepadStatus">Controle ativo para Jogador 1 (Principal)</span>
      </div>
    </div>
  </div>

  <!-- TAB: LOGS -->
  <div id="tab-logs" class="tab-pane" style="display:none;">
    <div class="card">
      <h2>🐛 Enviar Relatório de Erro (GitHub)</h2>
      <p>Gere um relatório completo da sessão atual com logs sanitizados (sem dados privados, e-mails ou tokens) formatado automaticamente para o repositório oficial no GitHub.</p>

      <div class="form-group" style="margin-bottom: 10px;">
        <label for="reportTitle" style="font-weight: 600; font-size: 0.82rem; display: block; margin-bottom: 4px;">Título do Problema (Opcional):</label>
        <input type="text" id="reportTitle" class="form-control" placeholder="Ex: Queda de transmissão ou erro de áudio" style="width: 100%; box-sizing: border-box; padding: 8px 10px; background: var(--surface-alt); border: 1px solid var(--border); border-radius: 8px; color: #fff; font-size: 0.85rem;">
      </div>

      <div class="form-group" style="margin-bottom: 12px;">
        <label for="reportWhat" style="font-weight: 600; font-size: 0.82rem; display: block; margin-bottom: 4px;">O que aconteceu? (Opcional):</label>
        <textarea id="reportWhat" class="form-control" rows="2" placeholder="Descreva brevemente o que você estava fazendo quando o erro ocorreu..." style="width: 100%; box-sizing: border-box; padding: 8px 10px; background: var(--surface-alt); border: 1px solid var(--border); border-radius: 8px; color: #fff; font-size: 0.85rem; resize: vertical;"></textarea>
      </div>

      <div class="btn-row" style="margin-top: 0; margin-bottom: 8px; gap: 8px; display: flex; flex-wrap: wrap;">
        <button type="button" class="btn btn-primary" onclick="openGitHubReport()" id="btnOpenReport">🚀 Abrir Relatório no GitHub</button>
        <button type="button" class="btn" onclick="copySanitizedReport()" id="btnCopyReport">📋 Copiar Relatório</button>
        <button type="button" class="btn" onclick="previewReport()" id="btnPreviewReport">🔍 Pré-visualizar</button>
      </div>
    </div>

    <div class="card">
      <h2>📋 Download de Logs Cru</h2>
      <p>Baixe o arquivo <code>xcloud.log</code> sanitizado (sem tokens ou dados privados) para inspeção local ou envio manual.</p>
      <div class="btn-row" style="margin-top: 0; margin-bottom: 14px; gap: 8px; display: flex; flex-wrap: wrap;">
        <a href="/api/log" download="xcloud.log" class="btn btn-primary">⬇️ Baixar xcloud.log</a>
        <button type="button" class="btn" onclick="loadLogPreview()">Visualizar Últimas Linhas</button>
      </div>
      <pre id="logPreview">Clique em "Visualizar Últimas Linhas" ou "Pré-visualizar" para inspecionar os dados...</pre>
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

const touchpadTriggers = [
  {key: "swipeUp", label: "Deslizar ↑ (Cima)"},
  {key: "swipeDown", label: "Deslizar ↓ (Baixo)"},
  {key: "swipeLeft", label: "Deslizar ← (Esquerda)"},
  {key: "swipeRight", label: "Deslizar → (Direita)"},
  {key: "clickLeft", label: "Clique Esquerdo"},
  {key: "clickRight", label: "Clique Direito"}
];

const touchpadActions = [
  {id: "none", name: "Desativado"},
  {id: "game_menu", name: "Menu do Jogo"},
  {id: "xbox_button", name: "Botão Xbox (Guia)"},
  {id: "toggle_stats", name: "Alternar Telemetria"},
  {id: "toggle_mic_mute", name: "Silenciar Microfone"},
  {id: "cycle_gyro", name: "Alternar Giroscópio"},
  {id: "cycle_trigger_profile", name: "Alternar Perfil de Gatilho"},
  {id: "view", name: "View"},
  {id: "menu", name: "Menu"},
  {id: "l3", name: "LS (L3)"},
  {id: "r3", name: "RS (R3)"},
  {id: "a", name: "A (Cruz)"},
  {id: "b", name: "B (Círculo)"},
  {id: "x", name: "X (Quadrado)"},
  {id: "y", name: "Y (Triângulo)"},
  {id: "lb", name: "LB (L1)"},
  {id: "rb", name: "RB (R1)"},
  {id: "dpad_up", name: "D-pad Cima"},
  {id: "dpad_down", name: "D-pad Baixo"},
  {id: "dpad_left", name: "D-pad Esquerda"},
  {id: "dpad_right", name: "D-pad Direita"}
];

const defaultTouchpadShortcuts = {
  swipeUp: "xbox_button",
  swipeDown: "game_menu",
  swipeLeft: "game_menu",
  swipeRight: "xbox_button",
  clickLeft: "view",
  clickRight: "view"
};

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
  keyMap: [...defaultKeyMap],
  touchpadShortcuts: {...defaultTouchpadShortcuts}
};
let lastSettingsVersion = 0;

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
  document.querySelectorAll('.tab-pane').forEach(el => {
    el.style.display = 'none';
    el.classList.remove('active');
  });
  document.querySelectorAll('.nav-tab').forEach(el => el.classList.remove('active'));
  const pane = document.getElementById('tab-' + name);
  if (pane) {
    pane.style.display = 'block';
    pane.classList.add('active');
  }
  const tabBtn = document.getElementById('navTab-' + name);
  if (tabBtn) tabBtn.classList.add('active');
  if (name === 'search') pollStreamingState();
  if (name === 'gamepad') {
    sendPadState(true);
    updateGamepadScale();
    setTimeout(updateGamepadScale, 10);
  }
  if (name === 'stream') {
    loadStreamMenuState();
    pollStats();
  }
}

function switchRemapSub(mode) {
  document.getElementById('subRemapPad').style.display = mode === 'pad' ? 'block' : 'none';
  document.getElementById('subRemapKey').style.display = mode === 'key' ? 'block' : 'none';
  const subTouch = document.getElementById('subRemapTouch');
  if (subTouch) subTouch.style.display = mode === 'touch' ? 'block' : 'none';
  document.getElementById('btnSubPad').classList.toggle('active', mode === 'pad');
  document.getElementById('btnSubKey').classList.toggle('active', mode === 'key');
  const btnTouch = document.getElementById('btnSubTouch');
  if (btnTouch) btnTouch.classList.toggle('active', mode === 'touch');
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

function renderTouchpadShortcutsGrid(containerId, idPrefix, tsObj, onChangeFn) {
  const grid = document.getElementById(containerId);
  if (!grid) return;
  grid.innerHTML = '';
  const ts = Object.assign({}, defaultTouchpadShortcuts, tsObj || {});
  touchpadTriggers.forEach(trig => {
    const item = document.createElement('div');
    item.className = 'map-item';
    const label = document.createElement('label');
    label.textContent = trig.label;
    const select = document.createElement('select');
    select.id = idPrefix + '_' + trig.key;
    touchpadActions.forEach((act, idx) => {
      const opt = document.createElement('option');
      opt.value = act.id;
      opt.textContent = act.name;
      if (ts[trig.key] === act.id || ts[trig.key] === idx) opt.selected = true;
      select.appendChild(opt);
    });
    select.addEventListener('change', () => {
      currentSettings.touchpadShortcuts = currentSettings.touchpadShortcuts || {...defaultTouchpadShortcuts};
      currentSettings.touchpadShortcuts[trig.key] = select.value;
      syncTouchpadSelects(trig.key, select.value);
      if (onChangeFn) onChangeFn(trig.key, select.value);
    });
    item.appendChild(label);
    item.appendChild(select);
    grid.appendChild(item);
  });
}

function syncTouchpadSelects(trigKey, val) {
  ['tp_remap_'].forEach(prefix => {
    const el = document.getElementById(prefix + trigKey);
    if (el && el.value !== String(val)) el.value = val;
  });
}

function readTouchpadShortcutsFromPrefix(prefix) {
  const out = {...defaultTouchpadShortcuts};
  touchpadTriggers.forEach(trig => {
    const el = document.getElementById(prefix + '_' + trig.key);
    if (el) out[trig.key] = el.value;
  });
  return out;
}

function renderAllTouchpadGrids(tsObj) {
  const ts = Object.assign({}, defaultTouchpadShortcuts, tsObj || currentSettings.touchpadShortcuts || {});
  currentSettings.touchpadShortcuts = ts;
  renderTouchpadShortcutsGrid('remapTouchpadShortcutsGrid', 'tp_remap', ts, null);
}

function resetTouchpadShortcuts() {
  currentSettings.touchpadShortcuts = {...defaultTouchpadShortcuts};
  renderAllTouchpadGrids(currentSettings.touchpadShortcuts);
  saveSettings();
}

function renderPadMap() {
  const grid = document.getElementById('padGrid');
  grid.innerHTML = '';
  const map = Array.isArray(currentSettings.padMap) ? currentSettings.padMap
            : (currentSettings.padMap && Array.isArray(currentSettings.padMap.map)) ? currentSettings.padMap.map
            : [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15];
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
  const touchpadShortcuts = readTouchpadShortcutsFromPrefix('tp_remap');
  fetch('/api/settings', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ padMap: padMap, keyMap: keyMap, touchpadShortcuts: touchpadShortcuts })
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
  }).then(r => r.json()).then(res => {
    if (res && res.ok) showToast('Aplicado na transmissão!');
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
    if (s.touchpadShortcuts) {
      renderAllTouchpadGrids(s.touchpadShortcuts);
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

function getReportParams() {
  const title = (document.getElementById('reportTitle')?.value || '').trim();
  const what = (document.getElementById('reportWhat')?.value || '').trim();
  return { title, what };
}

function openGitHubReport() {
  const { title, what } = getReportParams();
  const btn = document.getElementById('btnOpenReport');
  btn.disabled = true;
  btn.textContent = 'Gerando link...';
  fetch('/api/report-issue', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ title, what })
  }).then(r => r.json()).then(data => {
    btn.disabled = false;
    btn.textContent = '🚀 Abrir Relatório no GitHub';
    if (data && data.url) {
      window.open(data.url, '_blank');
      showToast('Formulário do GitHub aberto em nova aba!');
    } else {
      showToast('Erro ao obter link do relatório.');
    }
  }).catch(e => {
    btn.disabled = false;
    btn.textContent = '🚀 Abrir Relatório no GitHub';
    showToast('Falha na requisição: ' + e);
  });
}

function fallbackCopyText(text) {
  return new Promise((resolve, reject) => {
    try {
      const ta = document.createElement('textarea');
      ta.value = text;
      ta.style.position = 'fixed';
      ta.style.top = '0';
      ta.style.left = '0';
      ta.style.width = '2em';
      ta.style.height = '2em';
      ta.style.padding = '0';
      ta.style.border = 'none';
      ta.style.outline = 'none';
      ta.style.boxShadow = 'none';
      ta.style.background = 'transparent';
      ta.style.opacity = '0.01';
      document.body.appendChild(ta);
      ta.focus();
      ta.select();
      if (typeof ta.setSelectionRange === 'function') {
        ta.setSelectionRange(0, text.length);
      }
      let ok = false;
      try {
        ok = document.execCommand('copy');
      } catch (_) {
        ok = false;
      }
      document.body.removeChild(ta);
      if (ok) resolve(); else reject(new Error('execCommand copy failed'));
    } catch (e) { reject(e); }
  });
}

function copyTextToClipboard(text) {
  if (navigator.clipboard && window.isSecureContext && navigator.clipboard.writeText) {
    return navigator.clipboard.writeText(text).catch(() => fallbackCopyText(text));
  }
  return fallbackCopyText(text);
}

function copySanitizedReport() {
  const { title, what } = getReportParams();
  const btn = document.getElementById('btnCopyReport');
  btn.disabled = true;
  fetch('/api/report-issue', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ title, what })
  }).then(r => r.json()).then(data => {
    btn.disabled = false;
    if (data && data.excerpt) {
      const markdown = `### ${data.title || 'Relatório de Erro'}\n\n**Plataforma:** ${data.platform || 'PlayStation 5 (PPSA99810)'}\n**Versão:** v${data.release || '1.4.0'}\n**Firmware:** ${data.firmware || 'N/A'}\n\n**O que aconteceu:**\n${what || 'Relatório enviado via WebUI'}\n\n\`\`\`\n${data.excerpt}\n\`\`\``;
      copyTextToClipboard(markdown).then(() => {
        showToast('Relatório copiado para a área de transferência!');
      }).catch(() => {
        const p = document.getElementById('logPreview');
        if (p) {
          p.textContent = markdown;
          p.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
        }
        try {
          window.prompt('Copie o relatório abaixo (Ctrl+C / Toque longo):', markdown);
          showToast('Relatório preparado!');
        } catch (_) {
          showToast('Relatório preparado no campo abaixo para cópia!');
        }
      });
    } else {
      showToast('Erro ao gerar relatório.');
    }
  }).catch(e => {
    btn.disabled = false;
    showToast('Falha ao copiar: ' + e);
  });
}

function previewReport() {
  const { title, what } = getReportParams();
  const p = document.getElementById('logPreview');
  p.textContent = 'Carregando resumo do relatório sanitizado...';
  fetch('/api/report-issue', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ title, what })
  }).then(r => r.json()).then(data => {
    if (data && data.excerpt) {
      p.textContent = `=== RELATÓRIO PREPARADO ===\nTítulo: ${data.title}\nPlataforma: ${data.platform}\nVersão: v${data.release}\nFirmware: ${data.firmware}\n\n${data.excerpt}`;
    } else {
      p.textContent = 'Nenhum dado retornado.';
    }
  }).catch(e => {
    p.textContent = 'Erro ao carregar pré-visualização: ' + e;
  });
}

function applyLoadedSettings(s) {
  currentSettings = Object.assign(currentSettings, s);
  if (s.settingsVersion !== undefined) lastSettingsVersion = s.settingsVersion;
  if (s.query !== undefined && document.activeElement !== searchInput) searchInput.value = s.query;
  if (s.tab) {
    currentSearchTab = s.tab;
  }
  if (s.filters) {
    if (s.filters.wishlist !== undefined) {
      currentFilters.wishlist = !!s.filters.wishlist;
      const el = document.getElementById('chipWishlist');
      if (el) el.classList.toggle('active', currentFilters.wishlist);
    }
    if (s.filters.free !== undefined) {
      currentFilters.free = !!s.filters.free;
      const el = document.getElementById('chipFree');
      if (el) el.classList.toggle('active', currentFilters.free);
    }
    if (s.filters.cheapest !== undefined) {
      currentFilters.cheapest = !!s.filters.cheapest;
      const el = document.getElementById('chipCheapest');
      if (el) el.classList.toggle('active', currentFilters.cheapest);
    }
    if (s.filters.mouseKeyboard !== undefined) {
      currentFilters.mouseGP = !!s.filters.mouseKeyboard;
      currentFilters.mouseLib = !!s.filters.mouseKeyboard;
      const elGP = document.getElementById('chipMouseGP');
      if (elGP) elGP.classList.toggle('active', currentFilters.mouseGP);
      const elLib = document.getElementById('chipMouseLib');
      if (elLib) elLib.classList.toggle('active', currentFilters.mouseLib);
    }
    if (s.filters.plan !== undefined) {
      currentFilters.plan = s.filters.plan;
      const sel = document.getElementById('filterPlan');
      if (sel) sel.value = s.filters.plan;
      const wrap = document.getElementById('chipSelectPlan');
      if (wrap) wrap.classList.toggle('active', s.filters.plan > 0);
    }
    if (s.filters.console !== undefined) {
      currentFilters.consoleGP = s.filters.console;
      currentFilters.consoleLib = s.filters.console;
      const selGP = document.getElementById('filterConsoleGP');
      if (selGP) selGP.value = s.filters.console;
      const selLib = document.getElementById('filterConsoleLib');
      if (selLib) selLib.value = s.filters.console;
      const wrapGP = document.getElementById('chipSelectConsoleGP');
      if (wrapGP) wrapGP.classList.toggle('active', s.filters.console > 0);
      const wrapLib = document.getElementById('chipSelectConsoleLib');
      if (wrapLib) wrapLib.classList.toggle('active', s.filters.console > 0);
    }
    if (s.filters.mode !== undefined) {
      currentFilters.modeGP = s.filters.mode;
      currentFilters.modeLib = s.filters.mode;
      const selGP = document.getElementById('filterModeGP');
      if (selGP) selGP.value = s.filters.mode;
      const selLib = document.getElementById('filterModeLib');
      if (selLib) selLib.value = s.filters.mode;
      const wrapGP = document.getElementById('chipSelectModeGP');
      if (wrapGP) wrapGP.classList.toggle('active', s.filters.mode > 0);
      const wrapLib = document.getElementById('chipSelectModeLib');
      if (wrapLib) wrapLib.classList.toggle('active', s.filters.mode > 0);
    }
    if (s.filters.language !== undefined) {
      currentFilters.langGP = s.filters.language;
      currentFilters.langLib = s.filters.language;
      const selGP = document.getElementById('filterLangGP');
      if (selGP) selGP.value = s.filters.language;
      const selLib = document.getElementById('filterLangLib');
      if (selLib) selLib.value = s.filters.language;
      const wrapGP = document.getElementById('chipSelectLangGP');
      if (wrapGP) wrapGP.classList.toggle('active', s.filters.language > 0);
      const wrapLib = document.getElementById('chipSelectLangLib');
      if (wrapLib) wrapLib.classList.toggle('active', s.filters.language > 0);
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
  renderAllTouchpadGrids(s.touchpadShortcuts);
}

function loadAllSettingsFromServer() {
  fetch('/api/settings').then(r => r.json()).then(applyLoadedSettings).catch(() => {
    renderPadMap();
    renderKeyMap();
    renderAllTouchpadGrids(currentSettings.touchpadShortcuts);
  });
}
loadAllSettingsFromServer();

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
  const prev = activePlayer;
  activePlayer = p;
  for (let i = 1; i <= 4; i++) {
    const btn = document.getElementById('btnPlayer' + i);
    if (btn) btn.classList.toggle('active', p === i);
  }
  const statusEl = document.getElementById('gamepadStatus');
  if (statusEl) {
    statusEl.textContent = 'Controle ativo para Jogador ' + p + (p === 1 ? ' (Principal)' : ' (Co-op Local)');
  }
  sendPadState(true, prev);
}

function toggleGamepadFullscreen() {
  const card = document.getElementById('gamepadCard');
  if (!card) return;
  const isFs = card.classList.contains('gamepad-fullscreen') || document.fullscreenElement === card || document.webkitFullscreenElement === card;
  if (!isFs) {
    card.classList.add('gamepad-fullscreen');
    if (card.requestFullscreen) {
      card.requestFullscreen().catch(() => {});
    } else if (card.webkitRequestFullscreen) {
      try { card.webkitRequestFullscreen(); } catch (_) {}
    }
    if (screen.orientation && screen.orientation.lock) {
      screen.orientation.lock('landscape').catch(() => {});
    }
  } else {
    card.classList.remove('gamepad-fullscreen');
    if (document.fullscreenElement || document.webkitFullscreenElement) {
      if (document.exitFullscreen) document.exitFullscreen().catch(() => {});
      else if (document.webkitExitFullscreen) {
        try { document.webkitExitFullscreen(); } catch (_) {}
      }
    }
    if (screen.orientation && screen.orientation.unlock) {
      try { screen.orientation.unlock(); } catch (_) {}
    }
  }
  setTimeout(updateGamepadScale, 20);
  setTimeout(updateGamepadScale, 150);
}

document.addEventListener('fullscreenchange', () => {
  const card = document.getElementById('gamepadCard');
  if (card && !document.fullscreenElement && !document.webkitFullscreenElement) {
    card.classList.remove('gamepad-fullscreen');
  }
  setTimeout(updateGamepadScale, 30);
});
document.addEventListener('webkitfullscreenchange', () => {
  const card = document.getElementById('gamepadCard');
  if (card && !document.fullscreenElement && !document.webkitFullscreenElement) {
    card.classList.remove('gamepad-fullscreen');
  }
  setTimeout(updateGamepadScale, 30);
});

function sendPadState(activeSwitch = false, prevPlayer = 0) {
  const payload = { player: activePlayer, prevPlayer: prevPlayer, activeSwitch: !!activeSwitch, ...padState };
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
    sendPadState(false);
  });
}

function bindPadButtons() {
  document.querySelectorAll('[data-btn]').forEach(el => {
    const key = el.getAttribute('data-btn');
    const isTrigger = key === 'triggerL2' || key === 'triggerR2';
    const activeTouches = new Set();

    const setDown = () => {
      if (isTrigger) padState[key] = 1.0;
      else padState[key] = true;
      el.classList.add('pressed');
      if (navigator.vibrate) {
        try { navigator.vibrate(14); } catch (_) {}
      }
      sendPadState(false);
    };

    const setUp = () => {
      if (isTrigger) padState[key] = 0.0;
      else padState[key] = false;
      el.classList.remove('pressed');
      sendPadState(false);
    };

    el.addEventListener('touchstart', (e) => {
      e.preventDefault();
      for (let i = 0; i < e.changedTouches.length; i++) {
        activeTouches.add(e.changedTouches[i].identifier);
      }
      setDown();
    }, { passive: false });

    const onTouchEnd = (e) => {
      e.preventDefault();
      for (let i = 0; i < e.changedTouches.length; i++) {
        activeTouches.delete(e.changedTouches[i].identifier);
      }
      if (activeTouches.size === 0) setUp();
    };

    el.addEventListener('touchend', onTouchEnd, { passive: false });
    el.addEventListener('touchcancel', onTouchEnd, { passive: false });
    el.addEventListener('mousedown', (e) => { e.preventDefault(); setDown(); });
    el.addEventListener('mouseup', (e) => { e.preventDefault(); setUp(); });
    el.addEventListener('mouseleave', () => { if (activeTouches.size === 0 && el.classList.contains('pressed')) setUp(); });
  });
}

function setupStick(zoneId, headId, stickKeyX, stickKeyY) {
  const zone = document.getElementById(zoneId);
  const head = document.getElementById(headId);
  if (!zone || !head) return;

  let activeTouchId = null;

  function updateStick(clientX, clientY) {
    const rect = zone.getBoundingClientRect();
    const maxR = Math.max(28, (rect.width - head.offsetWidth) / 2);
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
    sendPadState(false);
  }

  zone.addEventListener('touchstart', (e) => {
    e.preventDefault();
    if (activeTouchId === null && e.changedTouches.length > 0) {
      activeTouchId = e.changedTouches[0].identifier;
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
        resetStick();
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
setupStick('leftStickZone', 'leftStickHead', 'leftStickX', 'leftStickY');
setupStick('rightStickZone', 'rightStickHead', 'rightStickX', 'rightStickY');

// Periodic streaming state poll and PS5 <-> WebUI settings version sync
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
    if (data.settingsVersion !== undefined && data.settingsVersion !== lastSettingsVersion) {
      lastSettingsVersion = data.settingsVersion;
      applyLoadedSettings(data);
      loadStreamMenuState();
    }
  }).catch(() => {});
}
setInterval(pollStreamingState, 1500);
pollStreamingState();

// Gamepad keepalive while gamepad tab is open (300ms ensures held buttons never drop)
setInterval(() => {
  const gpPane = document.getElementById('tab-gamepad');
  if (gpPane && gpPane.style.display !== 'none') {
    sendPadState(false);
  }
}, 300);

function updateGamepadScale() {
  const container = document.getElementById('gamepadScaleContainer');
  const card = document.getElementById('gamepadCard');
  if (!container || !card) return;
  const isFs = card.classList.contains('gamepad-fullscreen') || document.fullscreenElement === card || document.webkitFullscreenElement === card;
  const isLandscape = window.innerWidth > window.innerHeight;

  if (isFs) {
    const w = window.innerWidth;
    const h = window.innerHeight;
    const availW = container.clientWidth ? Math.max(200, container.clientWidth - 8) : (w - 28);
    const availH = container.clientHeight ? Math.max(150, container.clientHeight - 8) : (h - 20);
    const scale = Math.min(1.2, Math.max(0.4, Math.min(availW / 915, availH / 412)));
    document.documentElement.style.setProperty('--gp-fs-scale', scale.toFixed(3));
    document.documentElement.style.setProperty('--gp-scale', scale.toFixed(3));
    return;
  }

  let availW = container.clientWidth;
  if (!availW) {
    const padding = window.innerWidth <= 600 ? 16 : 32;
    availW = Math.max(0, window.innerWidth - padding);
  }

  if (!isLandscape) {
    const targetW = Math.max(0, availW - 16);
    const scale = Math.min(1, Math.max(0.4, targetW / 412));
    document.documentElement.style.setProperty('--gp-scale', scale.toFixed(3));
  } else {
    const targetW = Math.max(0, availW - 16);
    const availH = window.innerHeight;
    const targetH = Math.max(120, availH - 78);
    const scale = Math.min(1, Math.max(0.35, Math.min(targetW / 915, targetH / 412)));
    document.documentElement.style.setProperty('--gp-scale', scale.toFixed(3));
  }
}
window.addEventListener('resize', updateGamepadScale);
window.addEventListener('orientationchange', () => setTimeout(updateGamepadScale, 50));
window.addEventListener('DOMContentLoaded', updateGamepadScale);
if (window.ResizeObserver) {
  const ro = new ResizeObserver(() => updateGamepadScale());
  const cardEl = document.getElementById('gamepadCard');
  if (cardEl) ro.observe(cardEl);
}

// Hash & route navigation support: #gamepad, #gamepad-fs, #logs, #settings, /boost, /gamepad, etc.
function checkHashNav() {
  const p = (location.pathname || '').toLowerCase();
  if (p.includes('/boost') || p.includes('/gamepad')) {
    switchTab('gamepad');
  }
  const h = (location.hash || '').replace('#', '');
  if (!h) return;
  if (h === 'gamepad-fs') {
    switchTab('gamepad');
    const card = document.getElementById('gamepadCard');
    if (card && !card.classList.contains('gamepad-fullscreen')) {
      toggleGamepadFullscreen();
    }
  } else if (document.getElementById('tab-' + h)) {
    switchTab(h);
  }
}
window.addEventListener('hashchange', checkHashNav);
checkHashNav();
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
#ifdef SIGPIPE
    ::signal(SIGPIPE, SIG_IGN);
#endif
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
        slot = p - 1;
    }
    slot = std::clamp(slot, 0, 3);
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
#ifdef SO_NOSIGPIPE
    int nosig = 1;
    ::setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#endif

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
    if (method != "HEAD") {
        response += res.body;
    }

    size_t sent = 0;
    while (sent < response.size()) {
        ssize_t w = ::send(clientFd, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
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
                            if ((*j).has("activeSwitch") && (*j)["activeSwitch"].asBool()) {
                                switchVirtualGamepadSlot(slot);
                            }
                            setVirtualGamepad(slot, s);
                            if (s_hooks.injectGamepad) s_hooks.injectGamepad(slot, s);
                        }
                    }
                } else if (frame.opcode == ws::Opcode::Binary) {
                    if (frame.payload.size() >= 13) {
                        int slot = frame.payload[0] & 0x03;
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
    uint64_t lastSeenMs = 0;
    uint64_t lastInputMs = 0;
};
std::mutex s_vpadMutex;
VirtualGamepadSlot s_virtualPads[4];

bool hasGamepadInput(const input::ControllerState& s) {
    return s.btnA || s.btnB || s.btnX || s.btnY ||
           s.dpadUp || s.dpadDown || s.dpadLeft || s.dpadRight ||
           s.btnL1 || s.btnR1 || s.btnL3 || s.btnR3 ||
           s.btnTouchpad || s.btnOptions || s.btnNexus ||
           s.triggerL2 > 0.05f || s.triggerR2 > 0.05f ||
           std::abs(s.leftStickX) > 0.08f || std::abs(s.leftStickY) > 0.08f ||
           std::abs(s.rightStickX) > 0.08f || std::abs(s.rightStickY) > 0.08f;
}
}  // namespace

bool virtualGamepadState(int playerIndex, input::ControllerState& outState, bool& isConnected) {
    if (playerIndex < 0 || playerIndex >= 4) {
        isConnected = false;
        outState = {};
        return false;
    }
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    uint64_t now = platform::nowMs();
    uint64_t diff = now > s_virtualPads[playerIndex].lastSeenMs ? now - s_virtualPads[playerIndex].lastSeenMs : 0;
    isConnected = (s_virtualPads[playerIndex].lastSeenMs > 0 && diff < 4000);
    if (diff < 2000) {
        outState = s_virtualPads[playerIndex].state;
    } else {
        outState = {};
    }
    return isConnected;
}

void setVirtualGamepad(int playerIndex, const input::ControllerState& state) {
    if (playerIndex < 0 || playerIndex >= 4) return;
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    uint64_t now = platform::nowMs();
    bool firstSeen = (s_virtualPads[playerIndex].lastSeenMs == 0);
    s_virtualPads[playerIndex].state = state;
    s_virtualPads[playerIndex].lastSeenMs = now;
    if (firstSeen || hasGamepadInput(state)) {
        s_virtualPads[playerIndex].lastInputMs = now;
    }
}

void touchVirtualGamepad(int playerIndex) {
    if (playerIndex < 0 || playerIndex >= 4) return;
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    uint64_t now = platform::nowMs();
    s_virtualPads[playerIndex].lastSeenMs = now;
    s_virtualPads[playerIndex].lastInputMs = now;
}

void switchVirtualGamepadSlot(int newSlot) {
    if (newSlot < 0 || newSlot >= 4) return;
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    for (int i = 0; i < 4; ++i) {
        if (i != newSlot) {
            s_virtualPads[i] = {};
        }
    }
    uint64_t now = platform::nowMs();
    s_virtualPads[newSlot].lastSeenMs = now;
    s_virtualPads[newSlot].lastInputMs = now;
}

uint64_t virtualGamepadActiveAt(int playerIndex) {
    if (playerIndex < 0 || playerIndex >= 4) return 0;
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    return s_virtualPads[playerIndex].lastInputMs;
}

void resetVirtualGamepads() {
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    for (auto& slot : s_virtualPads) slot = {};
}

const char* webServerIndexHtml() {
    return kIndexHtml;
}

void setWebServerHooks(WebServerHooks hooks) {
    s_hooks = std::move(hooks);
}

WebServer::HttpResponse WebServer::handleRequest(const std::string& method, const std::string& path,
                                                 const std::string& body) {
    if (method == "OPTIONS") {
        return {204, "text/plain", "", ""};
    }

    if (method == "GET" || method == "HEAD") {
        if (path == "/" || path == "/index.html" || path == "/boost" || path == "/boost/" || path == "/gamepad" || path == "/gamepad/") {
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
                root.set("touchpadShortcuts", input::touchpadShortcutsJson(input::defaultTouchpadShortcuts()));
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
        if (path == "/api/report-issue" || path.rfind("/api/report-issue?", 0) == 0) {
            std::string rawLog;
            platform::readFile(platform::dataDir() + "/xcloud.log", rawLog);
            std::vector<std::string> privWords;
            for (int i = 0; i < input::kMaxPads; ++i) {
                std::string u = input::padUserName(i);
                if (!u.empty()) privWords.push_back(std::move(u));
            }

            report::Info info;
            info.title = "Relatório via WebUI";
            info.what = "Enviado através do painel WebUI do PSBox";
            info.release = XC_APP_VERSION;
            info.firmware = platform::firmwareVersion();
            info.platform = platform::platformName();
            std::string platHeader = "[Platform: " + info.platform + "]\n";
            info.log = platHeader + report::sanitize(report::logExcerpt(rawLog, false), privWords);
            std::string url = report::issueUrl(info);

            json::Value resp = json::Value::object();
            resp.set("ok", true);
            resp.set("url", url);
            resp.set("title", info.title);
            resp.set("platform", info.platform);
            resp.set("release", info.release);
            resp.set("firmware", info.firmware);
            resp.set("excerpt", info.log);
            return {200, "application/json; charset=utf-8", "", resp.dump()};
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
        if (path == "/api/report-issue") {
            std::string title = "Relatório via WebUI";
            std::string what = "Enviado através do painel WebUI do PSBox";
            auto j = json::parse(body);
            if (j && j->isObject()) {
                if ((*j).has("title") && (*j)["title"].isString() && !(*j)["title"].asString().empty()) {
                    title = (*j)["title"].asString();
                }
                if ((*j).has("what") && (*j)["what"].isString() && !(*j)["what"].asString().empty()) {
                    what = (*j)["what"].asString();
                }
            }
            std::string rawLog;
            platform::readFile(platform::dataDir() + "/xcloud.log", rawLog);

            std::vector<std::string> privWords;
            for (int i = 0; i < input::kMaxPads; ++i) {
                std::string u = input::padUserName(i);
                if (!u.empty()) privWords.push_back(std::move(u));
            }

            report::Info info;
            info.title = report::shorten(title, 70);
            info.what = report::sanitize(what, privWords);
            info.release = XC_APP_VERSION;
            info.firmware = platform::firmwareVersion();
            info.platform = platform::platformName();
            std::string platHeader = "[Platform: " + info.platform + "]\n";
            info.log = platHeader + report::sanitize(report::logExcerpt(rawLog, false), privWords);
            std::string url = report::issueUrl(info);

            json::Value resp = json::Value::object();
            resp.set("ok", true);
            resp.set("url", url);
            resp.set("title", info.title);
            resp.set("platform", info.platform);
            resp.set("release", info.release);
            resp.set("firmware", info.firmware);
            resp.set("excerpt", info.log);
            return {200, "application/json; charset=utf-8", "", resp.dump()};
        }
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

            if ((*j).has("activeSwitch") && (*j)["activeSwitch"].asBool()) {
                switchVirtualGamepadSlot(slot);
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
