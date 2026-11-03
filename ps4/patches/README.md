# PS4 patches to the WebRTC submodules

CMake applies these when it configures a PS4 build (`cmake/deps/libdatachannel.cmake`).
The submodules stay on their upstream commits, and their checkouts show as modified
afterwards. That is expected.

- `libjuice.patch` (deps/libdatachannel/deps/libjuice):
  - `timestamp.c`: on PS4, time comes from `sceKernelGetProcessTime`.
  - `conn_poll.c`: the poll loop gets woken by a UDP socket, as on Windows, because PS4 has no
    pipe. When the system refuses the loopback bind, it falls back to waking every 50 ms.
- `libdatachannel.patch` (deps/libdatachannel):
  - `sctptransport`: sets `sconn_len`, as on FreeBSD, and moves the 64 KB receive buffer off
    the thread stack, which is small on PS4.
  - `dtlstransport`: a DTLS send that cannot complete returns instead of spinning.
  - `init` and `peerconnection`: extra debug log lines.

To change a patch, edit the submodule checkout, then run
`git -C <submodule> diff > ps4/patches/<name>.patch`.
