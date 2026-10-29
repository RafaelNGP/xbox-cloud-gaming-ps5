# Roadmap — PSBox Cloud Gaming

Planejamento e visão de futuro do projeto **PSBox Cloud Gaming**, priorizado por valor para o usuário no console PlayStation 5 (e ecossistema PlayStation).

Status: **Em andamento (next)**, **Planejado (planned)**, **Futuro (later)**, **Descartado (dropped)**.

> **Última atualização:** 26/10/2026 (após lançamento da versão estável **v1.5.4**).

---

## 🐛 Bugs Prioritários & Correções Imediatas (High-Priority Fixes)

### 1. Falso Positivo de "46 Novos Jogos" & Otimização do Tempo de Splash Screen — *next*
- **Sintoma Atual**: A aplicação exibe recorrentemente em cada boot a notificação de debug *"Biblioteca atualizada (46 novos jogos)"* (falso positivo na sincronização diferencial `syncDelta`), mantendo o usuário retido na Splash Screen durante todo o bloqueio de rede até que a verificação remota termine e essa notificação seja emitida.
- **Requisitos & Estratégia de Solução**:
  - **Preservação Mandatória da Splash Screen**: A tela de Splash **NÃO PODE** ser excluída ou desativada; ela deve permanecer ativa para garantir inicialização limpa e segura do ecossistema.
  - **Persistência de Títulos no Cache Local**: Guardar o conjunto de IDs/produtos já conhecidos em cache persistente (`library_cache.json`), garantindo que o contador diferencial reflita apenas adições genuinamente novas ao catálogo do Xbox Cloud.
  - **Fim da Retenção no Boot**: Falsos positivos não devem impedir nem atrasar a transição de login para a tela inicial (`showHome()`). O carregamento inicial da UI deve utilizar o cache persistente com entrada instantânea, realizando a validação de novos títulos em segundo plano sem travar o usuário no splash.

### 2. Cursor do Mouse Duplicado na Build Desktop — *next*
- **Sintoma Atual**: Na versão Desktop (`psbox` para Linux / Steam Deck / Windows), tanto o cursor de hardware do sistema operacional gerenciado pelo SDL3 quanto o cursor vetorial de software desenhado pela interface (`AppUi::renderSoftwareCursor`) aparecem simultaneamente, causando a exibição de dois cursores de mouse sobrepostos na tela.
- **Solução**: Ocultar o cursor nativo do sistema operacional (`SDL_HideCursor()`) na inicialização do backend desktop ou desativar o cursor duplicado com base nas capacidades de janela do ambiente (`shell::caps().window`).

---

## 🚀 Em Andamento e Próximos Passos (Now & Next)

### 1. Port Nativo para PlayStation 4 (PSBox Cloud Gaming - PS4) — *next*
Port dedicado e otimizado para consoles PlayStation 4 com jailbreak (firmwares 9.00 / 11.00):
- **Pacote e Identidade Isolados**: Pacote PKG independente (`CUSA99810.pkg`) com `TitleID` próprio, evitando qualquer interferência ou colisão com a versão de PS5 (`PPSA99810`).
- **Conectividade WebRTC**: Stack ICE, STUN e handshake DTLS adaptados com compatibilidade FreeBSD/Orbis libc++ (`ETIMEDOUT=60`), handshake de certificados MbedTLS e resolução de SCTP para canais de controle.
- **Pipeline de Vídeo e Áudio**: Decodificação H.264 por hardware via `libSceVideoDec` / decodificador FFmpeg e renderização Vulkan via libpiglet / GNM.

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

### 4. Presença Visual e Banners em Lojas (RecompStore & Prospero Store) — *planned*
- Atualização do catálogo oficial com as artes em alta definição (512x512 Icon, 1080p/4K Hero Banners e screenshots oficiais de jogos como *Halo Infinite* e *Fortnite*).
- Manutenção contínua de metadados, links de releases assinadas e sincronização de atualizações automáticas via API.

### 5. Atalhos Diretos na Tela Inicial do PS5 (Home-Screen Forwarders) — *later*
- Criação de atalhos individuais na dashboard principal do PS5 para iniciar títulos específicos em nuvem diretamente, utilizando o formato ProsperoLight forwarder.

### 6. Acompanhamento de Quota Mensal de Nuvem — *later*
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

- **v1.5.4** *(Atual)*:
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
