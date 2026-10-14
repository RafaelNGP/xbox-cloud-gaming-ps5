# Writes a C++ file holding IN's bytes (plus a terminating NUL, as Mbed TLS's
# PEM parser wants) as NAME[] and NAME_size: cmake -DIN=... -DOUT=... -DNAME=... -P
file(READ "${IN}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
file(WRITE "${OUT}" "// Generated from ${IN} by cmake/embed_file.cmake: do not edit.\n#include <cstddef>\n\nextern const unsigned char ${NAME}[] = {\n${bytes}0x00};\nextern const std::size_t ${NAME}_size = sizeof(${NAME});\n")
