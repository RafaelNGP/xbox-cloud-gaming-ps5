<!-- LANG:EN -->
# PSBox Cloud Gaming v1.6.0

## 🎮 Native USB Controllers Support on PS5
- **Native Local Multiplayer (P1..P4)**: Added direct multi-controller USB support via `/dev/ugen` and `USB_FS` ioctls (`src/input/pad_usb_ps5.cpp`), bypassing `libScePad` limitations.
- **Expanded Hardware Compatibility**:
  - **Xbox Series X|S** controllers on a USB cable (GIP). Xbox One pads use the same protocol but were not tested yet.
  - **XInput** pads and dongles (e.g., **8BitDo Ultimate 2.4G** in X-Input mode).
- **Not supported yet**: DualShock 4, generic HID pads and fightsticks, and the Xbox Wireless Adapter.
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
- **Adaptive Audio Queue**: The audio delay follows the connection (target 40–100 ms; from ~98 to ~60 ms on average in Fortnite tests), and the sound fades back in after a gap instead of crackling.
- **Lost Packets on High Ping**: The app waits longer for resent video packets when the round trip is high (it used to give up too early).
- **Stall Diagnostics**: If the stream loop stops for more than 5 seconds, the log records where it got stuck (to track down the freeze reported in #317).

## 🎨 UI Alignment & Documentation Polish
- **UI Alignment**: PSBox brand logo centered vertically at Y=72 alongside compound title and cloud playtime progress meter.
- **Revamped Documentation**: Streamlined `README.md` prioritizing PS5, with direct install links for **RecompStore** and **ProsperoStore**, plus simplified build instructions.

---
**Installation on PS5**:
- Install directly from **ProsperoStore** or **RecompStore**, or download the signed `PPSA99810.zip` and install manually via Homebrew / FTP.

**Installation on PS4**:
- Download `PSBoxCloudGaming[PS4]-v1.6.0.pkg`, copy it to the root of a USB drive (exFAT/FAT32), and install via *Settings > GoldHEN > Package Installer*.

**Installation on Linux**:
- Download `PSBox-v1.6.0-linux-x86_64.tar.gz`, unpack it anywhere and run `PSBox/psbox` (`PSBox/README-linux.txt` has the details).

<!-- LANG:PT -->
# PSBox Cloud Gaming v1.6.0 (Português)

## 🎮 Suporte Nativo a Controles USB no PS5
- **Multiplayer Local Nativo (P1..P4)**: Adicionado suporte direto a múltiplos controles USB independentes via `/dev/ugen` e ioctls `USB_FS` (`src/input/pad_usb_ps5.cpp`), contornando as limitações da `libScePad`.
- **Compatibilidade Expandida**:
  - Controles **Xbox Series X|S** via cabo USB (GIP). Os do Xbox One usam o mesmo protocolo, mas ainda não foram testados.
  - Controles e dongles **XInput** (ex: **8BitDo Ultimate 2.4G** no modo X-Input).
- **Ainda não suportados**: DualShock 4, controles HID genéricos e fightsticks, e o Adaptador Sem Fio Xbox.
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
- **Fila de Áudio Adaptativa**: O atraso do áudio acompanha a conexão (alvo de 40–100 ms; de ~98 para ~60 ms em média nos testes com Fortnite), e o som volta suave depois de uma falha, sem estalos.
- **Pacotes Perdidos com Ping Alto**: O app espera mais pelos pacotes de vídeo reenviados quando o ping é alto (antes desistia cedo demais).
- **Diagnóstico de Travamentos**: Se o loop da transmissão parar por mais de 5 segundos, o log registra onde travou (para investigar o congelamento relatado na #317).

## 🎨 Ajustes Visuais e Documentação
- **Centralização da UI**: Logo PSBox perfeitamente centralizado verticalmente no eixo Y=72 com o título e a barra de progresso de tempo restante.
- **Documentação Otimizada**: `README.md` reescrito com foco prioritário no PS5, links de instalação rápida na **RecompStore** e **ProsperoStore**, e instruções de compilação resumidas.

---
**Instalação no PS5**:
- Instale via **ProsperoStore** ou **RecompStore**, ou baixe o `PPSA99810.zip` assinado e instale manualmente via Homebrew / FTP.

**Instalação no PS4**:
- Baixe o `PSBoxCloudGaming[PS4]-v1.6.0.pkg`, copie para a raiz de um pendrive USB (exFAT/FAT32) e instale pelo console em *Configurações > GoldHEN > Package Installer*.

**Instalação no Linux**:
- Baixe o `PSBox-v1.6.0-linux-x86_64.tar.gz`, descompacte onde quiser e rode `PSBox/psbox` (detalhes em `PSBox/README-linux.txt`).
