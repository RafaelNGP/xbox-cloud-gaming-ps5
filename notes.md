<!-- LANG:PT -->
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
