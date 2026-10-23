# Diretrizes de Desenvolvimento e Deploy no PS5 (xCloud-PS5)

Este documento estabelece regras operacionais estritas para qualquer agente de IA trabalhando no repositório **xCloud-PS5** e no ambiente do console PlayStation 5.

---

## 1. Regra Fundamental: Zero Blind Deploys

- **NUNCA solicite ao usuário que teste no console alterações de UI ou comportamento sem validação prévia.**
- **NUNCA assuma que o layout de tela está correto apenas pelo código C++.** Cálculos de clipping, safe-area, matrizes de projeção e renderização Vulkan no PS5 podem divergir do esperado na tela.
- **Toda e qualquer alteração de UI deve ser verificada visualmente pelo agente** através de screenshots capturadas pelo console antes de considerar a tarefa concluída ou entregar para o usuário.

---

## 2. Fluxo Obrigatório de Validação Autônoma de UI

Para qualquer alteração em arquivos de interface (`src/app_ui.cpp`, `src/app/autoplay.cpp`, shaders, renderizadores 2D):

1. **Parar a aplicação em execução**:
   ```bash
   PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py kill PPSA99810
   ```
2. **Compilar e Empacotar**:
   ```bash
   ninja -C build-ps5 xcloud_app
   bash tools/ps5/link.sh build-ps5
   XC_INCLUDE_ACCOUNT=1 bash tools/ps5/package.sh build-ps5
   ```
3. **Enviar a nova build ao console**:
   ```bash
   PS5_HOST=192.168.15.17 ./tools/ps5/deploy.sh build-ps5
   ```
4. **Configurar teste autônomo (`autoplay.txt`)**:
   - Enviar `/data/homebrew/PPSA99810/autoplay.txt` via FTP.
   - **Regra obrigatória para testes de UI**: Sempre utilize o prefixo `AUTOTEST` (ex: `AUTOTEST 30 gamesettingstest`, `AUTOTEST 30 librarytest`, etc.) para testes de telas/modais sem streaming. NUNCA use nomes arbitrários no primeiro argumento que possam ser interpretados como IDs de jogos pelo loop de streaming (`worker.cpp`).
   - Remover capturas `.ppm` antigas antes de iniciar.
5. **Iniciar a aplicação remotamente**:
   ```bash
   PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py launch PPSA99810
   ```
6. **Coletar e Inspecionar os Resultados Visuais**:
   - Baixar os arquivos `.ppm` (`ui.ppm`, `grid1.ppm`, `grid2.ppm`, `launch.ppm`, etc.) via FTP para um diretório local temporário.
   - Converter `.ppm` para `.png` utilizando Python (`PIL.Image`).
   - Usar a ferramenta `view_file` para analisar visualmente a imagem gerada.
   - Verificar conformidade: clipping, centralização, padding, corte de texto, contraste e consistência de grade.
7. **Limpeza e Restauração de Modo Interativo**:
   - Excluir `/data/homebrew/PPSA99810/autoplay.txt` via FTP para que a aplicação não inicialize em modo autoplay caso o usuário queira usá-la interativamente.
   - Matar e reiniciar o app se for entregar o controle ao usuário:
     ```bash
     PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py kill PPSA99810
     PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py launch PPSA99810
     ```

### 2.1 Armadilhas de Autoplay e Persistência de Configurações
- **Proteção Automática de Configurações (`guardSettingsForAutoplay`)**: Qualquer teste rodando com `autoplay.txt` faz backup das configurações (`settings.autoplay-backup.json`). Ao reiniciar sem `autoplay.txt` (modo interativo), as configurações originais do usuário são restauradas e o backup é apagado. Não assuma que alterações gravadas durante um autotest persistirão após a remoção do `autoplay.txt`.
- **Seleção de Tiles em Testes de Detalhes**: Nunca assuma que o primeiro card da tela é um jogo jogável com botão 'Jogar'. A biblioteca pode posicionar linhas de jogos gratuitos para adquirir no topo (*Faça streaming de jogos gratuitos*). Em testes de modais de jogo/detalhes, especifique o título alvo (`locktitle=` ou targeting explícito de título adquirido) para evitar abrir telas de aquisição da loja acidentalmente.

---

## 3. Higienização Mandatória do Console

- O arquivo `autoplay.txt` nunca deve permanecer no console após a finalização de testes automatizados.
- Se a aplicação estiver em execução e você precisar enviar novos arquivos, **SEMPRE mate o processo primeiro** (`kill PPSA99810`) antes de enviar o binário sobre FTP para evitar arquivos corrompidos ou locks em `eboot.bin`.

---

## 4. Preservação de Código e Prevenção de Regressões

- **NUNCA execute `git restore .` ou `git checkout -- .` de forma indiscriminada.**
- Antes de qualquer descarte ou reversão de alterações:
  - Inspecione `git diff` detalhadamente.
  - Se houver trabalho em andamento que precise ser isolado, utilize `git stash` ou crie uma branch temporária de segurança.
- Mantenha commits atômicos, focados e com mensagens claras.

### 4.1 Protocolo de Reversão de Emergência / Contexto Esgotado
- Se for solicitada a reversão para uma versão anterior ou release estável:
  1. **Isolamento Seguro**: Comite as alterações em branch temporária/feature: `git commit -am "wip: ..."` (NUNCA descarte com `git checkout -- .`).
  2. **Restauração Limpa**: Alterne para a `main` e garanta que o working tree esteja sincronizado e limpo na tag oficial (`git checkout main`).
  3. **Rebuild e Re-deploy no PS5**: Recompile, empacote e envie o binário da release oficial ao console via FTP.
  4. **Higienização Completa**: Remova `autoplay.txt` e relance o aplicativo em modo interativo foreground para o usuário.

### 4.2 Congelamento de Fluxos Estabelecidos e Modificações Cirúrgicas
- **Regra de Não-Interferência**: Qualquer alteração futura deve respeitar integralmente o fluxo já existente e consolidado. NUNCA altere comportamentos, layouts ou lógicas de módulos estáveis a menos que o usuário solicite explicitamente aquela mudança pontual.
- **Fluxos Blindados e Intocáveis (Preservação Mandatória)**:
  1. **Organização e Distribuição de Jogos**: Estantes oficiais SIGL (Novidades, Mais Populares, Free to Play, Saindo em Breve), catálogo geral e deduplicação Series X|S sobre Xbox One.
  2. **Aquisição e Compra na Loja**: Motor universal de aquisição de jogos gratuitos em 1-clique, transição imediata para "Jogar", e inserção imediata no topo da lista sem exigir <kbd>L3</kbd>.
  3. **Splash Screen e Retenção de Inputs**: Prender o usuário na tela de inicialização com inputs completamente travados até o login e sincronização inicial estarem concluídos.
  4. **Ordenação de Recentes**: Aba "Meus jogos" ordenada por recentes logo no primeiro frame do boot pelo cache persistente.
  5. **Telas e Menus de Configurações**: Ordem definida dos 9 itens principais, alinhamento visual de textos/chevrons, botão de reset protegido (5s) e sincronização bidirecional com a WebUI.
- **Escopo Estritamente Delimitado**: Desenvolva apenas o que foi solicitado na tarefa atual, sem refatorações periféricas ou "melhorias espontâneas" não autorizadas.

---

## 5. Gestão de Contexto e Uso de Subagentes

Para evitar degradação de desempenho, latência alta e manter a interação ágil e leve:

- **Delegação Mandatória de Tarefas Pesadas**:
  - **Pesquisa e Varredura de Código**: Delegar para subagentes (`research` ou `self`) tarefas que envolvam ler múltiplos arquivos, buscar padrões no codebase ou analisar grandes trechos de código/documentação.
  - **Análise de Logs Extensos**: Usar subagentes para processar ou depurar saídas longas (builds, WebRTC, traces), retornando apenas o diagnóstico e resumo executivo ao contexto principal.
  - **Inspeção de Imagens e Testes Intermediários**: Quando houver rotinas com múltiplas validações de telas ou passos intermediários de build, priorizar subagentes para evitar inflar o histórico principal com tokens de imagens e logs.
- **Saídas Enxutas no Contexto Principal**:
  - Evitar despejar comandos com saídas verbosas desnecessárias (usar filtros, flags resumidas ou subagentes).
  - O agente principal deve atuar na orquestração, decisão e interação com o usuário, preservando o contexto limpo e rápido.

---

## 6. Protocolo Obrigatório de Publicação de Releases (In-App Updater)

- **NUNCA crie uma release no GitHub sem anexar os assets assinados.** O in-app updater do console (`src/app/updater.cpp` e `src/app/worker.cpp`) rejeita qualquer versão que não possua tanto o pacote `.zip` quanto a assinatura criptográfica `.sig`.
- **Assets Mandatórios em TODA Release (`vX.Y.Z`)**:
  1. `PPSA99810.zip` (pacote limpo sem contas/tokens, empacotado sem `XC_INCLUDE_ACCOUNT`).
  2. `PPSA99810.zip.sig` (assinatura ECDSA gerada com `~/.config/psbox-release/signing-key.pem`).
  3. `PPSA99810.zip.sha256` (checksum para integridade).
- **Fluxo Mandatório de Publicação**:
  1. Delegar o empacotamento e assinatura para subagente (preservando o contexto).
  2. Gerar o pacote através de `tools/release.sh <X.Y.Z>` ou do pipeline de empacotamento oficial.
  3. Publicar ou anexar com `gh release create vX.Y.Z PPSA99810.zip PPSA99810.zip.sig PPSA99810.zip.sha256 ...` (ou `gh release upload --clobber`).
  4. **Verificação Obrigatória**: Sempre executar `gh release view vX.Y.Z` e confirmar que todos os 3 assets estão listados e públicos antes de considerar o release finalizado.

---

## 7. Protocolo Obrigatório de Otimização de Performance (Benchmarking A/B no Console)

Para qualquer proposta de melhoria de desempenho, redução de latência ou otimização de pipeline (vídeo, áudio, GPU, rede ou entrada):

- **Proibição de Otimizações Teóricas**: Nenhuma alteração de performance deve ser aceita apenas com base em estimativas ou testes sintéticos de desktop.
- **Benchmarking Comparativo Mandatório (Original vs Modificado)**:
  1. **Linha de Base (Original / Baseline)**:
     - Executar uma sessão de teste no console PS5 sob condições fixadas (ex: 60 ou 120 segundos de gameplay ativa em jogo padrão como Fortnite).
     - Coletar as métricas de referência do log oficial: `decodeMs` (média/pico), `drawMs` (GPU), `onScreenMs` (latência de tela), quadros descartados (`skipped`/`late`), recuperação de pacotes (`lost`/`recovered`), estabilidade da fila de áudio (`queued ms`, `empty grains`, `ms cut`) e taxa de relatórios de entrada.
  2. **Medição da Versão Modificada**:
     - Compilar, empacotar e enviar a build modificada para o console PS5.
     - Executar o teste sob **exatamente as mesmas condições** (mesmo jogo, mesma cena/mapa, mesma duração de amostragem, mesma taxa de bits configurada).
  3. **Relatório Comparativo Formal**:
     - Apresentar tabela lado a lado com os números: **Original vs Modificado**.
     - Apenas aceitar a modificação se os dados comprovarem ganho real mensurável sem regressões visuais, de áudio ou de estabilidade.
