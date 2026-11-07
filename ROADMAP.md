# Roadmap — PSBox Cloud Gaming

Planejamento e visão de futuro do projeto **PSBox Cloud Gaming**, priorizado por valor para o usuário no console PlayStation 5 (e ecossistema PlayStation).

Status: **Em andamento (next)**, **Planejado (planned)**, **Futuro (later)**, **Descartado (dropped)**.

> **Última atualização:** 07/11/2026 (release estável **v1.6.3** / correções de engasgos de streaming no PS5, cache v3, 20 dicas de launching e diagnósticos reativos).

---

## 🐛 Bugs Prioritários & Correções Imediatas (High-Priority Fixes)

### 1. Falso Positivo de "46 Novos Jogos" & Otimização do Tempo de Splash Screen — *done (v1.5.5)*
- **Sintoma Resolvido**: Notificação repetida de "46 novos jogos" e retenção desnecessária do usuário na tela de splash foram completamente eliminadas.
- **Implementação**:
  - Persistência integral de todos os produtos resolvidos do catálogo em `library_cache.json` e rastreamento de `knownProducts_`.
  - Liberação instantânea da Splash Screen (`showHome()`) imediatamente após o login, executando o `syncDelta()` de forma não-bloqueante em background com spinner discreto.
  - Cálculo de `newCount` restrito estritamente a novos jogos que ingressaram no Game Pass em relação aos conhecidos no cache anterior.

### 2. Cursor do Mouse Duplicado na Build Desktop — *done (v1.5.5)*
- **Sintoma Resolvido**: Exibição simultânea do cursor do sistema operacional e do cursor vetorial de software corrigida no Linux / Steam Deck / Windows.
- **Implementação**: Ocultação do cursor de hardware do SO via `SDL_HideCursor()` na inicialização da janela em `shell_desktop.cpp`.

### 3. Falha na Atualização Automática do Catálogo do Xbox Game Pass (Exige Refresh Manual) — *done (v1.5.5)*
- **Sintoma Resolvido**: Estantes de catálogo ("Novidades", "Mais populares", etc.) agora atualizam automaticamente no boot sem intervenção manual.
- **Implementação**: O `syncDelta()` consulta e sincroniza automaticamente os feeds SIGL atualizados (`kRecentlyAdded`, `kMostPopular`, `kFreeToPlay`, `kLeavingSoon`) e refresca as linhas correspondentes em `layout_`.

### 4. Otimização de Microfone Opus (10 ms) e Latência de Áudio — *done (v1.6.0)* (#187)
- **Implementação**: Pacotes de microfone com framing de 10 ms (cortando a latência de envio pela metade para ~10.5 ms) com métricas de temporização no log.

### 5. Estabilidade de Stream e Fila Adaptativa de Áudio — *done (v1.6.0)* (#317)
- **Implementação**: ~75% menos quadros pulados; fila de áudio adaptativa (alvo de 40–100 ms; de ~98 para ~60 ms em média nos testes com Fortnite) que volta suave depois de uma falha; espera por pacotes reenviados que acompanha o ping; e registro no log de onde a transmissão trava por mais de 5 s.

### 6. Cotas de Tempo de Nuvem e Avisos em Jogo — *done (v1.6.0)* (#342)
- **Implementação**: Suporte a limites de tempo mensal de nuvem (parser de horas restantes e totais, exibição na barra superior centralizada, avisos a 15m/5m e tela amigável com QR Code).

### 7. Diagnóstico "Verificar meu login" Inoperante (Modal Aberto sem Execução) — *done (v1.6.3)*
- **Sintoma Resolvido**: Modal abria em branco sem disparar os testes nem renderizar o feedback de progresso dos estágios de validação.
- **Implementação**:
  - Inicialização reativa imediata dos 6 estágios de validação (`signInCheck_.stages`) com spinners de carregamento em `openTester()`.
  - Fila não-bloqueante à prova de descartes com retry loop em `app_core.cpp` para o comando `kCheckSignIn`.
  - Preservação estrita de 100% da lógica e apresentação originais em `worker_diagnostics.cpp` e `app_ui_settings.cpp`.

### 8. Diagnóstico "Teste de rede" Inoperante (Modal Estático em "UDP / STUN (NAT)") — *done (v1.6.3)*
- **Sintoma Resolvido**: Modal abria estático exibindo apenas o cabeçalho UDP/STUN sem realizar medições de RTT e jitter para as regiões dos datacenters.
- **Implementação**:
  - Inicialização reativa imediata das regiões (`networkTest_.regions`) com indicadores de sondagem ativa (`...`).
  - Retry loop seguro para o comando `kNetworkTest` em `app_core.cpp` eliminando descartes durante transições do worker.
  - Sondas STUN RFC 5389 operando com total integridade e reportando latências instantaneamente à UI.

### 9. Problemas de Mapeamento de Input OneFamily no PS4 nas Opções 2, 3 e 4 (#369) — *investigating (prioridade máxima)*
- **Sintoma Reportado**: Feedback de usuários na build de PS4 (issue #369) reportando falhas com o driver OneFamily integrado: quando o controle está configurado como **Opção 1**, tudo opera perfeitamente. No entanto, ao selecionar as **Opções 2, 3 ou 4**, alguns jogos apresentam anomalias e perda de reconhecimento de inputs/botões (comprovado em testes com *Remnant II*). Ao retornar para a Opção 1, o controle volta ao funcionamento normal.
- **Investigação & Correção**:
  - Auditar as rotinas de multiplexação de portas, índices de dispositivos e tabelas de mapeamento HID em `src/input/pad_usb_ps4.cpp`, `src/input/pad_usb.cpp` e `src/input/pad_usb_hid.cpp`.
  - Verificar se as configurações das opções 2, 3 e 4 introduzem divergências no parsing de relatórios de entrada (input reports), no tratamento de botões compartilhados ou na leitura de múltiplos slots via `libSceUsbd`.
  - Corrigir o tratamento de estados e validar a consistência de leitura de botões e eixos em todos os modos suportados.

---

## 🚀 Em Andamento e Próximos Passos (Now & Next)

### 1. Port Nativo para PlayStation 4 (PSBox Cloud Gaming - PS4) — *done (v1.5.5)*
Port dedicado e otimizado para consoles PlayStation 4 com jailbreak (firmwares 9.00 / 11.00):
- **Pacote e Identidade Isolados**: Pacote PKG independente (`PSBX00001.pkg`) com `TitleID` próprio, evitando qualquer interferência ou colisão com a versão de PS5 (`PPSA99810`).
- **Conectividade WebRTC**: Stack ICE, STUN e handshake DTLS adaptados com compatibilidade FreeBSD/Orbis libc++ (`ETIMEDOUT=60`), handshake de certificados MbedTLS e canais de dados SCTP.
- **Pipeline de Vídeo e Áudio**: Decodificação H.264 por hardware via `libSceVideodec2` em Direct Memory Onion/Garlic, renderizador 2D por software com 4 threads SIMD SSE4.2 (cópia zero de overlay via `shared_ptr`) e saída de áudio DMA com dreno limpo.
- **Controles e Periféricos**: Suporte completo ao DualShock 4 com rumble de motores duplos, lightbar, touchpad e giroscópio.

### 2. Suporte Nativo a Controles USB no PS5 — *parcial (v1.6.0)*
Leitura direta pelo `/dev/ugen*` com ioctls `USB_FS`, contornando a `libScePad` (o `/dev/uhid*` não existe no PS5).
- **Entregue na v1.6.0**: controles **Xbox Series** no cabo (GIP) e controles/dongles **XInput** (ex: **8BitDo Ultimate 2.4G** no modo X-Input). Cada controle é um jogador, na ordem em que conecta; Xbox/Home abre o menu do jogo daquele jogador e Xbox/Home + LB/RB troca o jogador.
- **Falta**:
  - **DualShock 4 USB**.
  - **Xbox 360 com receptor sem fio** e **Adaptador Sem Fio Xbox** (protocolos diferentes).
  - **8BitDo em D-Input, fightsticks e controles HID genéricos** (exige ler o descritor HID).
  - Vibração nos controles USB, ícones de botões do Xbox e teste do controle de Xbox One no cabo.

### 3. Multi-Phone WebUI Controllers (P1..P4) & Layout Studio — *next*
Expansão do servidor local integrado (porta 8080) para multiplayer cooperativo local sem custos de hardware:
- **Suporte a até 4 Smartphones Simultâneos**: Seleção dinâmica de slot (`P1`, `P2`, `P3`, `P4`) via `/gamepad`, heartbeat automático de desconexão e transmissão binária via WebSocket (`/ws`) com latência < 5 ms.
- **PSBox Layout Studio**: Editor visual de controles na WebUI com pré-visualização WYSIWYG, ajuste de botões analógicos e gatilhos para telas de toque, salvamento de perfis (`portrait`/`landscape`) e persistência imediata.

### 4. SmartGlass QR Code no In-Game Menu (Overlay) — *next (v1.7.0)*
Acesso instantâneo e sem digitação manual ao controle virtual via smartphone durante a transmissão:
- **Exibição Dinâmica no Overlay**: Renderização de código QR vetorial de alta definição diretamente na interface do menu in-game (`stream_menu.cpp`).
- **Resolução Automática de IP Local**: Aponta dinamicamente para o endereço local da porta 8080 (`http://<ip_ps5>:8080/gamepad`), permitindo que qualquer smartphone conectado à rede Wi-Fi pareie instantaneamente como controle complementar ou principal (P1..P4).
- **Toggle de Exibição Rápida**: Opção dedicada no menu in-game (<kbd>TOUCHPAD</kbd> + <kbd>OPTIONS</kbd>) para exibir e ocultar o QR Code com 1 clique sem interromper a transmissão.

---

## 📋 Planejado para o Futuro (Planned & Later)

### 1. Sincronização de Conquistas Xbox para Troféus PlayStation (PS5 Entregue / Pop-in Nativo & PS4 Planejados) — *em evolução*
Espelhamento contínuo e estritamente aditivo das conquistas obtidas no Xbox Cloud Gaming para o subsistema local de troféus da conta PlayStation:
- **Entregue no PS5 (`PSBox_Trophies.elf` + Provisionamento Nativo)**:
  - **Automação 100% Transparente**: Payload interno (`assets/PSBox_Trophies.elf` estagiado em `/data/psbox/PSBox_Trophies.elf`) enviado automaticamente na inicialização sem exigir configuração manual pelo usuário.
  - **Sincronização Estritamente Aditiva e Proteção de Jogos Locais**: Preserva integralmente quaisquer troféus e timestamps já conquistados localmente no console (`TRPTITLE.DAT` e pacotes `TROPHY.UCP` de jogos instalados em disco/PSN nunca são sobrescritos ou zerados; conquistas de nuvem apenas somam novos troféus ao progresso existente).
  - **Controle Individual por Jogo**: Opção nas configurações individuais de cada jogo (<kbd>OPTIONS</kbd> na tela de detalhes) para desativar a sincronização de troféus de títulos específicos mantendo a sincronização global ativa para o restante da biblioteca.
  - **Zero Toasts do Aplicativo em Gameplay**: Como o próprio stream de vídeo do Xbox já exibe a notificação da conquista em tela, o aplicativo não exibe toasts próprios ao desbloquear troféus.
- **Exploração Futura — Pop-in Nativo do Sistema PlayStation Durante a Gameplay (`planned`)**:
  - Investigar o acionamento direto do pop-in nativo de troféu do sistema operacional do PS5 (`SceNotification` / canal `Trophies` com template `InteractiveToastTrophy` ou gatilho de transição `0 -> 1` via `sceNpTrophy2UnlockTrophy` com contexto ativo do `NPWR` dinâmico) durante a transmissão para exibir o banner oficial do PS5 sem sobrepor toasts customizados do app.
- **Plano Técnico para Suporte Nativo de Troféus no PlayStation 4 (`planned`)**:
  - Atualmente no PS4, `libSceNpTrophy` (`sceNpTrophyRegisterContext` / `sceNpTrophyUnlockTrophy`) exige um pacote `TROPHY.TRP` assinado pela chave oficial da Sony em `/app0/sce_sys/trophy/trophy00.trp` e vinculado ao `NPWR` fixo do pacote FPKG, não permitindo registrar múltiplos jogos de nuvem dinamicamente via API padrão.
  - **Referência de Implementação**: [Atlas Trophy Tool (Pauloxzak)](https://github.com/Pauloxzak/atlas-trophy-tool) — projeto de referência da comunidade para manipulação, extração, reconstrução de pacotes `TROPHY.TRP` (com `TROPCONF.SFM` e ícones) e injeção direta no banco `trophy_local.db` do PlayStation 4.
  - **Arquitetura Planejada para PS4**:
    1. **Construtor de Pacotes `TRP` (v1/v2)**: Implementar gerador de arquivos `TROPHY.TRP` (formato de arquivo Orbis OS contendo `TROPCONF.SFM` XML + `ICON0.PNG` + `TROP000.PNG..TROP127.PNG` sem criptografia em ambiente GoldHEN/desbloqueado) em `/user/home/<uid>/trophy/conf/<NPWR>/TROPHY.TRP`.
    2. **Provisionamento Direto no SQLite do Orbis OS (`trophy_local.db`)**: Popular de forma aditiva as tabelas `tbl_trophy_title` (resumo de progresso, `np_comm_id`, título, contadores de bronze/prata/ouro/platina) e `tbl_trophy_flag` (estado `unlocked`, `time_unlocked` em ticks RTC, `grade`, `visible`) diretamente em `/user/home/<uid>/trophy/db/trophy_local.db`, preservando troféus locais existentes.
    3. **Metadados de Reconhecimento no `/system_data/priv/mms/app.db` (PS4)**: Inserir entradas sintéticas de catálogo para que a tela nativa de Troféus do PS4 exiba os ícones e listas sem exigir FPKGs individuais instalados para cada jogo da nuvem.

### 2. Auto-Update para PlayStation 4 via GoldHEN Package Installer API — *planned*
Superar a limitação de partição somente-leitura (`/user/app/`) do Orbis OS permitindo atualizações diretas no PS4 sem depender de pendrive ou computador:
- **Download em Background**: Ao detectar uma release nova, o app baixa o `PSBX00001.pkg` oficial do GitHub diretamente para o disco interno (`/data/pkg/PSBX00001.pkg`).
- **Integração com API Local do GoldHEN**: Envio de comando HTTP para o serviço integrado do GoldHEN (`http://127.0.0.1:12800/api/install`), instruindo o daemon a instalar a atualização em segundo plano.
- **Encerramento Limpo**: Fechamento do app para que o instalador do sistema conclua o update do FPKG sem conflito de arquivos em uso.

### 3. Presença Visual e Banners em Lojas (RecompStore & Prospero Store) — *planned*
- Atualização do catálogo oficial com as artes em alta definição (512x512 Icon, 1080p/4K Hero Banners e screenshots oficiais de jogos como *Halo Infinite* e *Fortnite*).
- Manutenção contínua de metadados, links de releases assinadas e sincronização de atualizações automáticas via API.

### 4. Atalhos Diretos na Tela Inicial do PS5 (Home-Screen Forwarders) — *later*
- Criação de atalhos individuais na dashboard principal do PS5 para iniciar títulos específicos em nuvem diretamente, utilizando o formato ProsperoLight forwarder.

### 5. Suporte Completo a Teclado e Mouse no PlayStation 4 — *planned*
Paridade total no suporte a periféricos USB entre as versões de PS5 e PS4:
- **Camada de Entrada Nativa**: Habilitar a inicialização e polling contínuo via `libSceKeyboard` e `libSceMouse` (`src/input/kbm_ps4.cpp`) conectado diretamente ao loop de eventos global (`src/app/app_core.cpp`).
- **Identificação Visual Dinâmica**: Exibição do ícone do periférico em uso (`kIconKeyboardMouse` / `U+F8FE`) nos indicadores da UI, barra superior e menu in-game (`stream_menu.cpp`).
- **Navegação e Jogabilidade Transparente**:
  - Emulação de gamepad para controle da dashboard e jogabilidade direta em qualquer jogo da nuvem (inclusive títulos que não aceitam KBM nativo no stream).
  - Encaminhamento direto de eventos de mouse e teclado nos títulos com suporte nativo KBM.
  - Captura e liberação suave do cursor do mouse em transições de menus e overlays.

### 6. Calibração de Giroscópio e Ajuste Fino de Mira In-Game (RFC / Discussão Prévia) — *planned*
Recurso solicitado pela comunidade para jogos de tiro e mira precisa (ex: *Call of Duty: Warzone*, *Remnant II*, *Halo Infinite*):
- **Objetivo**: Permitir calibração dinâmica dos sensores de movimento (giroscópio/acelerômetro) do DualSense (PS5) e DualShock 4 (PS4) durante a sessão ativa na nuvem através do menu in-game (overlay).
- **Escopo e Planejamento Técnico**:
  - **Compensação de Desvio Estático (Zero-Drift Bias)**: Rotina de calibração rápida no menu in-game (ex: 2 a 3 segundos com o controle em repouso) para calcular a média de erro dos eixos angulares e zerar o drift estático.
  - **Ajustes de Sensibilidade e Zona Morta (Deadzone)**: Configurações análogas à calibração de sticks analógicos, incluindo deadzone angular para amortecer micro-tremores involuntários e multiplicadores de sensibilidade separados para eixos horizontal (yaw) e vertical (pitch).
  - **Menu de Calibração In-Game**: Interface acessível diretamente durante o streaming (<kbd>OPTIONS</kbd> + <kbd>TOUCHPAD</kbd>), possibilitando testar e refinar a precisão em tempo real.
  - **Persistência por Jogo**: Armazenamento dos parâmetros no perfil do título (`activeGameProfile_` em `settings.json`).
  - *Processo*: Requisitos e comportamento de UI/UX serão conversados e alinhados detalhadamente antes de qualquer implementação de código.

---

## 🚫 Decisões Técnicas e Recursos Descartados (Dropped)

- **Discord Rich Presence (Desktop)**: Descartado; o ecossistema Xbox Live / Game Pass já fornece presença nativa de atividade.
- **Assistente de Primeiro Uso (Onboarding)**: Descartado para manter navegação direta, rápida e sem telas forçadas.
- **Captura de Vídeo dos Últimos 30s da Partida**: Descartado; impacto desnecessário em memória e I/O de disco no console.
- **GeForce Now**: Descartado; protocolo totalmente distinto e termos de serviço incompatíveis com a proposta do projeto.
- **Transmissão HDR / Surround 5.1 / Frame Generation**: xCloud transmite nativamente em áudio estéreo e vídeo SDR 1080p; interpolação artificial de quadros adicionaria latência intolerável.
- **Múltiplas Contas Xbox Simultâneas**: Não suportado; simplificação de cache local e segurança de credenciais.

---

## 📦 Histórico de Versões Recentes

Para o histórico completo e detalhado de todas as versões anteriores (v0.3.0 até v1.4.0), consulte as [Releases Oficiais no GitHub](https://github.com/RafaelNGP/xbox-cloud-gaming-ps5/releases).

- **v1.6.3** *(Atual)*:
  - **Eliminação de Engasgos de Streaming no PS5**: Pausa automática e completa do processamento da fila em lote de troféus (`TaskType::BatchTitle`) em `g_trophyThread` durante gameplay ativo (`isStreaming()`), eliminando micro-travamentos e contenção de CPU; checagem em tempo real (`TaskType::RealtimePoll`) mantida com prioridade máxima e retomada suave pós-sessão.
  - **Restauração de Descrições Completas & Cache v3**: Inversão de precedência para priorizar descrições longas (`ProductDescription`) sobre slogans curtos (`ProductDescriptionShort`); migração automática para `kCacheVersion = 3` com reidratação de textos curtos via API `RemoteHighSapphire0`.
  - **20 Dicas Dinâmicas na Tela de Launching**: Expansão de 8 para 20 dicas cobrindo todas as novidades recentes (DualSense haptics/triggers, mouse no touchpad, WebUI SmartGlass, troféus, controles USB e áudio), rotacionando a cada 7 segundos com semente pseudo-aleatória por sessão e localização completa em 6 idiomas (EN, PT-BR, ES, DE, FR, IT).
  - **Restauração Reativa dos Diagnósticos**: Telas "Verificar meu login" e "Teste de rede" inicializadas reativamente com spinners e sondagem instantânea de estágios e regiões; retry loop em `app_core.cpp` prevenindo descartes de comandos durante transições do worker.
  - **Lançamento Tri-Plataforma Triplo**: Pacotes completos e verificados para PlayStation 5 (Standalone + Atualizador In-App PPSA99810), PlayStation 4 (FPKG GoldHEN) e Linux Desktop / Steam Deck (AppImage).
- **v1.6.0**:
  - **Controles USB no PS5**: Xbox Series no cabo e controles/dongles XInput (8BitDo Ultimate 2.4G), cada um como seu próprio jogador.
  - **Horas de jogo na nuvem** (#342): tempo restante na barra superior, avisos aos 15 e 5 minutos, e tela explicativa com QR quando o tempo acaba.
  - **Microfone em pacotes de 10 ms** (#187): latência de codificação de ~21 para ~10,5 ms.
  - **Transmissão** (#317): ~75% menos quadros pulados, áudio com menos atraso e recuperação suave, espera por pacotes reenviados que acompanha o ping.
  - **README** com foco no PS5 e instalação pela RecompStore e Prospero Store.
- **v1.5.5**:
  - **Eliminação do Falso Positivo de "46 Novos Jogos" & Boot Instantâneo**: Persistência integral de todos os produtos do catálogo em `library_cache.json` (`knownProducts_`), cálculo estrito de adições reais ao catálogo e liberação imediata da Splash Screen (`showHome()`) com sincronização diferencial não-bloqueante em segundo plano.
  - **Cursor do Mouse sem Duplicação no Desktop**: Chamada a `SDL_HideCursor()` no backend SDL3, ocultando o cursor de hardware nativo do sistema operacional e preservando exclusivamente o cursor vetorial suave da interface.
  - **Atualização Automática de Estantes do Game Pass**: Sincronização em segundo plano dos feeds SIGL ("Novidades", "Mais populares", "Streaming gratuito", "Saindo em breve") no `syncDelta` sem necessidade de refresh manual (<kbd>L3</kbd>).
  - **Port Nativo PlayStation 4 Consolidado (`PSBX00001`)**: Decodificação por hardware via `libSceVideodec2`, composição 2D por software multithread SIMD SSE4.2 (cópia zero de overlay), suporte completo a DualShock 4 e empacotamento PKG oficial assinado.
- **v1.5.4**:
  - Suporte completo a mouse e navegação vetorial no PS5 e Desktop.
  - Fast-paths no renderizador 2D (cópia direta de scanlines e batch fill para texturas opacas).
  - Cache de fontes UTF-8 e glifos sem contenção de mutexes em $O(1)$.
  - Modularização estrutural de arquivos monolíticos (`app_ui.cpp`, `web_server.cpp`, `library.cpp`, `worker.cpp`).
  - Correção de redirecionamento pós-login e resgate imediato de títulos Free-to-Play.
- **v1.5.3**:
  - Testes autônomos de resgate de jogos na loja (`claimtest`) e inserção imediata no índice 0 de "Meus jogos".
  - Transição imediata para streaming com decodificação por hardware (`libSceVideodec2`), 120 Hz e haptics DualSense.
  - Reformulação da WebUI para "Smartglass WebUI" com logo oficial do Xbox Guide.
- **v1.5.2**:
  - Motor universal de aquisição de licenças gratuitas em 1-clique sem looping de carregamento.
  - Deduplicação inteligente Series X|S sobre Xbox One e remoção de títulos exclusivos de PC.
  - Bloqueio de inputs na tela de splash até a conclusão do login.
- **v1.5.1**:
  - Restauração completa da estante de jogos adquiridos e eliminação de duplicatas de títulos multi-edição.
  - Auto-recuperação de cache local corrompido (`library_cache.json`).
- **v1.5.0**:
  - WebUI 2.0 com suporte a controle virtual em tela cheia para Jogadores 1 a 4.
  - Botão de reset de fábrica com proteção de toque seguro de 5 segundos.
  - Sincronização em tempo real de biblioteca sem congelamentos de tela.
