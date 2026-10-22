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

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace xc::app {

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
html, body { width: 100%; max-width: 100%; overflow-x: hidden; background: var(--bg); color: var(--text); padding-bottom: 40px; }
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
  <div class="nav-tab" id="navTab-remap" onclick="switchTab('remap')">🎮 Controles</div>
  <div class="nav-tab" id="navTab-logs" onclick="switchTab('logs')">📋 Logs</div>
</div>

<div class="content">
  <!-- TAB: SEARCH -->
  <div id="tab-search" class="tab-pane">
    <div class="card">
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
  applySearchTabUI(currentSearchTab);
  updateSearchStatusText();
  renderPadMap();
  renderKeyMap();
}).catch(e => {
  renderPadMap();
  renderKeyMap();
});
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

void WebServer::stop() {
    if (!running_.exchange(false)) return;
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
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
                handleClient(clientFd);
                ::close(clientFd);
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

    // Find body if present
    std::string body;
    auto hdrEnd = req.find("\r\n\r\n");
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

static WebServerHooks s_hooks;

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
        if (path == "/api/settings" || path == "/api/status") {
            json::Value root = json::Value::object();
            root.set("ok", true);
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
    }

    if (method == "POST") {
        if (path == "/api/search") {
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
