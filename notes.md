# PSBox Cloud Gaming v1.6.0

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
