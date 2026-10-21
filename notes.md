## Novidades e Melhorias no v1.3.3

### 🎮 Interface e Detalhes do Jogo
- **Botão de Conquistas Clicável**: Nos detalhes do jogo, o badge de conquistas agora é um botão focável posicionado diretamente ao lado do botão **Jogar**, com navegação suave por Esquerda / Direita (<kbd>✕</kbd> Conquistas abre o modal interativo com seu progresso e Gamerscore).
- **Tela de Carregamento Inicial**: A tela de splash segura os inputs do DualSense até que a autenticação e a sincronização inicial da conta estejam concluídas, garantindo que o catálogo abra já com o perfil pronto.
- **Menu de Streaming Mais Limpo**: Removido o item de tempo jogado do menu do jogo durante a transmissão e eliminada a notificação técnica de debug ao sair de um jogo.

### ⚙️ Configurações e Diagnósticos
- **Apoie o Projeto via Ko-Fi**: Novo item não-intrusivo nas Configurações com botão que abre um modal com **QR Code** e mensagem para apoiar o desenvolvimento contínuo do projeto ([https://ko-fi.com/RafaelNGP](https://ko-fi.com/RafaelNGP)).
- **Estilo dos Botões de Ação**: Opções como *Verificar meu login*, *Teste de rede* e *Apoie o projeto* agora usam texto limpo e direto, sem as setas (chevrons) de dropdown ou pílulas ao redor do texto. O item de *Atualizações* fica fixado como o último da lista.
- **Teste de Rede Preciso**: O diagnóstico de latência nas Configurações agora utiliza probes TCP SYN diretos aos servidores edge de streaming mais próximos, medindo latências reais idênticas às da gameplay (ex: 4 ms no Brasil).

### 📚 Biblioteca ("Meus jogos")
- **Atualização Manual da Biblioteca**: O atalho <kbd>L3</kbd> **Atualizar** agora é exibido e suportado diretamente na aba *Meus jogos*.
- **Cabeçalho Limpo**: Removido o botão redundante ao lado de *Ordenar: ...* no topo da tela, mantendo apenas a indicação inferior com <kbd>R3</kbd>.
- **Atalhos L2 / R2 Aprimorados**: A navegação rápida entre seções foi ajustada com badges individuais: `[ L2 ] | [ R2 ] Seções`.

---
### 📦 Assets da Release
- `PPSA99810.zip`: Pacote oficial limpo pronto para instalação no PS5 via ProsperoStore / Direct Package Installer.
- `PPSA99810.zip.sig`: Assinatura criptográfica ECDSA para o in-app updater oficial.
- `PPSA99810.zip.sha256`: Checksum SHA-256 de integridade.
