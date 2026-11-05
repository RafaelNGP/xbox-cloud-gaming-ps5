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

---
**Installation on PS5**:
- Install directly from **ProsperoStore** or **RecompStore**, or download the signed `PPSA99810.zip` and install manually via Homebrew / FTP.

**Installation on PS4**:
- Download `PSBoxCloudGaming[PS4]-v1.6.1.pkg`, copy it to the root of a USB drive (exFAT/FAT32), and install via *Settings > GoldHEN > Package Installer*.

**Installation on Linux**:
- Download `PSBox-v1.6.1-linux-x86_64.AppImage`, make it executable (`chmod +x`) and run it. It also works on the Steam Deck, in Desktop and Game modes.

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

---
**Instalação no PS5**:
- Instale diretamente pela **ProsperoStore** ou **RecompStore**, ou baixe o pacote assinado `PPSA99810.zip` e instale manualmente via Homebrew / FTP.

**Instalação no PS4**:
- Baixe `PSBoxCloudGaming[PS4]-v1.6.1.pkg`, copie para a raiz de um pendrive (exFAT/FAT32) e instale via *Configurações > GoldHEN > Instalador de Pacotes*.

**Instalação no Linux**:
- Baixe `PSBox-v1.6.1-linux-x86_64.AppImage`, dê permissão de execução (`chmod +x`) e execute. Compatível com Steam Deck em modo Desktop e Game.
