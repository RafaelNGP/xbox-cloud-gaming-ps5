<!-- LANG:EN -->
# PSBox Cloud Gaming v1.6.1

## 🏆 Xbox Achievements to Native PS5 Trophies Conversion
- **Xbox to PS5 Trophy Synchronization**: Converts unlocked Xbox achievements directly into native PlayStation 5 Trophy 2 packages (`TRPTITLE.DAT` + `TROPHY.UCP` + database registration).
- **Trophy Conversion Disclaimer Modal**: Explains local profile registration, warns about PSN ban/flag risk, and advises users to enable only on local/offline profiles or if they do not plan to connect to PSN again. Includes explicit "Estou ciente" (I understand) and "Cancelar" (Cancel) confirmation buttons.
- **Remove Local PSBox Trophies Option**: New action in Settings allowing users to completely remove all trophies and metadata added by PSBox from their local PS5 account at any time, eliminating ban risks. Trophies can be safely re-synced from Xbox anytime.

## 🐧 Linux Desktop Improvements & Input Bug Fixes
- **Keyboard Typing Isolation**: Fixed keyboard text input triggering gamepad buttons in UI navigation and Search screens.
- **Delete Key Fix**: Fixed `Delete` key triggering touchpad hold (signout) on desktop.
- **Eliminated Ghost Phone Icon**: Fixed virtual gamepad detection so inactive WebUI connections do not display a phantom phone controller icon on desktop slot 0.
- **Low-Latency Presentation & Handheld Detection**: Vulkan NV12 decode pipeline and mailbox presentation for Linux desktop and handheld devices (Steam Deck / ROG Ally / Legion Go).

## 🎮 PS4 Native USB Controller Support (OneFamily Integration)
- **Universal USB Controller Drivers**: Direct in-app support on PS4 (using `libSceUsbd`) for PS3 (DualShock 3), PS4 (wired/third-party), PS5 (DualSense), Xbox 360, Xbox One / Series X|S (GIP), 8BitDo adapters/controllers, Nintendo Switch Pro & Input-Only, Google Stadia, and generic DirectInput/HID gamepads.
- **Autonomous & Zero-Dependency**: Works out of the box directly in the application without requiring background daemons or external GoldHEN payloads.

### 📦 Attached Files (English)

| File Name | Description |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.6.1.zip` | Official standalone ZIP package for PlayStation 5 (manual install or homebrew manager). |
| `PSBoxCloudGaming[PS5]-v1.6.1.zip.sig` | ECDSA cryptographic digital signature verifying package authenticity on PS5. |
| `PSBoxCloudGaming[PS5]-v1.6.1.zip.sha256` | SHA256 checksum for the PS5 package. |
| `PPSA99810.zip` | Canonical PS5 package required by the In-App Updater for consoles updating from previous versions. |
| `PPSA99810.zip.sig` | Canonical cryptographic signature required by the In-App Updater. |
| `PPSA99810.zip.sha256` | SHA256 checksum for the canonical PS5 package. |
| `PSBoxCloudGaming[PS4]-v1.6.1.pkg` | Installable FPKG package for PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.6.1.pkg.sha256` | SHA256 checksum for the PS4 package. |
| `PSBoxCloudGaming[Linux]-v1.6.1.AppImage` | Universal portable binary for Linux distributions and Steam Deck (Desktop Mode & Game Mode). |
| `PSBoxCloudGaming[Linux]-v1.6.1.AppImage.sha256` | SHA256 checksum for the Linux AppImage. |

---

### 🚀 How to Install (English)

#### 🎮 PlayStation 5
- **Automatic In-App Update**: Open the app's *Settings* menu and choose the built-in update option.
- **Manual Installation**:
  1. Download `PSBoxCloudGaming[PS5]-v1.6.1.zip` or `PPSA99810.zip`.
  2. Extract its contents over FTP to `/data/homebrew/PPSA99810/` on your jailbroken PS5.
  3. Ensure all files and folders have `0777` permissions.

#### 🎮 PlayStation 4
- Download `PSBoxCloudGaming[PS4]-v1.6.1.pkg`, copy it to the root of a USB drive (exFAT/FAT32), and install via *Settings > GoldHEN > Package Installer*.

#### 🐧 Linux Desktop & Steam Deck
- Download `PSBoxCloudGaming[Linux]-v1.6.1.AppImage`, make it executable (`chmod +x`) and run it. Works directly in Desktop Mode and Steam Game Mode.

<!-- LANG:PT -->
# PSBox Cloud Gaming v1.6.1 (Português)

## 🏆 Conversão de Conquistas Xbox em Troféus Nativos do PS5
- **Sincronização de Conquistas para Troféus PS5**: Converte conquistas desbloqueadas do Xbox diretamente em pacotes nativos do sistema Trophy 2 do PlayStation 5 (`TRPTITLE.DAT` + `TROPHY.UCP` + registro no sistema).
- **Modal de Aviso sobre Troféus**: Explica a modificação no perfil local, alerta sobre o risco de banimento/marcação na PSN e instrui o usuário a ativar a opção apenas em contas locais (offline) ou se não pretende conectar novamente à PSN. Inclui botões explícitos "Estou ciente" e "Cancelar".
- **Opção de Remoção de Troféus Locais do PSBox**: Nova opção nas Configurações para remover com segurança todos os troféus e registros adicionados pelo PSBox da conta local a qualquer momento, eliminando riscos de banimento. Os troféus podem ser re-sincronizados da conta Xbox quando desejar.

## 🐧 Melhorias no Linux Desktop & Correções de Entrada
- **Isolamento de Digitação no Teclado**: Corrigido problema onde a digitação em telas de busca e interface acionava botões do controle.
- **Correção da Tecla Delete**: Evita que a tecla `Delete` acione o botão touchpad (desconexão da conta) no desktop.
- **Eliminação do Ícone Fantasma de Celular**: Corrigida a detecção do controle virtual da WebUI para evitar a exibição indevida de ícone de celular no slot 0 do desktop.
- **Apresentação de Baixa Latência & Detecção de Handhelds**: Pipeline Vulkan NV12 e modo de apresentação mailbox para desktop Linux e dispositivos portáteis (Steam Deck / ROG Ally / Legion Go).

## 🎮 Suporte Nativo a Controles USB no PS4 (Integração OneFamily)
- **Drivers Universais de Controles USB**: Suporte nativo direto no app para PS4 (via `libSceUsbd`) para controles de PS3 (DualShock 3), PS4 (com fio/paralelos), PS5 (DualSense), Xbox 360, Xbox One / Series X|S (GIP), adaptadores/controles 8BitDo, Nintendo Switch Pro e Input-Only, Google Stadia e gamepads HID/DirectInput genéricos.
- **Autônomo e Sem Dependências Externas**: Funciona imediatamente dentro do próprio aplicativo sem necessidade de daemons ou payloads externos do GoldHEN em segundo plano.

---

### 📦 Arquivos Anexados (Português)

| Nome do Arquivo | Descrição |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.6.1.zip` | Pacote ZIP oficial para PlayStation 5 (instalação manual ou via gerenciadores homebrew). |
| `PSBoxCloudGaming[PS5]-v1.6.1.zip.sig` | Assinatura digital ECDSA para verificação de integridade e autenticidade no PS5. |
| `PSBoxCloudGaming[PS5]-v1.6.1.zip.sha256` | Checksum SHA256 do pacote de PS5. |
| `PPSA99810.zip` | Pacote canônico de PS5 obrigatório para o atualizador automático in-app (consoles rodando v1.5.4 ou anterior). |
| `PPSA99810.zip.sig` | Assinatura canônica obrigatória para validação do in-app updater. |
| `PPSA99810.zip.sha256` | Checksum SHA256 do pacote canônico de PS5. |
| `PSBoxCloudGaming[PS4]-v1.6.1.pkg` | Pacote FPKG instalável para PlayStation 4 (compatível com GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.6.1.pkg.sha256` | Checksum SHA256 do pacote de PS4. |
| `PSBoxCloudGaming[Linux]-v1.6.1.AppImage` | Pacote executável universal para distribuições Linux e Steam Deck (Desktop Mode e Game Mode). |
| `PSBoxCloudGaming[Linux]-v1.6.1.AppImage.sha256` | Checksum SHA256 do pacote AppImage Linux. |

---

### 🚀 Como Instalar (Português)

#### 🎮 PlayStation 5
- **Atualização Automática**: Abra as *Configurações* no próprio app e utilize a opção de atualização automática integrada.
- **Instalação Manual**:
  1. Baixe `PSBoxCloudGaming[PS5]-v1.6.1.zip` ou `PPSA99810.zip`.
  2. Extraia o conteúdo para `/data/homebrew/PPSA99810/` no seu PS5 via FTP.
  3. Garanta permissões `0777` em todas as pastas e arquivos extraídos.

#### 🎮 PlayStation 4
- Baixe `PSBoxCloudGaming[PS4]-v1.6.1.pkg`, copie para a raiz de um pendrive (exFAT/FAT32) e instale via *Configurações > GoldHEN > Instalador de Pacotes*.

#### 🐧 Linux Desktop & Steam Deck
- Baixe `PSBoxCloudGaming[Linux]-v1.6.1.AppImage`, dê permissão de execução (`chmod +x`) e execute. Compatível com Steam Deck em modo Desktop e Game Mode.
