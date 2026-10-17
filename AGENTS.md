# Diretrizes de Desenvolvimento e Deploy no PS5 (xCloud-PS5)

Este documento estabelece regras operacionais estritas para qualquer agente de IA trabalhando no repositório **xCloud-PS5** e no ambiente do console PlayStation 5.

---

## 1. Regra Fundamental: Zero Blind Deploys

- **NUNCA solicite ao usuário que teste no console alterações de UI ou comportamento sem validação prévia.**
- **NUNCA assuma que o layout de tela está correto apenas pelo código C++.** Cálculos de clipping, safe-area, matrizes de projeção e renderização Vulkan no PS5 podem divergir do esperado na tela.
- **Toda e qualquer alteração de UI deve ser verificada visualmente pelo agente** através de screenshots capturadas pelo console antes de considerar a tarefa concluída ou entregar para o usuário.

---

## 2. Fluxo Obrigatório de Validação Autônoma de UI

Para qualquer alteração em arquivos de interface (`src/app_ui.cpp`, `src/app/autoplay.cpp`, shaders, renderizadores 2D):

1. **Parar a aplicação em execução**:
   ```bash
   PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py kill PPSA99810
   ```
2. **Compilar e Empacotar**:
   ```bash
   ninja -C build-ps5 xcloud_app
   bash tools/ps5/link.sh build-ps5
   XC_INCLUDE_ACCOUNT=1 bash tools/ps5/package.sh build-ps5
   ```
3. **Enviar a nova build ao console**:
   ```bash
   PS5_HOST=192.168.15.17 ./tools/ps5/deploy.sh build-ps5
   ```
4. **Configurar teste autônomo (`autoplay.txt`)**:
   - Enviar `/data/psbox/autoplay.txt` via FTP (desde a v0.9.7 os dados do app, logs e capturas `.ppm` ficam em `/data/psbox`; o app continua instalado em `/data/homebrew/PPSA99810`).
   - **Regra obrigatória para testes de UI**: Sempre utilize o prefixo `AUTOTEST` (ex: `AUTOTEST 30 gamesettingstest`, `AUTOTEST 30 librarytest`, etc.) para testes de telas/modais sem streaming. NUNCA use nomes arbitrários no primeiro argumento que possam ser interpretados como IDs de jogos pelo loop de streaming (`worker.cpp`).
   - Remover capturas `.ppm` antigas antes de iniciar.
5. **Iniciar a aplicação remotamente**:
   ```bash
   PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py launch PPSA99810
   ```
6. **Coletar e Inspecionar os Resultados Visuais**:
   - Baixar os arquivos `.ppm` (`ui.ppm`, `grid1.ppm`, `grid2.ppm`, `launch.ppm`, etc.) via FTP para um diretório local temporário.
   - Converter `.ppm` para `.png` utilizando Python (`PIL.Image`).
   - Usar a ferramenta `view_file` para analisar visualmente a imagem gerada.
   - Verificar conformidade: clipping, centralização, padding, corte de texto, contraste e consistência de grade.
7. **Limpeza e Restauração de Modo Interativo**:
   - Excluir `/data/psbox/autoplay.txt` via FTP para que a aplicação não inicialize em modo autoplay caso o usuário queira usá-la interativamente.
   - Matar e reiniciar o app se for entregar o controle ao usuário:
     ```bash
     PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py kill PPSA99810
     PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py launch PPSA99810
     ```

---

## 3. Higienização Mandatória do Console

- O arquivo `autoplay.txt` nunca deve permanecer no console após a finalização de testes automatizados.
- Se a aplicação estiver em execução e você precisar enviar novos arquivos, **SEMPRE mate o processo primeiro** (`kill PPSA99810`) antes de enviar o binário sobre FTP para evitar arquivos corrompidos ou locks em `eboot.bin`.

---

## 4. Preservação de Código e Prevenção de Regressões

- **NUNCA execute `git restore .` ou `git checkout -- .` de forma indiscriminada.**
- Antes de qualquer descarte ou reversão de alterações:
  - Inspecione `git diff` detalhadamente.
  - Se houver trabalho em andamento que precise ser isolado, utilize `git stash` ou crie uma branch temporária de segurança.
- Mantenha commits atômicos, focados e com mensagens claras.

---

## 5. Gestão de Contexto e Uso de Subagentes

Para evitar degradação de desempenho, latência alta e manter a interação ágil e leve:

- **Delegação Mandatória de Tarefas Pesadas**:
  - **Pesquisa e Varredura de Código**: Delegar para subagentes (`research` ou `self`) tarefas que envolvam ler múltiplos arquivos, buscar padrões no codebase ou analisar grandes trechos de código/documentação.
  - **Análise de Logs Extensos**: Usar subagentes para processar ou depurar saídas longas (builds, WebRTC, traces), retornando apenas o diagnóstico e resumo executivo ao contexto principal.
  - **Inspeção de Imagens e Testes Intermediários**: Quando houver rotinas com múltiplas validações de telas ou passos intermediários de build, priorizar subagentes para evitar inflar o histórico principal com tokens de imagens e logs.
- **Saídas Enxutas no Contexto Principal**:
  - Evitar despejar comandos com saídas verbosas desnecessárias (usar filtros, flags resumidas ou subagentes).
  - O agente principal deve atuar na orquestração, decisão e interação com o usuário, preservando o contexto limpo e rápido.

---

## 6. Protocolo Obrigatório de Publicação de Releases (In-App Updater)

- **NUNCA crie uma release no GitHub sem anexar os assets assinados.** O in-app updater do console (`src/app/updater.cpp` e `src/app/worker.cpp`) rejeita qualquer versão que não possua tanto o pacote `.zip` quanto a assinatura criptográfica `.sig`.
- **Assets Mandatórios em TODA Release (`vX.Y.Z`)**:
  1. `PPSA99810.zip` (pacote limpo sem contas/tokens, empacotado sem `XC_INCLUDE_ACCOUNT`).
  2. `PPSA99810.zip.sig` (assinatura ECDSA gerada com `~/.config/psbox-release/signing-key.pem`).
  3. `PPSA99810.zip.sha256` (checksum para integridade).
- **Assets Opcionais do Linux (app desktop `psbox`)**, gerados com `tools/release.sh <X.Y.Z> --linux`:
  1. `PSBox-vX.Y.Z-linux-x86_64.tar.gz` (pasta `PSBox/` com `psbox` sem debug info, `lib/libSDL3.so.0`, `assets/`, `licenses/`; nunca dados de conta).
  2. `PSBox-vX.Y.Z-linux-x86_64.tar.gz.sig` (assinado com a **mesma** chave e o mesmo método do `PPSA99810.zip`).
  3. `PSBox-vX.Y.Z-linux-x86_64.tar.gz.sha256`.
  - Eles nunca substituem os 3 assets do PS5, que continuam obrigatórios. O app desktop não se atualiza sozinho: só avisa da versão nova e abre a página da release, e nunca oferece o `PPSA99810.zip`.
  - O tarball gerado localmente herda a glibc da máquina que compilou (o `release.sh` mostra qual). O job `linux-app` do CI compila no ubuntu-24.04 (glibc 2.39), a base mais ampla; os símbolos ficam em `symbols/vX.Y.Z/psbox.debug` (não versionado).
- **Fluxo Mandatório de Publicação**:
  1. Delegar o empacotamento e assinatura para subagente (preservando o contexto).
  2. Gerar o pacote através de `tools/release.sh <X.Y.Z>` ou do pipeline de empacotamento oficial.
  3. Publicar ou anexar com `gh release create vX.Y.Z PPSA99810.zip PPSA99810.zip.sig PPSA99810.zip.sha256 ...` (ou `gh release upload --clobber`), mais os 3 assets do Linux quando gerados com `--linux`.
  4. **Verificação Obrigatória**: Sempre executar `gh release view vX.Y.Z` e confirmar que todos os 3 assets do PS5 (e os 3 do Linux, se publicados) estão listados e públicos antes de considerar o release finalizado.

---

## 7. Multiplataforma (PS5, Linux, Windows)

- O **PS5 é a plataforma principal**; Linux e Windows rodam o mesmo núcleo do app (`src/app/app_core.cpp`) com backends próprios. O mapa completo está em `docs/multiplatform.md`.
- **Toda alteração deve manter Linux e Windows compilando.** Uma funcionalidade exclusiva do PS5 vira no-op controlado por capacidade no desktop (`display::caps()`, `shell::canSelfUpdate()`...) ou tem a opção escondida — nunca quebra o build nem trava o app.
- **NUNCA chame funções `sce*`, inclua headers do SDK do PS5 ou use `XCLOUD_PS5` (ou outro teste de PS5) fora dos arquivos de backend do PS5** (`*_ps5.cpp`, `src/platform/ps5/`, `src/display/gpu.cpp`). O script `tools/check-platform-boundaries.sh` verifica isso (também no CI).
- Antes de considerar uma alteração pronta, compile os alvos de desktop (`ninja -C build-test` ou `build-host`, incluindo `psbox` — o app SDL3, quando o SDL3 existe — e `psbox-headless`, e rode `xcloud-tests`; `psbox-headless`, `xcloud-cli` e `xcloud-tests` nunca dependem do SDL), o app do PS5 (`ninja -C build-ps5 xcloud_app` + `bash tools/ps5/link.sh build-ps5`) e rode o script de fronteiras. Um link de desenvolvimento grava os símbolos em `symbols/vX.Y.Z-dev`; só o `tools/release.sh` grava `symbols/vX.Y.Z` (os da release, usados para decodificar crashes dos usuários).
- O fluxo de validação por screenshots no console (seção 2) continua obrigatório para mudanças de UI no PS5.
- No desktop, `psbox --selftest <dir>` (com `SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy`) verifica janela, telas, vídeo, overlay, entrada e áudio sem conta nem rede; use-o (e olhe os PNGs) ao mexer no display, shell ou UI do desktop. Opções que o PC não suporta somem pela capacidade (`ui::UiCaps`); no PS5 todas continuam visíveis.
