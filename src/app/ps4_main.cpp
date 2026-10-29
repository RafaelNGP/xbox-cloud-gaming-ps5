// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The PS4's entry point: runs the app with PS4 backends and shell_ps4.cpp.
#include "app/app_core.h"
#include "util/log.h"

#include <fcntl.h>
#include <unistd.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    // Early diagnostic marker: verifies main() was entered before complex subsystems
    for (const char* path : {"/data/psbox/boot.log", "/data/homebrew/PSBOX00001/boot.log"}) {
        int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) {
            const char msg[] = "PSBox Cloud Gaming - PS4 main() entered successfully\n";
            (void)::write(fd, msg, sizeof(msg) - 1);
            ::close(fd);
            break;
        }
    }
    int rc = xc::app::runApp();
    if (rc != 0) {
        XC_LOGE("main: runApp returned %d", rc);
    }
    xc::log::setFile(nullptr);
    return rc;
}
