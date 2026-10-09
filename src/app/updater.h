// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Updates the app in place from a release (update_check.h).
//
// The release's PPSA99810.zip must carry a signature (PPSA99810.zip.sig,
// ECDSA P-256 over SHA-256) by the project's own key, which tools/release.sh
// makes and only the maintainer holds: kReleaseKey here is its public half.
// Nothing is changed without it, nor for a version that isn't newer.
//
// The files are unpacked into <dir>/update.new, then each one in place is
// moved to <dir>/update.old and the new one put there (eboot.bin last); the
// running app keeps its old eboot.bin until it restarts. <dir>/update.journal
// is there while files move: a start that finds it puts the old ones back.
// The user's files (sign-in, settings, caches, log) are never touched.
#pragma once

#include "app/update_check.h"

#include <functional>
#include <string>
#include <vector>

namespace xc::app {

// The project's release signing key (public).
extern const char* const kReleaseKey;

enum class UpdateStep { Downloading, Verifying, Installing };
// `fraction` 0..1 within the step (downloading), -1 when unknown.
using UpdateProgress = std::function<void(UpdateStep step, double fraction)>;

// Downloads `release`, checks it and puts it in place in `dir` (the app's
// folder). `currentVersion` as XC_APP_VERSION. False with `err` (in English,
// for the log) when nothing was changed.
bool installRelease(const Release& release, const std::string& dir, const std::string& currentVersion,
                    const UpdateProgress& progress, std::string& err);

// At start: puts back the old files of an update cut short (true when it
// did), and removes what updates left behind.
bool recoverUpdate(const std::string& dir);

// --- The parts, for the tests ---------------------------------------------------

// True when `sigDer` is a valid signature of `data` by `publicKeyPem`.
bool verifySignature(const std::string& data, const std::string& sigDer, const char* publicKeyPem, std::string& err);

struct ZipEntry {
    std::string path;  // inside the archive, without `prefix`
    std::string data;
};
// The files of a zip archive (stored or deflated) under `prefix` ("PPSA99810/").
// False on a damaged archive, an entry outside `prefix` or an unsafe path.
bool unzip(const std::string& zip, const std::string& prefix, std::vector<ZipEntry>& out, std::string& err);

// A path an update may write: relative, no "..", and not one of the user's
// files or the updater's own.
bool updatablePath(const std::string& path);

}  // namespace xc::app
