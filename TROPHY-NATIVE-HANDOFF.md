# Troféus nativos no PSBox (xCloud-PS5) — Handoff técnico

> **Público-alvo:** um desenvolvedor (ou agente) dando continuidade à feature de troféus
> no console PS5 13.60 jailbroken do dono do projeto. Documento de estado + achados +
> referências. Não é um manual de bypass passo-a-passo; a implementação do bypass de
> aceitação do daemon é via o método público (PHU, MIT) referenciado no fim.

Data: 2026-11-03. Firmware alvo: **13.60** (jailbreak Relapse, público; kstuff + ShadowMountPlus + nanoDNS).

---

## 1. Objetivo

Exibir, no **perfil local** do PS5, as conquistas que o usuário ganhou jogando via
streaming no PSBox Cloud Gaming (xCloud) — nome do jogo, ícone, lista de troféus, grau,
desbloqueado/bloqueado — **nativamente** (notificação do sistema + coleção de Troféus),
como se fossem troféus de um jogo instalado.

Cenário: **100% offline** dos servidores da Sony (nanoDNS bloqueia). Nada sincroniza com
a PSN. As conquistas são reais (ganhas pelo usuário em serviço oficial Xbox); o objetivo
é só espelhá-las localmente, de forma organizada. Aceita-se marcar como local/não-sync.

---

## 2. Como o sistema de troféus do PS5 funciona (conceitual)

Dois subsistemas DISJUNTOS dentro do **SceShellCore** (o daemon de troféus, NPXS40082):

- **(B) Desbloqueio** — encena o valor do troféu no contexto por-usuário e grava os bytes
  em disco via um **serviço externo** de troféus (IPC). É o que `sceNpTrophyUnlockTrophy`
  (jogo real) e as variantes System/Debug tocam.
- **(A) Coleção / exibição** — é o que faz o título + troféus APARECEREM em Perfil →
  Troféus e dispara o popup. Num console normal, o **serviço externo** dispara esse passo
  de volta no SceShellCore DEPOIS de um desbloqueio aceito.

Os dois NÃO se cruzam no grafo de chamadas. Desbloquear sem passar pela via (A) grava
dados mas **não aparece nada**.

Armazenamento:
- Config (lista de troféus): `TROPHY.UCP` / `trophy00.ucp` + `npbind.dat` em
  `/system_data/priv/appmeta/<TitleID>/trophy2/` e `/user/trophy2/nobackup/conf/<NPWR>/`.
- Estado por-usuário: `TRPTITLE.DAT` em `/user/home/<uid>/trophy2/nobackup/data/<NPWR>/`.
- A **coleção que a UI lê** NÃO são esses arquivos crus — é um índice em memória/selado
  mantido pelo serviço; a entrada é criada pela via (A).

---

## 3. Diagnóstico — por que a implementação atual NÃO funciona

Testado e verificado neste console:

1. **Usa o backdoor de debug** (`sceNpTrophy2SystemDebugUnlockTrophy`). Ele retorna
   `rc=0` mas está no subsistema (B) — **nunca** toca a coleção. Por isso "tudo dá certo
   nos logs e nada aparece". (Confirmado: 22 unlocks rc=0, nada no perfil.)
2. **`RegisterContext` já é aceito** (rc=0) para o título — mas registrar sozinho coloca o
   contexto na tabela interna por-ctx, **não** na coleção. (Confirmado: registramos e o
   usuário checou o perfil — não apareceu.)
3. **Reboot/rescan não ingere** os arquivos crus (confirmado pelo usuário).
4. **Religar a função da via (A)** não teve efeito — ela só é alcançável pela via de
   comando/serviço que o nosso fluxo não aciona.
5. **O unlock normal** (`sceNpTrophy2UnlockTrophy`) **crashou o app** quando chamado da
   nossa casca — provável símbolo não exportado / NID errado, OU a função falha porque o
   título não está "provisionado" o suficiente.

**Por que os dumps do usuário funcionam e a nossa casca não:** um dump é um jogo real,
com NPWR/commId/pacote de troféus **provisionados pela Sony** e rodando sob a identidade
real do título. O serviço aceita o desbloqueio dele e dispara a via (A). A nossa casca
(nuvem / homebrew) tem dados **fabricados** e/ou identidade de processo diferente → o
serviço não dispara a via (A) para ela sem ajuste no daemon.

---

## 4. Arquitetura mapeada (achados de RE deste console, fw 13.60, mapbase 0x3059c000)

Endereços como offset-de-mapbase (ao vivo = mapbase + offset). São ACHADOS factuais da RE,
para orientar quem continuar — não um procedimento de patch.

- Via (A) de exibição: handler de comando `0xe232e0` → `0xe1a4c0` → ingest no cache da
  coleção `call [cache_vtable+0x30]` (= `0xe39310`) → fan-out `0xe34e70` → `0xe330a0` →
  `0xe1f8f0` (porta do ShellUI).
- `0xe1a4c0` só é alcançável de `0xe23487` (dentro de `0xe232e0`) e de `0xba7ce5`.
- Serviço: ponteiro singleton em `[mapbase+0x22269a0]`; cache da coleção em `[svc+0x80]`
  (vtable `0x2035d60`, mapa de 32 buckets em `cache+0x18`, flags em `+0xc/+0xd/+0xe`);
  vtable do serviço `0x2035900`; init do serviço `0xe36250` registra handlers via
  `0xe1d420/0xe1d6e0/0xe1d9b0/0xe1dc80/0xe1df50`.
- Subsistema (B) de unlock: handlers ~`0xe59610/0xe59b50/0xe5b110` → updater `0xe63570`
  → worker de registro `0xe65c10`.
- `RegisterContext` = IPMI cmd `0x90016`.
- Placeholder de commId `AAAA00000` (alvo "B1" do PHU) presente na rodata do 13.60, **não**
  patchado — confirma que o setup base do usuário não faz esse patch.

Dumps descriptografados do SceShellCore 13.60 capturados (base para a RE da via de unlock):
no diretório de scratchpad da sessão — `sc_text.bin` (mb+0xb00000), `sc_rodata.bin`
(mb+0x1a00000), `sc_data.bin` (mb+0x1f00000). Payload read-only de dump:
`tools/ps5/payload_patch_test/dump_shellcore.c`.

---

## 5. O caminho conhecido e PÚBLICO (referência de implementação)

A cena homebrew já resolve "troféus de jogos dumpados aparecerem nativamente":

- **ArkSama/PS5-PHU-Trophy-System** (MIT) — https://github.com/ArkSama/PS5-PHU-Trophy-System
  "PS5 trophy install + unlock for dumped games", 24 firmwares (4.03 → 12.70). Documenta,
  em `README.md` + `WORKFLOW.md`, exatamente como o daemon aceita/rejeita e onde ficam os
  pontos de ajuste ("B-patches": strings de commId, inventário de app, gate do LncManager,
  injeção de commId client-side no NpAsmClient do processo do jogo, e o register vindo do
  processo do jogo). **Esse repo é a referência de método.**
- **kauansilvalopes/trophy** (Trophy Manager) — app nativo no console, sem PC/payload.
- KIK-Trophy (edita TRPTITLE.DAT), PS5TrophyExtract0r (extrai trophy00.ucp), Hamad3bdulla/PS5-Trophy-Unlocker-.

**Importante sobre 13.60:** o PHU para no 12.70. Para 13.60, os offsets equivalentes
precisam ser **re-derivados** pelo MESMO método do PHU, usando os dumps da seção 4 como
ponto de partida. Um perfil 13.60 "diagnóstico" (sem offsets chutados) existe no repo do
Hamad3bdulla; a comunidade está adicionando offsets até 13.60 mas pelo menos um valor por
perfil precisa ser verificado em console real.

**Adaptação do caso nuvem (o que é novo além do PHU):** o PHU ancora no processo do JOGO
real rodando. Na nuvem não há processo do jogo — o PSBox (PPSA99810) é o processo. Então a
adaptação é fazer o título alvo ser uma identidade real no console (ex.: a ideia de
**forwarder por jogo** — cada jogo um tile/título próprio com seu pacote de troféus) e o
register/unlock sair sob essa identidade. Essa parte é genuinamente nova.

---

## 6. Estado do repositório (tudo não-commitado, na working tree)

- `src/platform/ps5/trophy_backend_ps5.cpp`:
  - **Correção de regressão (verificada): `waitForUnjail()`** antes de `deployTrophyPack`.
    Sem ela, o deploy dos arquivos falhava silencioso com `errno=2` (o unjail assíncrono não
    tinha efetivado). Depois da correção: `errno=2` = 0, arquivos reescritos. **Isso é um
    fix bom, independente do resto.**
  - Log honesto no `flush()` (não afirma "commit to OS database").
  - `runPsboxNativeSelfTest()` — auto-teste que registra um conjunto de troféus no título do
    próprio PSBox e tenta o unlock normal (crasha no unlock — ver §3.5). Gatilho: autoplay
    `psboxself`.
- `tools/ps5/payload_patch_test/main.c`: **payload limpo de 19 → 12 patches** (removidos 7
  que eram chutes de agentes passados em funções off-path/inertes). Cabeçalho documenta o
  que saiu. #18 mudado de "stub" para "restaurar" a função de ingest.
- `ps5/stubs/libSceNpTrophy2.c`: adicionado `sceNpTrophy2UnlockTrophy` (import).
- `src/app/autoplay.cpp` / `autoplay.h`: gatilho `psboxself`.
- `tools/ps5/payload_patch_test/dump_shellcore.c`: payload read-only de dump de memória.

Observação: a feature inteira de troféus está **não-commitada** desde antes desta sessão.

---

## 7. Precauções de engenharia (obrigatórias)

Do AGENTS.md §8 e da experiência desta sessão:

- **Um título por vez.** Validar em UM alvo até provar persistência; nunca o catálogo todo
  (já houve poluição de NPWR8xxxx virtuais de um sync de catálogo completo proibido).
- **Nada de bips/vibração/toasts de debug** no caminho de troféu (§8.2); só a barra de
  progresso padrão.
- **`serviceLabel` nunca 0** — usar 1 ou -1 (senão kernel `0x80553914`).
- **Higiene do console:** matar o app antes de FTP; remover `autoplay.txt` ao final;
  relançar o app em foreground interativo. NUNCA deixar o console com app crashado.
- **Backups antes de escrever em DB de sistema:** `app.db`/`appinfo.db` em `/system_data/
  priv/mms/` são sensíveis — fazer `.bak` antes e escrever só as linhas do título alvo.
- **NÃO sobrescrever o `param.json` real do PSBox** no appmeta — escrever só o subdir
  `trophy2/` (escrita aditiva), senão arriscar a metadata do próprio app.
- **NÃO tocar nos outros payloads** em `/data/pldmgr/payloads/` (ftpsrv, kstuff, etc.).
- **Carregar o módulo de troféu ANTES do unjail** — depois do unjail o caminho
  `/<sandboxword>/common/lib/` quebra (erro `0x80020002`).
- **Chamar só símbolos resolvidos** — chamar um import não-exportado crasha o app na hora
  (foi o que derrubou nas tentativas de unlock). Verificar o símbolo/NID antes.
- **Rollback do daemon:** qualquer patch em memória no SceShellCore deve ter restauração dos
  bytes originais (e some no reboot, pois o jailbreak é tethered). Patches persistentes no
  daemon podem quebrar o fluxo de licença de jogos PS4 BC (ver PHU: rollback ao sair do jogo).
- **Offline/não-sync:** manter o nanoDNS/bloqueio de PSN; nunca sincronizar na conta
  principal. Se possível, marcar os troféus como locais.
- **Console de acesso:** 192.168.15.17 — FTP 2121 (anon), klog 3232, ps5vkctl 9111.
  Build/deploy: ver `tools/ps5/autotest.sh` e o README ("Particularidades do PS5").

---

## 8. Pontos em aberto / próximos passos (conceitual)

1. Descobrir o símbolo/caminho REAL do unlock normal no 13.60 (o `sceNpTrophy2UnlockTrophy`
   crashou) — e se, para um título provisionado, ele dispara a via (A).
2. Determinar se algum comando IPMI cliente alcança o handler da coleção `0xe232e0`, ou se
   só o serviço externo o emite (decide "chamar via SDK" vs "acionar a via (A) por payload").
3. Re-derivar, pelo método do PHU, os pontos de aceitação do daemon no 13.60 (usar os dumps
   da §4), se a via nativa exigir que o título seja aceito como provisionado.
4. Fechar a adaptação da identidade para o caso nuvem (forwarder por jogo vs título do PSBox).
5. Verificação sempre a nível de serviço (re-dump do cache `[svc+0x80]` após o unlock, checar
   a entrada do NPWR + flag `+0xe`) E visual (o dono olhando Perfil → Troféus na TV — não há
   como capturar a tela do sistema remotamente).

---

## 9. O que já é ganho sólido, sem depender do resto

- A correção do `errno=2` (deploy de arquivos voltou a funcionar) — commitável.
- O payload limpo (12 patches, documentado) — reduz "lixo" de tentativas passadas.
- Todo o mapeamento/diagnóstico acima.
- Alternativa pronta para construir: **tela de conquistas DENTRO do PSBox** (não é o perfil
  nativo do sistema, mas mostra nome/ícone/lista/grau/progresso de tudo que a pessoa
  conquistou) — 100% legítima, sem depender da via (A).
