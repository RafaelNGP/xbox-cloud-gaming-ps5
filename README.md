# xCloud-PS5

Cliente nativo de Xbox Cloud Gaming (xCloud) para PS5 com homebrew.

## Estado

| Parte | PC (`xcloud-cli`) | PS5 |
| --- | --- | --- |
| Interface na tela, DualSense nos menus | — | testado |
| Login (conta salva), lista de jogos, sessão, fila | testado | testado |
| WebRTC (SDP/ICE, DTLS-SRTP, canais de dados) | testado | testado |
| Vídeo 1080p60 H.264 (FFmpeg, 3,1 ms/quadro no PS5) | testado | testado: 3 min, 99,6% dos quadros exibidos |
| Áudio Opus → sceAudioOut | decodificação testada | decodificado e enviado à saída (falta confirmar de ouvido) |
| DualSense → controle Xbox no jogo | — | testado (autoplay aperta A no Balatro) |

## Teste automático no console

`tools/ps5/autotest.sh` compila, faz o deploy, abre o app pelo agente
`ps5vkctl` do PS5_Vulkan e espera o resultado. O app, ao achar
`autoplay.txt`, entra no jogo sozinho, aperta A aos 15 s e 20 s, registra
estatísticas por segundo e salva quadros em `frame.ppm`/`frame2.ppm`.

```bash
PS5_HOST=<ip> tools/ps5/autotest.sh BALATRO 60          # streaming
PS5_HOST=<ip> tools/ps5/autotest.sh BALATRO 30 dump     # + grava stream.aus (H.264 recebido)
XC_SAMPLE=amostra.h264 PS5_HOST=<ip> tools/ps5/autotest.sh BENCH 30   # só o decoder
build-host/xcloud-cli bench-decode build-ps5/autotest/stream.aus      # reproduz no PC
```

Resultados em `build-ps5/autotest/`.

## Particularidades do PS5 (sandbox do app)

- `getaddrinfo` não funciona (e o link do PS5_Vulkan o troca por um stub que
  sempre falha): `libc_compat.c` implementa `getaddrinfo`/`getnameinfo` com
  `sceNetResolver`, e o `link.sh` remove o redirecionamento.
- `ioctl(FIONBIO)`/`fcntl(O_NONBLOCK)` em sockets dão `EACCES`;
  `setsockopt(SO_NBIO)` funciona (`ioctl` é embrulhado via `ps5/compat/ps5_lfs.h`).
- O callback de log padrão do FFmpeg derruba o app: o log vai para o nosso logger.
- O depacketizador H.264 às vezes entrega unidades vazias, que o libavcodec
  entende como fim de stream: são descartadas.

## Build

```bash
git clone --recursive https://github.com/RafaelNGP/xbox-cloud-gaming-ps5.git

# PC (validação do protocolo)
cmake -S . -B build-host -G Ninja && ninja -C build-host
XCLOUD_DATA_DIR=build-host/data build-host/xcloud-cli login
XCLOUD_DATA_DIR=build-host/data build-host/xcloud-cli stream BALATRO 30 /tmp/out.h264

# PS5 (depende de ../../WoW-PS5/deps/PS5_Vulkan: SDK, linker, ps5-native-tool)
cmake -S . -B build-ps5 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/ps5.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
ninja -C build-ps5 xcloud_app
tools/ps5/link.sh build-ps5 && tools/ps5/package.sh build-ps5
PS5_HOST=<ip> tools/ps5/deploy.sh           # FTP para /data/homebrew/PPSA99810
PS5_HOST=<ip> tools/ps5/deploy.sh --log     # baixa xcloud.log
```

O FFmpeg (`deps/ffmpeg`, só os decoders H.264/Opus) é compilado pelo
`tools/build-ffmpeg.sh` na primeira configuração do CMake. O build roda em
`~/.cache/xcloud-ps5` porque o FFmpeg não aceita espaços no caminho; o `nasm`
também é compilado ali se não estiver instalado.

## No console

- Na primeira vez, aparece um código: abra https://www.microsoft.com/link no
  celular e digite o código. O `package.sh` copia o `build-host/data/account.json`
  para o pacote se ele existir, e aí o app pula essa etapa. Esse arquivo tem o
  seu token: não compartilhe o pacote.
- Lista de jogos: direcional move a seleção, X joga, OPTIONS sai da conta.
- No jogo: segure OPTIONS + TOUCHPAD por 1 s para sair. TOUCHPAD = View,
  OPTIONS = Menu.
- Log: `/data/homebrew/PPSA99810/xcloud.log`.

## Organização

- `src/auth`, `src/xcloud`: login e API gssv (sessões, SDP, ICE).
- `src/stream`: sessão WebRTC (libdatachannel) e formato do canal de input.
- `src/media`: decodificação (libavcodec) e saída de áudio.
- `src/display`, `src/input`: framebuffer do PS5 (sceVideoOut) e DualSense.
- `src/app`: `cli_main.cpp` (PC), `ps5_main.cpp` + `stream_player.cpp` (PS5).
- `deps`: mbedTLS (com DTLS-SRTP habilitado), libdatachannel e FFmpeg.

Protocolo baseado nos clientes open-source xbox-xcloud-player e Greenlight.
