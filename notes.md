<!-- LANG:EN -->
# PSBox Cloud Gaming v1.6.3

## ⚡ Streaming Stutter Elimination on PS5 (`g_trophyThread` Batch Pause)
- **Zero Background Sync Interference**: The background trophy worker thread (`g_trophyThread`) now completely pauses batch catalog discovery and achievement queries (`TaskType::BatchTitle`) during active cloud streaming sessions (`ctx.isStreaming()`).
- **Immediate Real-Time Polls**: Real-time achievement checks (`TaskType::RealtimePoll`) for the currently running game continue without disruption with 0ms preemption.
- **Graceful Non-Dropping Resume**: Retains ongoing batch sync tasks in scope and resumes smoothly upon exiting the game or returning to the home screen.

## 📖 Full Catalog Descriptions & Cache Migration (v3)
- **Precedence Inversion**: Catalog entries now strictly prioritize the complete, rich game overview (`ProductDescription`) over brief marketing slogans (`ProductDescriptionShort`).
- **Automated Cache Migration**: Bumped library cache version to `kCacheVersion = 3`. Cached entries with short or missing descriptions (< 80 chars) are automatically invalidated and rehydrated via Microsoft's `RemoteHighSapphire0` API in the background.

## 💡 20 Dynamic Launching Tips Across 6 Languages
- **Expanded Tips Catalog**: Increased loading screen tips from 8 to 20, providing useful insights on DualSense adaptive triggers & haptics, touchpad mouse emulation, in-game menu overlay, WebUI companion controllers, trophy synchronization, audio buffer tuning, and USB controllers.
- **Dynamic 7-Second Rotation & Random Seeding**: Tips rotate dynamically every 7 seconds during launch, seeded pseudo-randomly per session based on launch timestamp and title hash.
- **Complete 6-Language Localization**: Full translation parity across English, Brazilian Portuguese, Spanish, German, French, and Italian with verified string placeholders.

## 🩺 Reactive Diagnostics Restoration ("Verify Sign-In" & "Network Test")
- **Immediate Reactive State**: Diagnósticos modals now open with instant visual feedback, pre-populating test stages and ping regions with active spinners.
- **Non-Dropping Command Dispatch**: Fixed command race conditions with a retry loop in `app_core.cpp`, ensuring diagnostic requests are never dropped during worker state transitions.
- **Original Logic & Presentation Preserved**: 100% fidelity to the original diagnostic checks, STUN RFC 5389 UDP network probing, and Xbox Live authentication verification.

---

### 📦 Attached Files (English)

| File Name | Description |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.6.3.zip` | Official standalone ZIP package for PlayStation 5 (manual or homebrew manager installation). |
| `PSBoxCloudGaming[PS5]-v1.6.3.zip.sig` | ECDSA digital signature for verifying the PS5 standalone package. |
| `PSBoxCloudGaming[PS5]-v1.6.3.zip.sha256` | SHA256 checksum for the PS5 standalone package. |
| `PPSA99810.zip` | Canonical PS5 package required by the in-app auto-updater. |
| `PPSA99810.zip.sig` | Cryptographic signature for the canonical PS5 package (verified by in-app updater). |
| `PPSA99810.zip.sha256` | SHA256 checksum for the canonical PS5 package. |
| `PSBoxCloudGaming[PS4]-v1.6.3.pkg` | Installable FPKG package for PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.6.3.pkg.sha256` | SHA256 checksum for the PS4 package. |
| `PSBoxCloudGaming[Linux]-v1.6.3.AppImage` | Universal standalone executable for Linux distributions and Steam Deck (Desktop & Game Mode). |
| `PSBoxCloudGaming[Linux]-v1.6.3.AppImage.sha256` | SHA256 checksum for the Linux AppImage. |

---

### 🚀 How to Install (English)

#### 🎮 PlayStation 5
- **Homebrew App Store / Direct**: Download `PSBoxCloudGaming[PS5]-v1.6.3.zip`, extract to `/data/homebrew/PPSA99810/` on your PS5 (via FTP or USB), and launch from the homebrew launcher.
- **In-App Updater**: If you already have PSBox installed, simply check for updates in the settings menu; `PPSA99810.zip` will download, verify signatures, and install automatically.

#### 🎮 PlayStation 4
- Download `PSBoxCloudGaming[PS4]-v1.6.3.pkg`, copy it to the root of a USB drive (exFAT/FAT32), and install via *Settings > GoldHEN > Package Installer*.

#### 💻 Linux Desktop / Steam Deck
- Download `PSBoxCloudGaming[Linux]-v1.6.3.AppImage`, make it executable (`chmod +x PSBoxCloudGaming[Linux]-v1.6.3.AppImage`), and run it directly.
- **Steam Deck**: Add the `.AppImage` file as a Non-Steam Game in Steam Desktop Mode to launch directly in Game Mode.

<!-- LANG:PT -->
# PSBox Cloud Gaming v1.6.3

## ⚡ Eliminação de Engasgos de Streaming no PS5 (Pausa de Lotes em `g_trophyThread`)
- **Zero Interferência no Streaming**: A thread em segundo plano de troféus (`g_trophyThread`) agora pausa completamente a varredura em lote do catálogo e a busca de conquistas (`TaskType::BatchTitle`) durante sessões ativas de transmissão na nuvem (`ctx.isStreaming()`).
- **Checagem Imediata em Tempo Real**: As consultas em tempo real (`TaskType::RealtimePoll`) para o jogo em execução continuam ativas sem interrupção e com prioridade máxima (preempção imediata).
- **Retomada Suave sem Descarte**: Tarefas em lote em andamento são mantidas em escopo e retomam de forma transparente ao encerrar o jogo ou retornar à tela inicial.

## 📖 Descrições Completas no Catálogo & Migração de Cache (v3)
- **Inversão de Precedência**: As fichas de jogos agora priorizam estritamente a descrição rica e detalhada (`ProductDescription`) em vez do slogan promocional curto (`ProductDescriptionShort`).
- **Migração Automática do Cache**: Versão do cache da biblioteca elevada para `kCacheVersion = 3`. Jogos com descrições curtas ou ausentes (< 80 caracteres) são automaticamente reidratados em segundo plano via API `RemoteHighSapphire0` da Microsoft.

## 💡 20 Dicas Dinâmicas na Tela de Launching em 6 Idiomas
- **Catálogo de Dicas Expandido**: A tela de carregamento de jogos passou de 8 para 20 dicas dinâmicas, abordando recursos recentes como gatilhos adaptáveis e vibração háptica do DualSense, emulação de mouse pelo touchpad, menu de opções in-game, controles virtuais via WebUI/SmartGlass, sincronização de troféus, ajuste de buffers de áudio e compatibilidade com controles USB.
- **Rotação Dinâmica a Cada 7 Segundos & Semente Aleatória**: As dicas alternam a cada 7 segundos durante a inicialização, com semente pseudo-aleatória baseada no relógio do sistema e no hash do título.
- **Localização Completa em 6 Idiomas**: Paridade total de tradução em Português do Brasil, Inglês, Espanhol, Alemão, Francês e Italiano com todos os placeholders validados.

## 🩺 Restauração Reativa dos Diagnósticos ("Verificar meu login" & "Teste de rede")
- **Inicialização Reativa Instantânea**: Os modais de diagnóstico abrem com resposta visual imediata, pré-populando os estágios de validação e regiões de ping com spinners ativos.
- **Despacho à Prova de Descarte**: Eliminadas condições de corrida com retry loop em `app_core.cpp`, garantindo que os comandos de diagnóstico não sejam descartados durante transições de estado do worker.
- **Preservação Integral da Lógica e Interface**: Fidelidade absoluta à apresentação original, aos testes STUN RFC 5389 via UDP e à validação de autenticação da conta Xbox Live.

---

### 📦 Arquivos Anexados (Português)

| Nome do Arquivo | Descrição |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.6.3.zip` | Pacote ZIP oficial para PlayStation 5 (instalação manual ou via gerenciadores homebrew). |
| `PSBoxCloudGaming[PS5]-v1.6.3.zip.sig` | Assinatura digital ECDSA para verificação de integridade do pacote de PS5. |
| `PSBoxCloudGaming[PS5]-v1.6.3.zip.sha256` | Checksum SHA256 do pacote ZIP de PS5. |
| `PPSA99810.zip` | Pacote canônico de PS5 obrigatório para o atualizador automático in-app. |
| `PPSA99810.zip.sig` | Assinatura criptográfica canônica para validação do in-app updater. |
| `PPSA99810.zip.sha256` | Checksum SHA256 do pacote canônico de PS5. |
| `PSBoxCloudGaming[PS4]-v1.6.3.pkg` | Pacote FPKG instalável para PlayStation 4 (compatível com GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.6.3.pkg.sha256` | Checksum SHA256 do pacote de PS4. |
| `PSBoxCloudGaming[Linux]-v1.6.3.AppImage` | Executável universal para distribuições Linux e Steam Deck (Modo Desktop e Game Mode). |
| `PSBoxCloudGaming[Linux]-v1.6.3.AppImage.sha256` | Checksum SHA256 do pacote AppImage Linux. |

---

### 🚀 Como Instalar (Português)

#### 🎮 PlayStation 5
- **Homebrew App Store / Manual**: Baixe `PSBoxCloudGaming[PS5]-v1.6.3.zip`, extraia para `/data/homebrew/PPSA99810/` no PS5 (via FTP ou pendrive) e inicialize pelo launcher de homebrew.
- **Atualizador Integrado**: Se você já tem o PSBox instalado, acerte as atualizações no menu de configurações; o `PPSA99810.zip` será baixado, terá a assinatura verificada e será instalado automaticamente.

#### 🎮 PlayStation 4
- Baixe `PSBoxCloudGaming[PS4]-v1.6.3.pkg`, copie para a raiz de um pendrive (exFAT/FAT32) e instale via *Configurações > GoldHEN > Instalador de Pacotes*.

#### 💻 Linux Desktop / Steam Deck
- Baixe `PSBoxCloudGaming[Linux]-v1.6.3.AppImage`, conceda permissão de execução (`chmod +x PSBoxCloudGaming[Linux]-v1.6.3.AppImage`) e execute diretamente.
- **Steam Deck**: Adicione o executável `.AppImage` como jogo de fora do Steam (Non-Steam Game) no Modo Desktop para jogar direto pelo Modo Jogo (Game Mode).
