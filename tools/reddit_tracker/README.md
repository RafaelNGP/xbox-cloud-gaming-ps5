# 📊 Reddit Feedback Timeline Extractor (PSBox / AI Tracker)

Ferramenta desenvolvida para extrair todos os comentários de uma thread do Reddit em uma **linha do tempo cronológica com data/hora exatas, deltas em relação à publicação e categorização automática de bugs e relatos**.

Isso permite acompanhar com precisão cirúrgica o feedback da comunidade, correlacionar comentários com horários de builds/releases e identificar quando um problema começou ou quando uma solução foi validada.

---

## 🚀 Como Usar

Você tem duas formas extremamente simples de usar:

### Opção 1: Bookmarklet (Instantâneo, Sem Instalação de Extensão)
Funciona em qualquer navegador (Chrome, Brave, Edge, Firefox, Opera) em 10 segundos:

1. No seu navegador, crie um novo Favorito/Marcador na barra de favoritos (ou edite um existente).
2. Dê o nome: **`Extract Reddit Timeline`**.
3. No campo **URL / Endereço**, cole exatamente o código contido no arquivo [`bookmarklet.js`](./bookmarklet.js) (iniciado com `javascript:(function(){...})();`).
4. **Pronto!** Ao abrir qualquer post do Reddit (ex: no `r/PS5_Jailbreak` ou `r/ps4homebrew`), clique no favorito:
   - Um modal escuro completo se abrirá na tela exibindo a linha do tempo e filtros por categoria.

---

### Opção 2: Extensão de Navegador (Chrome / Brave / Edge)
Para quem prefere um botão permanente que surge automaticamente nas páginas de posts do Reddit:

1. Abra a página de extensões do navegador:
   - No Chrome / Brave: `chrome://extensions`
   - No Edge: `edge://extensions`
2. Ative a chave **"Modo do desenvolvedor"** (*Developer mode*) no canto superior direito.
3. Clique no botão **"Carregar sem compactação"** (*Load unpacked*).
4. Selecione a pasta deste repositório:
   `/home/notebook-storm/Documentos/PS5 Ports/xCloud-PS5/tools/reddit_tracker`
5. **Pronto!** Em qualquer post do Reddit, um botão verde flutuante **`💬 Extract Timeline`** aparecerá no canto inferior direito.

---

## 🎯 Recursos e Tags Automáticas

A ferramenta analisa o texto de cada comentário e aplica tags contextuais:
- 🚨 **Crash / Error**: Detecta menções a `CE-34878-0`, travamentos, tela azul, panics ou encerramentos.
- 🕹️ **PS4**: Comentários específicos sobre PS4 Pro, Slim, Fat ou Orbis.
- 🎮 **PS5**: Comentários específicos sobre PS5 e Vulkan.
- ⚙️ **Firmware**: Versões de firmware (`11.00`, `9.00`, `13.02`, GoldHEN, PPPwn).
- ✅ **Working**: Relatos de sucesso, streaming liso ou jogo funcionando.
- ❓ **Logs / Diag**: Menções a FTP, `boot.log`, `xcloud.log`, `/data/psbox`.
- 👑 **OP**: Respostas do próprio autor do post (você).

---

## 📋 Como fornecer o feedback para o Antigravity (IA)

1. No modal aberto no Reddit, clique no botão:
   👉 **`📋 Copy for AI (Antigravity)`**
2. Cole diretamente na conversa aqui no Antigravity!
3. O relatório virá estruturado em Markdown com a data/hora local, deltas em minutos/horas, autor e o texto formatado.
4. O Antigravity correlacionará os relatos com os commits e releases para identificar imediatamente o que funcionou ou o que causou regressão.

---

## 💻 Script CLI Python Companion (`summarize.py`)

Se você baixar o arquivo `.json` (botão `💾 Download JSON`) e salvar na pasta `reddit/`:

```bash
# Ver resumo executivo no terminal da última exportação salva em reddit/
python3 tools/reddit_tracker/summarize.py

# Comparar duas exportações e ver apenas os COMENTÁRIOS NOVOS que chegaram:
python3 tools/reddit_tracker/summarize.py --diff reddit/ontem.json reddit/hoje.json
```
