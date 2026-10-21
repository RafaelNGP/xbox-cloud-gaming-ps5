## Novidades e Melhorias no v1.3.4

### 🔊 Alerta Sonoro de Fila no DualSense (Queue-Ready)
- **Áudio no Alto-falante do Controle**: O aviso sonoro de jogo pronto na fila agora toca diretamente pelo alto-falante integrado do DualSense (`PADSPK`), além do sinal tátil (vibração).
- **Sem Popups de Debug**: Ao testar o som do alerta nas Configurações pressionando <kbd>▢</kbd>, o som é reproduzido de forma limpa pelo controle, sem exibir popups ou toasts técnicos na tela.

### ⭐ Lista de Desejos (Wishlist)
- **Acesso Dedicado pela Pesquisa**: Para acessar a lista de desejos, abra a pesquisa na aba **"Meus jogos"** e ative o filtro **"Lista de desejos"**.
- **Preços Atualizados e Estado Vazio**: Exibe apenas os jogos marcados com seus valores da loja atualizados em tempo real, ou a mensagem *"A lista de desejos está vazia"* caso não haja itens favoritados.
- **Destaque na Biblioteca**: Jogos da lista de desejos recebem um selo com **estrela azul** no canto superior direito do card na seção *"Disponíveis para comprar"* e são fixados sempre no **topo da lista** em qualquer tipo de ordenação.
- **Regra de Adição**: Apenas títulos não adquiridos podem ser adicionados à lista de desejos.

### 🎨 Diagramação Visual e Padronização
- **Separação de Selos nos Cards**: O selo do console (`SERIES X|S`, `XBOX ONE`) foi ajustado para largura proporcional compacta, e o selo de controle/teclado foi centralizado, eliminando qualquer colisão ou sobreposição com os selos de preço e jogos gratuitos.
- **Alinhamento dos Rótulos de Ordenação**: Os textos *"Ordenar: Recentes primeiro"*, *"Ordenar: A-Z"* e *"Ordenar: Por console"* agora estão alinhados de forma precisa com as informações do perfil do usuário no cabeçalho superior direito.
- **Padronização dos Botões da UI**: Os atalhos <kbd>L3</kbd> (*Atualizar*) e <kbd>R3</kbd> (*Ordenar*) foram padronizados na mesma geometria arredondada de pílula dos botões <kbd>L2</kbd> / <kbd>R2</kbd>.

### ⚙️ Ajustes nas Configurações
- **Remoção de Opção Obsoleta**: Removida a opção redundante *"Volume do jogo"*.
- **Reorganização de Controles**: A categoria Controles agora segue a ordem intuitiva:
  *Botões do controle → Teclas do teclado → Zona morta dos analógicos → Mira por giroscópio → Vibração dos gatilhos → Botão de confirmar → Barra de luz → Aviso sonoro de fila pronta*.

---
### 📦 Assets da Release
- `PPSA99810.zip`: Pacote oficial limpo pronto para instalação no PS5 via ProsperoStore / Direct Package Installer.
- `PPSA99810.zip.sig`: Assinatura criptográfica ECDSA para o in-app updater oficial.
- `PPSA99810.zip.sha256`: Checksum SHA-256 de integridade.
