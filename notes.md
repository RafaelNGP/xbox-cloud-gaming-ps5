<!-- LANG:EN -->
# PSBox Cloud Gaming v1.6.2 (Experimental PS4 Release)

## 📺 PS4 Horizontal Screen Tearing Elimination (Issue #366)
- **Strict V-Sync Flip Presentation**: Changed display presentation flip mode on PS4 from `ORBIS_VIDEO_OUT_FLIP_HSYNC` to `ORBIS_VIDEO_OUT_FLIP_VSYNC`.
- **Smooth 60 FPS Pacing**: Eliminates horizontal tearing lines during fast camera rotation and lateral movement, synchronizing frame swaps strictly to the 60 Hz vertical blanking interval.

## 🌐 PS4 High Bitrate UDP Socket Buffering (Issue #368)
- **Kernel Socket Buffer Expansion**: UDP sockets now explicitly request a progressive receive buffer up to 2MB (`SO_RCVBUF = 0x1002`) with fallback down to 128KB, and send buffer up to 1MB (`SO_SNDBUF = 0x1001`), replacing the default ~48-64KB OS buffer.
- **Socket Options Translation & Fallback**: Implemented comprehensive option translation for BSD/Orbis socket levels and constants (`SOL_SOCKET`, `SO_RCVBUF`, `SO_SNDBUF`, `SO_ERROR`, `SO_REUSEADDR`, `SO_RCVTIMEO`, `SO_SNDTIMEO`), with graceful step-down on kernel buffer limits, preventing packet drop during 15+ Mbps burst streams.

## 🎮 PS4 USB Controller & OneFamily Conflict Fix + Rumble Support (Issue #369)
- **OneFamily Coexistence Setting**: Added a user toggle (`usbControllers` / `"Controles USB (Driver OneFamily integrado)"`) via WebUI and `settings.json`. Users using GoldHEN's OneFamily plugin can disable in-app USB claiming to prevent double input registration.
- **Full USB Force-Feedback (Rumble)**: Implemented rumble output packets for XInput, GIP (Xbox One / Series X|S), DualShock 4, DualSense, DualShock 3, and Switch Pro controllers.
- **Rumble Pipeline Routing**: Refactored pad polling so rumble timers and motor outputs dispatch correctly to USB devices even when native `scePad` ports are unoccupied.

### 📦 Attached Files (English)

| File Name | Description |
|---|---|
| `PSBoxCloudGaming[PS4]-v1.6.2.pkg` | Installable FPKG package for PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.6.2.pkg.sha256` | SHA256 checksum for the PS4 package. |

---

### 🚀 How to Install (English)

#### 🎮 PlayStation 4
- Download `PSBoxCloudGaming[PS4]-v1.6.2.pkg`, copy it to the root of a USB drive (exFAT/FAT32), and install via *Settings > GoldHEN > Package Installer*.

<!-- LANG:PT -->
# PSBox Cloud Gaming v1.6.2 (Release Experimental PS4)

## 📺 Eliminação de Screen Tearing Horizontal no PS4 (Issue #366)
- **Apresentação com V-Sync Estrito**: Alterado o modo de flip no PS4 de `ORBIS_VIDEO_OUT_FLIP_HSYNC` para `ORBIS_VIDEO_OUT_FLIP_VSYNC`.
- **Taxa Suave de 60 FPS**: Elimina linhas de corte/rasgo horizontal durante giros rápidos de câmera e movimentação lateral rápida, sincronizando os quadros estritamente no intervalo de apagamento vertical de 60 Hz.

## 🌐 Otimização de Buffers UDP contra Perda de Pacotes no PS4 (Issue #368)
- **Expansão Progressiva de Buffers no Kernel**: Sockets UDP agora alocam de forma progressiva até 2MB de buffer de recepção (`SO_RCVBUF = 0x1002`) com escalonamento gradual até 128KB e buffer de envio de 1MB (`SO_SNDBUF = 0x1001`), superando a limitação do buffer padrão de ~48-64KB do sistema operacional.
- **Tradução e Fallback de Opções de Socket**: Adicionada camada de tradução completa para níveis e constantes de socket (`SOL_SOCKET`, `SO_RCVBUF`, `SO_SNDBUF`, `SO_ERROR`, `SO_REUSEADDR`, etc.) com redução automática em caso de limite do kernel, evitando estouro de buffer e perda de pacotes em bitrates altos de 15+ Mbps.

## 🎮 Suporte e Ajuste a Controles USB no PS4 & Vibração (Issue #369)
- **Opção de Coexistência com OneFamily**: Adicionada chave de configuração (`usbControllers` / `"Controles USB (Driver OneFamily integrado)"`) na WebUI e no `settings.json`. Permite desativar o driver USB interno quando o usuário preferir usar o plugin OneFamily do GoldHEN, eliminando a duplicação de comandos.
- **Vibração Completa em Controles USB**: Implementado envio nativo de pacotes de vibração/rumble para controles XInput, GIP (Xbox One / Series X|S), DualShock 4, DualSense, DualShock 3 e Nintendo Switch Pro.
- **Roteamento do Pipeline de Vibração**: Ajustado o laço de polling para despachar a vibração para controles USB mesmo quando a porta nativa do `scePad` não estiver aberta.

---

### 📦 Arquivos Anexados (Português)

| Nome do Arquivo | Descrição |
|---|---|
| `PSBoxCloudGaming[PS4]-v1.6.2.pkg` | Pacote FPKG instalável para PlayStation 4 (compatível com GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.6.2.pkg.sha256` | Checksum SHA256 do pacote de PS4. |

---

### 🚀 Como Instalar (Português)

#### 🎮 PlayStation 4
- Baixe `PSBoxCloudGaming[PS4]-v1.6.2.pkg`, copie para a raiz de um pendrive (exFAT/FAT32) e instale via *Configurações > GoldHEN > Instalador de Pacotes*.
