<!-- LANG:PT -->
## Novidades e Correções no v1.5.2

### 🛍️ Aquisição Universal de Jogos Gratuitos e Persistência Imediata
- **Engine Universal de Aquisição**: Aquisição direta na loja para qualquer jogo gratuito ou de catálogo sem travar em loops de loading ou requisições pendentes.
- **Persistência Imediata na Biblioteca**: Títulos recém-adquiridos são imediatamente gravados no cache local (`mine` / `owned_`) e injetados na lista "Meus jogos" sem necessidade de reiniciar.
- **Ordenação por Recentes no Boot**: A biblioteca no boot agora respeita a ordem de recência preservada pelo cache local.

### 🎯 Filtros Estritos de Catálogo e Deduplicação Cross-Gen
- **Deduplicação Universal Series X|S > Xbox One**: Para jogos cross-gen disponíveis em ambas as versões, a versão Xbox Series X|S tem prioridade absoluta, evitando títulos duplicados na biblioteca e no catálogo.
- **Remoção de Títulos Exclusivos de PC**: Jogos exclusivos de PC sem suporte a streaming de console são rigorosamente filtrados do feed.
- **Respeito Estrito aos Feeds SIGL**: Cada estante (como 'Novidades', 'Mais Populares', etc.) agora reflete fielmente o feed SIGL oficial da Microsoft sem contaminações.
- **Artes e Metadados de Call of Duty**: Suporte completo para capas hero, logos e metadados oficiais dos jogos da franquia Call of Duty (MW4, Warzone, BO6).

### 🛡️ Splash Screen Protegida e Fim do Falso Popup
- **Splash Screen com Input Lock**: A tela de inicialização segura o usuário com inputs travados até que a autenticação e carregamento inicial estejam 100% prontos, prevenindo toques acidentais e race conditions.
- **Eliminação do Falso Popup de "24 Novos Jogos"**: Corrigido o cálculo delta de novos jogos no login que reportava falsamente 24 jogos adicionados devido a IDs de teste/placeholder.
- **Higienização Rigorosa de Logs**: Supressão de logs duplicados e ruídos em `xcloud.log`, mantendo métricas de rede, áudio e vídeo legíveis.

### 🎮 WebUI Estável & Polimento de Navegação
- **WebUI ABXY Estáticos**: Botões ABXY do controle virtual touch permanecem perfeitamente ancorados e estáticos durante interações.
- **Estabilidade do Cursor na Busca**: O foco de navegação por D-pad/teclado na tela de busca não perde mais a posição ao alternar entre resultados.
- **Arte Hero na Estante de Amigos**: Exibição da arte promocional Hero personalizada na estante de jogos dos amigos.

## Novidades e Correções no v1.5.1

### 🎮 Restauração Completa da Lista "Meus jogos"
- **Recuperação de Todos os Jogos da Conta**: Corrigida a regressão crítica introduzida no v1.5.0 onde a lista "Meus jogos" exibia apenas Call of Duty e Fortnite. Todos os títulos comprados e gratuitos do usuário voltam a aparecer normalmente.
- **Fim das Duplicatas de Call of Duty**: Removida a inserção em loop das dezenas de pacotes/DLCs de Call of Duty na biblioteca do usuário, mantendo apenas as entradas jogáveis legítimas (Modern Warfare 4 e Warzone).
- **Auto-cura de Cache Local**: O aplicativo agora detecta automaticamente se o arquivo `library_cache.json` no console foi corrompido por versões anteriores e reconstrói o catálogo limpo de forma transparente.

### ⚡ Atualização da Biblioteca (L3)
- **Atualização Forçada Confiável**: Pressionar `L3` ("Atualizar") agora executa uma sincronização completa com os servidores da Microsoft, garantindo que novos títulos adquiridos apareçam imediatamente.

## Novidades e Melhorias no v1.5.0

### ⚙️ Reorganização das Configurações
- **Nova Ordem Intuitiva**: As opções principais foram reorganizadas para priorizar rede, região e conexões:
  1. Idioma
  2. Região do servidor
  3. País do login
  4. Verificar meu login
  5. Teste de rede
  6. Resolução do streaming
  7. Decodificação de Vídeo
  8. Saída 120 Hz (VRR)
  9. Bitrate máximo
- **Botão Restaurar Configurações Padrões**: Novo botão vermelho estilizado com confirmação protegida de 5 segundos para restaurar as preferências padrão de fábrica com segurança.

### 🎮 Aquisição em 1-Clique ("Ainda não está na sua conta")
- **Botão Direto na Tela de Erro**: Ao tentar rodar jogos gratuitos ou launchers (como Call of Duty HQ / Black Ops 6 Launcher e FINAL FANTASY XIV Online Free Trial) que ainda não foram resgatados na conta Microsoft, a própria tela de erro oferece o atalho direto `✕ Adquirir Grátis`.
- **Sem Celular nem QR Code**: A licença gratuita é adquirida instantaneamente pela API da Microsoft Store no próprio PS5, abrindo a tela de detalhes com o botão "Jogar" liberado na hora.

### ⚡ Atualização Delta Instantânea da Biblioteca (L3)
- **Zero Travamento**: Pressionar `L3` ("Atualizar") ou iniciar o aplicativo não reconstrói mais a biblioteca do zero nem apaga o cache local. O app compara os títulos com o catálogo online e baixa apenas os metadados dos jogos novos de forma instantânea.
- **Feedback Transparente**: Notificação indicando exatamente a quantidade de novos jogos adicionados ou confirmando que a biblioteca já está em dia.

### 🔍 Filtro "Grátis" Refinado na Pesquisa
- **Ocultação de Jogos Já Adquiridos**: O filtro "Grátis" agora exibe apenas jogos gratuitos que você ainda NÃO tem na conta, facilitando descobrir novidades da loja.
- **Eliminação de Erros de Catálogo**: Títulos de pacotes, códigos ou tokens sem disponibilidade de compra direta (`CatalogAvailabilityDataNotFound`) não são mais marcados erroneamente como gratuitos.

### 📱 WebUI 2.0 & Controle Virtual Touch
- **Smartphone como Gamepad**: Controle virtual redesenhado com suporte a modo paisagem em tela cheia e modo retrato, permitindo jogar como Player 1 até Player 4 com ícones dinâmicos no PS5.
- **Painel de Configurações Web e Mapeamento**: Novo layout limpo em grade 3x2, sincronização bidirecional em tempo real e remoção de rolagens desnecessárias.

### 🔔 Notificações e Polimento Visual no Console
- **Notificações Limpas**: Remoção de toasts duplicados no centro e topo da tela ao sincronizar configurações ou adquirir jogos, mantendo avisos leves de debug no console.
- **Alinhamento Perfeito**: Textos, seletores e chevrons perfeitamente alinhados à direita no menu de configurações.

<!-- LANG:EN -->
## What's New and Fixed in Version 1.5.2

### 🛍️ Universal Free Game Acquisition & Instant Persistence
- **Universal Store Acquisition Engine**: One-click direct acquisition for any free store title without loading spinner deadlocks or pending request stalls.
- **Immediate Library Persistence**: Newly claimed games are instantly persisted to local storage (`mine` / `owned_`) and populated into "My Games" without needing an app restart.
- **Recent-First Ordering at Boot**: Local cache hydration on startup accurately preserves and applies "Recent First" ordering.

### 🎯 Strict Catalog Filtering & Cross-Gen Deduplication
- **Universal Cross-Gen Deduplication (Series X|S > One)**: Dual-generation titles prioritize Xbox Series X|S cloud offerings, eliminating duplicated tiles across catalog feeds and library.
- **PC-Only Game Filtering**: Non-streamable PC titles are strictly excluded from catalog shelves.
- **Strict SIGL Feed Compliance**: Curated shelves faithfully mirror Microsoft SIGL feeds without cross-shelf contamination.
- **Call of Duty Artwork & Metadata**: Full support for official hero art, logos, and descriptions for Call of Duty titles (MW4, Warzone, BO6).

### 🛡️ Guarded Splash Screen & False Popup Elimination
- **Guarded Splash Screen with Input Lock**: Startup splash retains user focus with locked inputs until sign-in and initial catalog sync complete, preventing premature input race conditions.
- **Eliminated False "24 New Games" Popup**: Fixed delta sync count calculation that erroneously alerted users about 24 new games on fresh boot.
- **Rigorous Log Sanitization**: Deduplication and noise filtering in `xcloud.log`, keeping telemetry, audio, and decode metrics crystal clear.

### 🎮 Stable WebUI & Navigation Polish
- **Static WebUI ABXY Layout**: Virtual touch controller ABXY layout remains solidly anchored and stable during touch gestures.
- **Search Cursor Stability**: D-pad and virtual keyboard navigation in the search screen reliably retains tile focus.
- **Friends Shelf Hero Artwork**: Friends shelf properly preserves and renders rich hero artwork.

## What's New and Fixed in Version 1.5.1

### 🎮 Full Restoration of "My Games" / Owned Titles
- **Account Games Recovered**: Fixed critical regression in v1.5.0 where "My Games" displayed only Call of Duty and Fortnite. All purchased and streamable games associated with the user's account are restored.
- **Removed Duplicate Call of Duty Entries**: Cleaned up the loop insertion that populated dozens of Call of Duty DLC/edition entries, retaining only clean playable titles (Modern Warfare 4 and Warzone).
- **Automatic Cache Self-Healing**: App detects if previous versions stored a contaminated `library_cache.json` and automatically triggers a fresh sync to heal local storage.

### ⚡ Manual Library Refresh (L3)
- **Reliable Full Refresh**: Pressing `L3` ("Refresh") now performs a full refresh from Microsoft endpoints, ensuring freshly acquired titles show up immediately.

## What's New in Version 1.5.0

### ⚙️ Settings Menu Reorganization
- **Intuitive Ordering**: Settings options have been rearranged prioritizing connectivity, region, and network:
  1. Language
  2. Server Region
  3. Login Country
  4. Check Sign-in
  5. Network Test
  6. Streaming Resolution
  7. Video Decoder
  8. 120 Hz Output (VRR)
  9. Max Bitrate
- **Reset to Defaults Button**: Added a dedicated red reset button at the bottom of Settings with an animated 5-second hold confirmation to safely restore default configurations.

### 🎮 1-Click License Acquisition ("Not on your account yet")
- **Direct Error Screen Claiming**: When launching free-to-play packages or launchers (such as Call of Duty HQ / Black Ops 6 Launcher or FINAL FANTASY XIV Online Free Trial) not yet registered to your Microsoft account, the error screen now features a direct `✕ Claim Free` action.
- **No External Browser Needed**: Entitlements are obtained autonomously via the Microsoft Store API from your console, opening the game page with the "Play" button unlocked immediately.

### ⚡ Instant Non-Destructive Delta Library Refresh (L3)
- **Zero Freezing**: Tapping `L3` ("Refresh") or logging in no longer flushes cache or freezes navigation. It performs a lightweight delta query against Xbox cloud endpoints, fetching metadata only for newly released games.
- **Clear Notifications**: In-app indicator shows how many new titles were added or confirms the library is completely up to date.

### 🔍 Polished "Free" Search Filter
- **Excludes Owned Games**: The "Free" filter now exclusively presents unacquired free titles, hiding games already present in your library.
- **Store Availability Guard**: Bundle tokens and products lacking a direct purchase action (`CatalogAvailabilityDataNotFound`) are filtered out to prevent purchase errors.

### 📱 WebUI 2.0 & Virtual Touch Gamepad
- **Smartphone Gamepad**: Fully responsive virtual controller featuring fullscreen landscape and portrait modes, allowing mobile devices to join as Players 1 through 4 with live icon indicators on the TV.
- **Streamlined Web Dashboard**: Clean 3x2 tab navigation, real-time bidirectional settings synchronization, and optimized touch reachability.

### 🔔 Clean Console Notifications & UI Alignment
- **De-cluttered Alerts**: Eliminated redundant toast overlays on PS5 during settings sync and license acquisition in favor of concise debug banners.
- **Pixel-Perfect Alignment**: Strict right-edge text and chevron alignment across all settings rows and cards.
