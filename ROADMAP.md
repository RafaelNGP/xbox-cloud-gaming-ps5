# Roadmap — PSBox Cloud Gaming

Planejamento e visão de futuro do projeto **PSBox Cloud Gaming**, priorizado por valor para o usuário no console PlayStation 5 (e ecossistema PlayStation).

Status: **Em andamento (next)**, **Planejado (planned)**, **Futuro (later)**, **Descartado (dropped)**.

> **Última atualização:** 30/10/2026 (release estável **v1.5.5** / ecossistema multi-plataforma).

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

---

## 🚀 Em Andamento e Próximos Passos (Now & Next)

### 1. Port Nativo para PlayStation 4 (PSBox Cloud Gaming - PS4) — *done (v1.5.5)*
Port dedicado e otimizado para consoles PlayStation 4 com jailbreak (firmwares 9.00 / 11.00):
- **Pacote e Identidade Isolados**: Pacote PKG independente (`PSBX00001.pkg`) com `TitleID` próprio, evitando qualquer interferência ou colisão com a versão de PS5 (`PPSA99810`).
- **Conectividade WebRTC**: Stack ICE, STUN e handshake DTLS adaptados com compatibilidade FreeBSD/Orbis libc++ (`ETIMEDOUT=60`), handshake de certificados MbedTLS e canais de dados SCTP.
- **Pipeline de Vídeo e Áudio**: Decodificação H.264 por hardware via `libSceVideodec2` em Direct Memory Onion/Garlic, renderizador 2D por software com 4 threads SIMD SSE4.2 (cópia zero de overlay via `shared_ptr`) e saída de áudio DMA com dreno limpo.
- **Controles e Periféricos**: Suporte completo ao DualShock 4 com rumble de motores duplos, lightbar, touchpad e giroscópio.

### 2. Suporte Nativo a Controles USB no PS5 (8BitDo 2.4G, Xbox, DS4, Fightsticks) — *next*
Leitura direta de nós USB HID e interrupção no kernel do PS5 (`/dev/uhid*`, `/dev/ugen*`), contornando a whitelist restritiva da `libScePad` da Sony que exige coprocessadores criptográficos proprietários:
- **8BitDo Ultimate 2.4G**: Suporte nativo ao dongle USB em modo D-Input e X-Input.
- **DualShock 4 USB**: Liberação de controles de PS4 no PS5 para Jogador 1 ou multiplayer local (P2..P4).
- **Controles Xbox**: Suporte a Xbox 360 (com fio ou receptor sem fio USB), Xbox One e Series via USB-C.
- **Fightsticks & Arcade**: Mapeamento direto de controles arcade HID genéricos de 14/16 botões.

### 3. Multi-Phone WebUI Controllers (P1..P4) & Layout Studio — *next*
Expansão do servidor local integrado (porta 8080) para multiplayer cooperativo local sem custos de hardware:
- **Suporte a até 4 Smartphones Simultâneos**: Seleção dinâmica de slot (`P1`, `P2`, `P3`, `P4`) via `/gamepad`, heartbeat automático de desconexão e transmissão binária via WebSocket (`/ws`) com latência < 5 ms.
- **PSBox Layout Studio**: Editor visual de controles na WebUI com pré-visualização WYSIWYG, ajuste de botões analógicos e gatilhos para telas de toque, salvamento de perfis (`portrait`/`landscape`) e persistência imediata.

---

## 📋 Planejado para o Futuro (Planned & Later)

### 1. Sincronização de Conquistas Xbox para Troféus PlayStation (PS5/PS4) — *planned*
Espelhamento contínuo das conquistas obtidas no Xbox Cloud Gaming para o subsistema local de troféus da conta PlayStation:
- **Loop de Monitoramento Ativo (`worker_stream.cpp`)**: A cada 20 segundos de gameplay ativo, consulta a API Xbox Live (`fetchTitleAchievements(xboxTitleId, xuid)`) e compara os status com snapshot mantido em disco e memória.
- **Mapeamento de Gamerscore para Graus de Troféus**:
  - `5G` a `15G` ──► Troféu **Bronze**
  - `20G` a `45G` ──► Troféu **Prata**
  - `50G` a `100G` ──► Troféu **Ouro**
  - `100% dos Achievements` do jogo obtidos ──► Troféu **Platina**
- **HUD & Notificação em Tempo Real**: Banner translúcido animado exibido no topo da tela com o ícone original da conquista, nome, descrição, pontuação Gamerscore e reprodução do áudio característico de desbloqueio de troféu.
- **Integração com Subsistema Local PlayStation**: Gravação dos registros no banco local do console (`/user/home/<id>/trophy/db/trophy_local.db`) ou chamadas `sceNpTrophy` no console desbloqueado.
- **Retro-Sincronização (Back-Sync)**: Botão dedicado na tela de detalhes do título para sincronizar retroativamente todas as conquistas já obtidas na conta para a base de troféus do console.

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

### 5. Acompanhamento de Quota Mensal de Nuvem — *later*
- Suporte no cliente já implementado (`AuthManager::fetchPlaytimeLimit`); aguardando ativação oficial das quotas mensais pela Microsoft para exibir tempo restante no perfil.

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

- **v1.5.5** *(Atual)*:
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
