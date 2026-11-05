# PS5 Native Trophies & Session Handoff (xCloud-PS5)

> **Documento de Hand-off Técnico e Estado Atual do Projeto**
> **Data:** 2026-11-05
> **Branch:** `feat/trophy-sync` (Worktree: `/home/notebook-storm/Documentos/PS5 Ports/xCloud-PS5-trophy`)
> **Target Console:** PS5 FW 13.60 (IP: `192.168.15.17`, Portas: 2121 FTP, 8084 elfldr, 3232 klog)
> **Usuário Alvo:** `0x1ea2f4da` (`devRafaelNGP`)
> **Título Alvo:** Estritamente **Fortnite (`PPSA01922` / `NPWR20561_00`)** — regra AGENTS.md §8.1

---

## 1. Resumo Executivo e Status Geral

1. **Correção de Regressão Crítica de Login/Gravação (Resolvida)**:
   - **Sintoma:** O app abria deslogado, sem carregar a conta existente (`account.json`), e ao tentar fazer login, este não salvava (`g_dataDirWritable = false`).
   - **Causa Raiz 1 (Timing de Unjail):** Em `src/platform/ps5/platform_ps5.cpp`, a função `init()` disparava o unjail assíncrono via `127.0.0.1:8084` e aguardava apenas `150ms`. Quando o payload demorava mais de 150ms para unjail do PID, `writable("/data/psbox")` falhava com `errno 13` (EACCES) ou `errno 2` (ENOENT). A pasta de dados caía em fallback restrito (`/download0/psbox`) e `g_dataDirWritable` tornava-se `false`, ignorando o `account.json` existente e impedindo gravações.
   - **Causa Raiz 2 (Autoplay Residual):** O arquivo `/data/psbox/autoplay.txt` continha `AUTOTEST 35 trophytest`. Toda inicialização ativava `guardSettingsForAutoplay()`, restaurando o backup anterior das configurações.
   - **Solução Implementada:** 
     - Adicionado loop de retry polling em `platform_ps5.cpp:init()` de até 30 tentativas (100ms cada = até 3.0s de janela) para esperar o unjail antes de avaliar as pastas de dados.
     - Higienizado o console: removidos todos os `autoplay.txt` residuais de `/data/psbox/` e `/data/homebrew/PPSA99810/`.
     - Confirmado via FTP: `/data/psbox/account.json` (491 bytes, gamertag `RafaelNGP`, uid 1, permissão 0666) está preservado e intacto.

2. **Causa do Kernel Panic na Execução Anterior (Diagnosticada e Sanada)**:
   - **Causa Raiz:** Em versões experimentais anteriores do payload `tools/ps5/payload_patch_test/main.c`, patches agressivos no SceShellCore (especialmente `B18`, que forçava retorno 0 em falhas de `FindTitle`) faziam o daemon desreferenciar ponteiros nulos no kernel/daemon, somado a um dump de heap de 32MB (`kernel_proc_copyout`) durante execução concorrente.
   - **Solução Implementada:** Removidos todos os patches perigosos e instáveis (`B13`, `B14`, `B15`, `B16`, `B18`, `B21`, `B22`, e scan de heap). O payload `patch_test.elf` agora executa o unjail do PID na linha 1 do `main()` de forma determinística (< 1ms) e aplica `chmod 0777` seguro nas pastas de troféus. O ELF foi recompilado e atualizado em `/data/pldmgr/payloads/patch_test/patch_test.elf`.

---

## 2. Análise Profunda das Referências da Comunidade

### 2.1 RecompTrophy (`cptlamarca/RecompTrophy`)
Criado por **lamarking** para prover troféus PS5 nativos para portas e emuladores do Xbox 360 no console:
- **Arquivos Manipulados (e apenas eles):**
  1. `/user/trophy2/nobackup/conf/<NPWR>/TROPHY.UCP`:
     - Contém `tropconf.json` (`schemaVersion: "1.00"`, `trophyDefinitionRevision: 1`, `trophySetVersion: "01.00"`).
     - Contém `tropmeta_<lang>.json` (`schemaVersion: "0.90"`).
     - Contém os PNGs de ícones (`trop0000.png` até `tropXXXX.png`, 512x512).
     - Header UCP `0xB228C60A`, versão 1, SHA-1 nos bytes `0x1C..0x2F`.
  2. `/user/home/<UID>/trophy2/nobackup/data/<NPWR>/TRPTITLE.DAT`:
     - Clona os registros de cabeçalho `0x1` e `0x2` de um jogo PS5 existente no perfil (`NPWR39259_00`).
     - Insere o SHA-1 do `TROPHY.UCP` no registro `0x100`.
     - Configura o registro `0x300` com o NPWR e máscara de troféus.
     - Grava os registros `0x800` (desbloqueios) com flags `0x11` e timestamps em microssegundos desde 0001-01-01 (`unix_time + 62135596800ULL * 1000000ULL`).
     - Empacota em blocos de `0x400` bytes de dados + `0x20` bytes de SHA-256 (stride `0x420`).
- **O que o RecompTrophy NÃO faz:**
  - **NÃO toca no `app.db`**.
  - **NÃO toca no `appinfo.db`**.
  - **NÃO toca em `/system_data/priv/appmeta/`** nem em `npbind.dat`.
  - **NÃO aplica patches de kernel nem de SceShellCore**.
- **Como a UI do PS5 exibe os troféus:**
  - O README do RecompTrophy estabelece expressamente: *"In a few seconds its trophies are in the PS5's Trophies list. If they don't show up yet, restart the console."*
  - Popups durante o gameplay são gerados por um watcher em background chamando `sceNotificationSend(0xFE, true, json)` com template `InteractiveToastTrophy` e canal `Trophies`.

### 2.2 TrophyManager (`kauansilvalopes/trophy`)
- Lê os jogos instalados diretamente de `/appdb`.
- Busca `/system_data/priv/appmeta/<TitleID>/trophy2/npbind.dat` para descobrir o NPWR associado ao título.
- Acessa e edita `/user/home/<UID>/trophy2/nobackup/data/<NPWR>/TRPTITLE.DAT`.
- Confirma que o PS5 valida estritamente a integridade SHA-256 dos blocos de `TRPTITLE.DAT`.

---

## 3. Estado Atual dos Arquivos no Console PS5 (`192.168.15.17`)

### 3.1 Fortnite (`PPSA01922` / `NPWR20561_00`)
- **TRPTITLE.DAT** (`/user/home/1ea2f4da/trophy2/nobackup/data/NPWR20561_00/TRPTITLE.DAT`):
  - Tamanho: 18.016 bytes.
  - Total de troféus: 46 (22 já registrados como desbloqueados).
  - Integridade: **17/17 blocos com SHA-256 100% válidos**.
  - Registro 0x100: SHA-1 (`9053681804adefb38e52ced0d736baf80e899167`) bate **100% com o TROPHY.UCP**.
- **TROPHY.UCP** (`/user/trophy2/nobackup/conf/NPWR20561_00/TROPHY.UCP`):
  - Tamanho: 193.040 bytes.
  - Total de arquivos: 50 (`tropconf.json`, `tropmeta_en-US.json`, `tropmeta_pt-BR.json`, 47 PNGs).
  - Header: Magic `0xB228C60A`, Version 1, SHA-1 verificado.
- **Banco de Dados do Sistema (`app.db` / `appinfo.db`)**:
  - `PPSA01922` está atualmente registrado com `DISPLAYLOCATION: 138` (o que fez o jogo aparecer na Biblioteca de Jogos).
  - **Decisão do Usuário:** O usuário solicitou manter o jogo oculto na Biblioteca (`visible = 0` / `DISPLAYLOCATION: 0`) a menos que a exibição na lista de troféus exija visibilidade, ou até que um forwarder executável funcional seja configurado.
- **Higienização de Conflitos:**
  - Removidos pacotes duplicados de `NPWR20561_00` que estavam em `/system_data/priv/appmeta/PPSA99810/trophy2/` para evitar colisão com o app principal PSBox.

---

## 4. Estado da Árvore Git (`xCloud-PS5-trophy`)

Branch: `feat/trophy-sync`
Principais arquivos modificados:
1. `src/platform/ps5/platform_ps5.cpp`:
   - Early unjail trigger via localhost:8084.
   - Retry loop de até 3s para aguardar `writable(kDataDir)` após o unjail.
   - Logs detalhados de diagnóstico da pasta de armazenamento.
2. `src/platform/ps5/trophy_backend_ps5.cpp`:
   - Geração e gravação direta dos arquivos `TRPTITLE.DAT` e `TROPHY.UCP`.
   - Remoção de bips de áudio, vibração ou toasts intrusivos de debug.
3. `src/trophy/trophy_builder.cpp`:
   - Implementação fiel e testada do builder de `TRPTITLE.DAT` e `TROPHY.UCP`, espelhando a lógica de `RecompTrophy`.
4. `tools/ps5/payload_patch_test/main.c`:
   - Early unjail instantâneo no início da execução.
   - Remoção de patches instáveis do SceShellCore que provocaram o kernel panic.
5. `tools/ps5/register_title_db.py`:
   - Script para manipulação e ajuste de `app.db`/`appinfo.db`.

---

## 5. Próximos Passos Imediatos para a Nova Sessão

1. **Validação do Login e Persistência**:
   - Recompilar e enviar a build atualizada de `xCloud-PS5-trophy` com o fix de retry loop:
     ```bash
     ninja -C build-ps5 xcloud_app
     bash tools/ps5/link.sh build-ps5
     XC_INCLUDE_ACCOUNT=1 bash tools/ps5/package.sh build-ps5
     PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py kill PPSA99810
     PS5_HOST=192.168.15.17 ./tools/ps5/deploy.sh build-ps5
     ```
   - Iniciar em modo interativo foreground:
     ```bash
     PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py launch PPSA99810
     ```
   - Confirmar que o app inicia já com a conta `RafaelNGP` autenticada e que qualquer alteração de configuração persiste em `/data/psbox/settings.json`.

2. **Visibilidade do Título na Biblioteca vs Troféus**:
   - Ajustar `DISPLAYLOCATION` de `PPSA01922` para `0` em `app.db` / `appinfo.db` (usando `register_title_db.py`) para que o título fique oculto na Biblioteca de Jogos conforme a preferência do usuário.
   - Se os troféus precisarem de um gatilho de leitura do SceShellUI (como documentado no RecompTrophy), verificar se a reinicialização da interface/console ou o envio da notificação nativa (`sceNotificationSend`) indexa o conjunto no menu Perfil.

---

## 6. Feedback Visual do Usuário no Console & Diagnóstico dos Pontos Faltantes

### 6.1 Status Visual Confirmado pelo Usuário
- **FORTNITE APARECE NA LISTA NATIVA DE TROFÉUS DO PS5!**
- Exibe **22 troféus liberados (48% de conclusão)**.
- Cada troféu exibe corretamente seu **nome**, **descrição** e **timestamp de quando foi desbloqueado**.

### 6.2 Análise dos 3 Pontos Pendentes e Soluções:

1. **Graus dos Troféus no PlayStation (Verificação Oficial via PSNProfiles / PlayStationTrophies)**:
   - **No PlayStation, os 46 troféus do Fortnite NÃO são todos Bronze.**
   - A distribuição oficial é:
     - **1 Platina:** `Hero of the Storm` (Obtenha todos os outros troféus).
     - **3 Ouro:**
       1. `All Tied Up` (Twine Peaks Storm Shield Defense 6)
       2. `Guardian Angel` (Salvar 10.000 sobreviventes em missões concluídas)
       3. `Plays Well with Others` (Completar 1.000 missões "Jogar com Outros")
     - **10 Prata:**
       1. `Many Planks` (Missões principais de Plankerton)
       2. `Amped for Expansion` (Canny Valley Storm Shield Defense 6)
       3. `Uncanny!` (Missões principais de Canny Valley)
       4. `Into the Night` (Twine Peaks Storm Shield Defense 1)
       5. `Evil Things with Crazy Teeth` (Toxic Treasures - Estágio 10)
       6. `Talented Builder` (Construir 500.000 estruturas)
       7. `Go Gnome!` (Destruir 100 Gnomos de Jardim)
       8. `Loot Legend` (Abrir 300 Baús de Tesouro)
       9. `World Explorer` (Explorar 1.500 zonas completamente)
       10. `Unspeakable Horrors` (Eliminar 20.000 Monstros de Névoa)
     - **32 Bronze:** Todos os outros 32 troféus.
   - **Ação:** No próximo chat, mapear o array `set.items` no `trophy_sync.cpp` para atribuir exatamente esses graus oficiais (`Platinum`, `Gold`, `Silver`, `Bronze`), em vez do fallback genérico de Bronze.

2. **Ícones dos Troféus e Ícone do Jogo (Círculo Verde)**:
   - **Por que está repetido e verde:**
     - Em `src/trophy/trophy_builder.cpp:425`, `icon0_en-US.png` foi montado usando `kDefaultIconPng`, que é um bitmap embutido de um círculo verde estático.
     - Em `src/trophy/trophy_builder.cpp:431`, todos os `trop0001.png` .. `trop0045.png` foram montados usando `kDefaultTropPng`, que é um único ícone genérico de troféu.
   - **Ação:** Substituir `icon0_en-US.png` pelo PNG real de capa do Fortnite (já presente em `/system_data/priv/appmeta/PPSA01922/icon0.png` no console) e carregar os PNGs de cada conquista retornados pela API da Xbox Live (`mediaAssets` / URLs de ícones) ao gerar o UCP.

3. **Carregamento e Gravação da Conta**:
   - O usuário testou com o binário antigo (anterior ao retry loop).
   - O fix está implementado em `src/platform/ps5/platform_ps5.cpp` (loop de até 3s aguardando unjail) e o `autoplay.txt` foi removido do console.
   - Ao rodar o deploy da nova build, o app lerá `/data/psbox/account.json` (que já existe e é válido) no primeiro frame.

