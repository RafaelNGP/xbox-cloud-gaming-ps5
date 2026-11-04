<!-- LANG:EN -->
# PSBox Cloud Gaming v1.6.0

## 🎮 Native USB Controllers Support on PS5
- **Native Local Multiplayer (P1..P4)**: Added direct multi-controller USB support via `/dev/ugen` and `USB_FS` ioctls (`src/input/pad_usb_ps5.cpp`), bypassing `libScePad` limitations.
- **Expanded Hardware Compatibility**:
  - **Xbox Series X|S** and **Xbox One** controllers connected via USB-C (GIP).
  - **XInput** dongles and adapters (e.g., **8BitDo Ultimate 2.4G**, Xbox wireless USB adapters).
- **Visual Status Indicators**: Connected controller slot badges (1..4) displayed on the home menu footer.

## ⏱️ Cloud Playtime Quota & In-Game Warnings (#342)
- **Playtime Meter**: Real-time display of monthly remaining cloud stream allowance directly on the top header bar.
- **In-Game Warnings**: Automatic on-screen notifications at 15 minutes and 5 minutes of remaining game time.
- **Graceful Quota Handling**: User-friendly dialog with QR code if a session ends due to playtime limits (`KickForOutOfStreamingTime`).

## 🎙️ Low-Latency Microphone (10 ms) (#187)
- **10 ms Opus Framing**: Microphone audio capture frames reduced from 20 ms to 10 ms, halving voice transmission latency (~10.5 ms).
- **Telemetry & Diagnostics**: Comprehensive timing and packet delivery metrics added to logging.

## 🚀 Stream Performance & Audio Stability (#317)
- **Reduced Skipped Frames**: Up to 75% reduction in frame drops via enhanced frame pacing and presentation synchronization.
- **Adaptive Audio Queue**: Dynamic target audio buffer (59 ms – 62 ms) preventing audio crackle with smooth underrun recovery.
- **Stream Recovery Watchdog**: Automatic detection and recovery if the incoming video stream stalls for more than 5 seconds.

## 🎨 UI Alignment & Documentation Polish
- **UI Alignment**: PSBox brand logo centered vertically at Y=72 alongside compound title and cloud playtime progress meter.
- **Revamped Documentation**: Streamlined `README.md` prioritizing PS5, with direct install links for **RecompStore** and **ProsperoStore**, plus simplified build instructions.

---
**Installation on PS5**:
- Install directly from **ProsperoStore** or **RecompStore**, or download the signed `PPSA99810.zip` and install manually via Homebrew / FTP.

**Installation on PS4**:
- Download `PSBoxCloudGaming[PS4]-v1.6.0.pkg`, copy it to the root of a USB drive (exFAT/FAT32), and install via *Settings > GoldHEN > Package Installer*.

<!-- LANG:PT -->
# PSBox Cloud Gaming v1.6.0 (Português)

## 🎮 Suporte Nativo a Controles USB no PS5
- **Multiplayer Local Nativo (P1..P4)**: Adicionado suporte direto a múltiplos controles USB independentes via `/dev/ugen` e ioctls `USB_FS` (`src/input/pad_usb_ps5.cpp`), contornando as limitações da `libScePad`.
- **Compatibilidade Expandida**:
  - Controles **Xbox Series X|S** e **Xbox One** via USB-C (GIP).
  - Adaptadores e dongles **XInput** (ex: **8BitDo Ultimate 2.4G**, receptores sem fio Xbox).
- **Indicadores Visuais**: Ícones de slots de controles (1..4) no rodapé do menu inicial.

## ⏱️ Cotas de Tempo de Nuvem & Notificações (#342)
- **Rastreamento de Horas**: Exibição em tempo real do tempo restante de streaming do plano mensal na barra superior.
- **Notificações em Jogo**: Alertas visuais automáticos aos 15 minutos e 5 minutos restantes de jogo.
- **Tratamento Amigável**: Diálogo explicativo e QR Code caso a sessão seja encerrada por limite de tempo (`KickForOutOfStreamingTime`).

## 🎙️ Microfone de Baixa Latência (10 ms) (#187)
- **Framing Opus de 10 ms**: Pacotes de captura de áudio do microfone reduzidos de 20 ms para 10 ms, cortando a latência de transmissão pela metade (~10.5 ms).
- **Métricas no Log**: Diagnóstico detalhado de temporização e envio do microfone.

## 🚀 Desempenho e Estabilidade de Streaming (#317)
- **Menos Quadros Descartados**: Redução de até 75% em *skipped frames* através de sincronização aprimorada de quadros.
- **Fila de Áudio Adaptativa**: Buffer alvo dinâmico entre 59 ms e 62 ms, prevenindo estalidos (*crackle*) com recarga suave de underrun.
- **Watchdog de Recuperação**: Detecção e recuperação automática caso o fluxo de vídeo congele por mais de 5 segundos.

## 🎨 Ajustes Visuais e Documentação
- **Centralização da UI**: Logo PSBox perfeitamente centralizado verticalmente no eixo Y=72 com o título e a barra de progresso de tempo restante.
- **Documentação Otimizada**: `README.md` reescrito com foco prioritário no PS5, links de instalação rápida na **RecompStore** e **ProsperoStore**, e instruções de compilação resumidas.

---
**Instalação no PS5**:
- Instale via **ProsperoStore** ou **RecompStore**, ou baixe o `PPSA99810.zip` assinado e instale manualmente via Homebrew / FTP.

**Instalação no PS4**:
- Baixe o `PSBoxCloudGaming[PS4]-v1.6.0.pkg`, copie para a raiz de um pendrive USB (exFAT/FAT32) e instale pelo console em *Configurações > GoldHEN > Package Installer*.
