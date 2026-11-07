// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "trophy/trophy_db.h"
#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <sqlite3.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace xc::trophy {

namespace {

uint32_t fnv1a(const std::string& str) {
    uint32_t hash = 2166136261u;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

struct ColumnVal {
    int type = SQLITE_NULL;
    int64_t intVal = 0;
    double floatVal = 0.0;
    std::string strVal;
    std::vector<uint8_t> blobVal;
};

void backupIfMissing(const std::string& path) {
    std::string bak = path + ".bak";
    struct stat stBak{};
    if (::stat(bak.c_str(), &stBak) == 0 && stBak.st_size > 0) {
        return;
    }
    std::string data;
    if (platform::readFile(path, data) && !data.empty()) {
        platform::writeFileAtomic(bak, data);
        ::chmod(bak.c_str(), 0666);
        XC_LOGI("trophy db: backed up %s to %s (%zu bytes)", path.c_str(), bak.c_str(), data.size());
    }
}

std::string buildAppInfoJson(const std::string& originalJson, const TitleDbInfo& info) {
    auto j = json::parse(originalJson);
    json::Value out = (j && j->isObject()) ? std::move(*j) : json::Value::object();
    out.set("TITLE_ID", info.titleId);
    out.set("TITLE", info.titleName);
    out.set("CONTENT_ID", info.contentId);
    out.set("CONCEPT_ID", static_cast<int64_t>(info.conceptId));
    out.set("APP_TYPE", static_cast<int64_t>(1));
    out.set("ATTRIBUTE3", static_cast<int64_t>(1835012));
    out.set("DISPLAYLOCATION", static_cast<int64_t>(0));
    out.set("DEEPLINK_URI", "psgm:play?id=" + info.titleId);
    out.set("HUBAPP_URI", "pshome:gamehub?titleId=" + info.titleId);
    out.set("_metadata_path", info.metaDataPath);
    out.set("_org_path", "/user/app/" + info.titleId);
    out.set("_local_concept_id", info.localConceptId);

    static const char* kTitleLocales[] = {
        "TITLE_AR", "TITLE_CS", "TITLE_DA", "TITLE_DE", "TITLE_EL", "TITLE_EN",
        "TITLE_EN_GB", "TITLE_EN_US", "TITLE_ES", "TITLE_ES_419", "TITLE_FI",
        "TITLE_FR", "TITLE_FR_CA", "TITLE_HU", "TITLE_IT", "TITLE_JA",
        "TITLE_KO", "TITLE_NL", "TITLE_NO", "TITLE_PL", "TITLE_PT", "TITLE_PT_BR",
        "TITLE_RO", "TITLE_RU", "TITLE_SV", "TITLE_TH", "TITLE_TR", "TITLE_VI",
        "TITLE_ZH_CN", "TITLE_ZH_TW"
    };
    for (const char* k : kTitleLocales) {
        out.set(k, info.titleName);
    }
    return out.dump();
}

inline void updateCol(std::map<std::string, ColumnVal>& row, const std::string& col, const ColumnVal& val) {
    auto it = row.find(col);
    if (it != row.end()) {
        it->second = val;
    }
}


void setupDbPragmas(sqlite3* db) {
    if (!db) return;
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode = MEMORY; PRAGMA synchronous = OFF; PRAGMA temp_store = MEMORY;", nullptr, nullptr, nullptr);
}

bool executeInsertRow(sqlite3* db, const std::string& tableName, const std::map<std::string, ColumnVal>& row) {
    if (row.empty()) return false;
    std::string sql = "INSERT OR REPLACE INTO " + tableName + " (";
    std::string ph = " VALUES (";
    bool first = true;
    for (const auto& [col, _] : row) {
        if (!first) { sql += ", "; ph += ", "; }
        sql += col;
        ph += "?";
        first = false;
    }
    sql += ")" + ph + ");";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        XC_LOGW("trophy db: prepare insert failed for %s: %s", tableName.c_str(), sqlite3_errmsg(db));
        return false;
    }

    int idx = 1;
    for (const auto& [col, val] : row) {
        if (val.type == SQLITE_INTEGER) {
            sqlite3_bind_int64(stmt, idx, val.intVal);
        } else if (val.type == SQLITE_FLOAT) {
            sqlite3_bind_double(stmt, idx, val.floatVal);
        } else if (val.type == SQLITE_TEXT) {
            sqlite3_bind_text(stmt, idx, val.strVal.c_str(), -1, SQLITE_TRANSIENT);
        } else if (val.type == SQLITE_BLOB) {
            sqlite3_bind_blob(stmt, idx, val.blobVal.data(), static_cast<int>(val.blobVal.size()), SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, idx);
        }
        idx++;
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        XC_LOGW("trophy db: insert failed into %s: %s", tableName.c_str(), sqlite3_errmsg(db));
        return false;
    }
    return true;
}

std::map<std::string, ColumnVal> readCurrentRow(sqlite3_stmt* stmt) {
    std::map<std::string, ColumnVal> row;
    int cols = sqlite3_column_count(stmt);
    for (int i = 0; i < cols; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        if (!name) continue;
        int type = sqlite3_column_type(stmt, i);
        ColumnVal cv;
        cv.type = type;
        if (type == SQLITE_INTEGER) {
            cv.intVal = sqlite3_column_int64(stmt, i);
        } else if (type == SQLITE_FLOAT) {
            cv.floatVal = sqlite3_column_double(stmt, i);
        } else if (type == SQLITE_TEXT) {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            if (txt) cv.strVal = txt;
        } else if (type == SQLITE_BLOB) {
            const uint8_t* b = reinterpret_cast<const uint8_t*>(sqlite3_column_blob(stmt, i));
            int len = sqlite3_column_bytes(stmt, i);
            if (b && len > 0) cv.blobVal.assign(b, b + len);
        }
        row[name] = std::move(cv);
    }
    return row;
}

}  // namespace

TitleDbInfo makeTitleDbInfo(const std::string& titleId, const std::string& titleName,
                            const std::string& contentId) {
    TitleDbInfo info;
    info.titleId = titleId;
    info.titleName = titleName.empty() ? ("Game " + titleId) : titleName;

    if (titleId == "PPSA01922") {
        info.contentId = "UP1477-PPSA01922_00-FORTNITECORE0001";
        info.conceptId = 210118;
        info.localConceptId = "cid:scp:00000000000334c6";
    } else if (titleId == "PPSA03350") {
        info.contentId = "UP4433-PPSA03350_00-MINECRAFTPS50001";
        info.conceptId = 10001479;
        info.localConceptId = "cid:scp:000000000098a83e";
    } else if (titleId == "PPSA16155") {
        info.contentId = "UP1477-PPSA16155_00-SEAOFTHEIVES0001";
        info.conceptId = 210119;
        info.localConceptId = "cid:scp:00000000000334c7";
    } else {
        uint32_t h = fnv1a(titleId);
        info.conceptId = 200000 + (h % 700000);
        char hexBuf[32];
        snprintf(hexBuf, sizeof(hexBuf), "cid:scp:00000000%08x", h);
        info.localConceptId = hexBuf;
        if (!contentId.empty()) {
            info.contentId = contentId;
        } else {
            info.contentId = "UP0001-" + titleId + "_00-0000000000000000";
        }
    }

    info.metaDataPath = "/user/app/" + titleId + "/sce_sys";
    info.icon0Path = "/user/app/" + titleId + "/sce_sys/icon0.png";
    info.pic0Path = "/user/app/" + titleId + "/sce_sys/pic0.png";
    return info;
}

bool isRetailInstalledOnConsole(const std::string& sonyTitleId) {
    if (sonyTitleId.empty() || sonyTitleId == "PPSA99810" || sonyTitleId == "PPSA22520") return true;
    struct stat st{};
    if (::stat(("/user/app/" + sonyTitleId + "/app.pkg").c_str(), &st) == 0) return true;
    if (::stat(("/mnt/ext0/user/app/" + sonyTitleId + "/app.pkg").c_str(), &st) == 0) return true;
    if (::stat(("/mnt/ext1/user/app/" + sonyTitleId + "/app.pkg").c_str(), &st) == 0) return true;
    std::string paramJson;
    if (platform::readFile("/system_data/priv/appmeta/" + sonyTitleId + "/param.json", paramJson) && !paramJson.empty()) {
        if (paramJson.find("2026-10-31 00:00:00") == std::string::npos) {
            return true;
        }
    }
    return false;
}

bool registerTitleInDatabase(const TitleDbInfo& info,
                             const std::string& appDbPath,
                             const std::string& appinfoDbPath) {
    if (info.titleId.empty() || info.titleId == "PPSA99810") {
        return false;
    }
    if (appDbPath.empty() && isRetailInstalledOnConsole(info.titleId)) {
        XC_LOGI("trophy db: preserving retail installed title %s in app.db", info.titleId.c_str());
        return true;
    }

    std::string appDb = appDbPath.empty() ? "/system_data/priv/mms/app.db" : appDbPath;
    std::string appinfoDb = appinfoDbPath.empty() ? "/system_data/priv/mms/appinfo.db" : appinfoDbPath;

    struct stat stApp{};
    if (::stat(appDb.c_str(), &stApp) != 0) {
        XC_LOGI("trophy db: %s not accessible (stat failed); skipping DB registration", appDb.c_str());
        return false;
    }

    backupIfMissing(appDb);
    backupIfMissing(appinfoDb);

    // 1. Process app.db
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(appDb.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        XC_LOGW("trophy db: failed to open %s: %s", appDb.c_str(), db ? sqlite3_errmsg(db) : "unknown");
        if (db) sqlite3_close(db);
        return false;
    }
    setupDbPragmas(db);
    if (sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        XC_LOGW("trophy db: failed to begin transaction on %s: %s", appDb.c_str(), sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    // 1a. tbl_contentinfo
    bool contentExists = false;
    int existingDispLocation = 0;
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT dispLocation FROM tbl_contentinfo WHERE titleId = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                contentExists = true;
                existingDispLocation = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }
    if (contentExists && existingDispLocation != 0) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        XC_LOGI("trophy db: preserving retail installed title %s (dispLocation=%d) in app.db",
                info.titleId.c_str(), existingDispLocation);
        return true;
    }

    if (!contentExists) {
        // Find model row
        std::map<std::string, ColumnVal> modelRow;
        sqlite3_stmt* stmt = nullptr;
        const char* q = "SELECT * FROM tbl_contentinfo WHERE appType = 1 ORDER BY (CASE WHEN titleId = 'PPSA22520' THEN 0 WHEN titleId = 'PPSA01922' THEN 1 ELSE 2 END) LIMIT 1;";
        if (sqlite3_prepare_v2(db, q, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                modelRow = readCurrentRow(stmt);
            }
            sqlite3_finalize(stmt);
        }
        if (modelRow.empty()) {
            // Fallback to any row
            if (sqlite3_prepare_v2(db, "SELECT * FROM tbl_contentinfo LIMIT 1;", -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    modelRow = readCurrentRow(stmt);
                }
                sqlite3_finalize(stmt);
            }
        }

        if (modelRow.empty()) {
            modelRow["titleId"] = {SQLITE_TEXT, 0, 0.0, info.titleId, {}};
            modelRow["titleName"] = {SQLITE_TEXT, 0, 0.0, info.titleName, {}};
            modelRow["contentId"] = {SQLITE_TEXT, 0, 0.0, info.contentId, {}};
            modelRow["conceptId"] = {SQLITE_INTEGER, static_cast<int64_t>(info.conceptId), 0.0, "", {}};
            modelRow["localConceptId"] = {SQLITE_TEXT, 0, 0.0, info.localConceptId, {}};
            modelRow["metaDataPath"] = {SQLITE_TEXT, 0, 0.0, info.metaDataPath, {}};
            modelRow["dispLocation"] = {SQLITE_INTEGER, 0, 0.0, "", {}};
            modelRow["appType"] = {SQLITE_INTEGER, 1, 0.0, "", {}};
            modelRow["uiCategory"] = {SQLITE_TEXT, 0, 0.0, "game", {}};
            modelRow["icon0Info"] = {SQLITE_TEXT, 0, 0.0, info.icon0Path, {}};
            modelRow["pic0Info"] = {SQLITE_TEXT, 0, 0.0, info.pic0Path, {}};
            modelRow["snd0Info"] = {SQLITE_TEXT, 0, 0.0, "/user/appmeta/" + info.titleId + "/snd0.at9", {}};
            modelRow["pprDeeplinkUri"] = {SQLITE_TEXT, 0, 0.0, "psgm:play?id=" + info.titleId, {}};
            modelRow["pprHubAppUri"] = {SQLITE_TEXT, 0, 0.0, "pshome:gamehub?titleId=" + info.titleId, {}};
            modelRow["AppInfoJson"] = {SQLITE_TEXT, 0, 0.0, buildAppInfoJson("{}", info), {}};
            executeInsertRow(db, "tbl_contentinfo", modelRow);
            XC_LOGI("trophy db: inserted synthetic %s into tbl_contentinfo", info.titleId.c_str());
        } else {
            updateCol(modelRow, "titleId", {SQLITE_TEXT, 0, 0.0, info.titleId, {}});
            updateCol(modelRow, "contentId", {SQLITE_TEXT, 0, 0.0, info.contentId, {}});
            updateCol(modelRow, "titleName", {SQLITE_TEXT, 0, 0.0, info.titleName, {}});
            updateCol(modelRow, "metaDataPath", {SQLITE_TEXT, 0, 0.0, info.metaDataPath, {}});
            updateCol(modelRow, "conceptId", {SQLITE_INTEGER, static_cast<int64_t>(info.conceptId), 0.0, "", {}});
            updateCol(modelRow, "localConceptId", {SQLITE_TEXT, 0, 0.0, info.localConceptId, {}});
            updateCol(modelRow, "pprDeeplinkUri", {SQLITE_TEXT, 0, 0.0, "psgm:play?id=" + info.titleId, {}});
            updateCol(modelRow, "pprHubAppUri", {SQLITE_TEXT, 0, 0.0, "pshome:gamehub?titleId=" + info.titleId, {}});
            updateCol(modelRow, "snd0Info", {SQLITE_TEXT, 0, 0.0, "/user/appmeta/" + info.titleId + "/snd0.at9", {}});
            updateCol(modelRow, "icon0Info", {SQLITE_TEXT, 0, 0.0, info.icon0Path, {}});
            updateCol(modelRow, "pic0Info", {SQLITE_TEXT, 0, 0.0, info.pic0Path, {}});
            updateCol(modelRow, "appType", {SQLITE_INTEGER, 1, 0.0, "", {}});
            updateCol(modelRow, "dispLocation", {SQLITE_INTEGER, 0, 0.0, "", {}});
            updateCol(modelRow, "uiCategory", {SQLITE_TEXT, 0, 0.0, "game", {}});
            if (modelRow.count("AppInfoJson")) {
                modelRow["AppInfoJson"] = {SQLITE_TEXT, 0, 0.0, buildAppInfoJson(modelRow["AppInfoJson"].strVal, info), {}};
            }
            executeInsertRow(db, "tbl_contentinfo", modelRow);
            XC_LOGI("trophy db: inserted %s into tbl_contentinfo", info.titleId.c_str());
        }
    } else {
        // Update existing row
        std::string existingJson;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT AppInfoJson FROM tbl_contentinfo WHERE titleId = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (t) existingJson = t;
            }
            sqlite3_finalize(stmt);
        }
        std::string updatedJson = buildAppInfoJson(existingJson, info);
        const char* uSql = "UPDATE tbl_contentinfo SET appType = 1, dispLocation = 0, titleName = ?, contentId = ?, conceptId = ?, AppInfoJson = ? WHERE titleId = ?;";
        if (sqlite3_prepare_v2(db, uSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, info.titleName.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, info.contentId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(info.conceptId));
            sqlite3_bind_text(stmt, 4, updatedJson.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            XC_LOGI("trophy db: updated %s in tbl_contentinfo", info.titleId.c_str());
        }
    }

    // 1b. tbl_iconinfo_<uid>
    std::vector<std::string> iconTables;
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_iconinfo_%';", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (n) iconTables.push_back(n);
            }
            sqlite3_finalize(stmt);
        }
    }

    for (const auto& tbl : iconTables) {
        bool exists = false;
        sqlite3_stmt* stmt = nullptr;
        std::string q = "SELECT 1 FROM " + tbl + " WHERE titleId = ?;";
        if (sqlite3_prepare_v2(db, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) exists = true;
            sqlite3_finalize(stmt);
        }

        if (!exists) {
            std::map<std::string, ColumnVal> modelRow;
            std::string mq = "SELECT * FROM " + tbl + " ORDER BY (CASE WHEN titleId = 'PPSA22520' THEN 0 WHEN titleId = 'PPSA01922' THEN 1 ELSE 2 END) LIMIT 1;";
            if (sqlite3_prepare_v2(db, mq.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    modelRow = readCurrentRow(stmt);
                }
                sqlite3_finalize(stmt);
            }
            if (!modelRow.empty()) {
                updateCol(modelRow, "titleId", {SQLITE_TEXT, 0, 0.0, info.titleId, {}});
                updateCol(modelRow, "titleName", {SQLITE_TEXT, 0, 0.0, info.titleName, {}});
                updateCol(modelRow, "contentId", {SQLITE_TEXT, 0, 0.0, info.contentId, {}});
                updateCol(modelRow, "conceptId", {SQLITE_INTEGER, static_cast<int64_t>(info.conceptId), 0.0, "", {}});
                updateCol(modelRow, "localConceptId", {SQLITE_TEXT, 0, 0.0, info.localConceptId, {}});
                updateCol(modelRow, "visible", {SQLITE_INTEGER, 0, 0.0, "", {}});
                updateCol(modelRow, "installStatus", {SQLITE_INTEGER, 2, 0.0, "", {}});
                updateCol(modelRow, "appDrmType", {SQLITE_INTEGER, 5, 0.0, "", {}});
                updateCol(modelRow, "deeplinkUri", {SQLITE_TEXT, 0, 0.0, "psgm:play?id=" + info.titleId, {}});
                updateCol(modelRow, "hubAppUri", {SQLITE_TEXT, 0, 0.0, "pshome:gamehub?titleId=" + info.titleId, {}});
                executeInsertRow(db, tbl, modelRow);
                XC_LOGI("trophy db: inserted %s into %s", info.titleId.c_str(), tbl.c_str());
            }
        } else {
            std::string uq = "UPDATE " + tbl + " SET conceptId = ?, localConceptId = ?, contentId = ?, titleName = ?, visible = 0, installStatus = 2, appDrmType = 5 WHERE titleId = ?;";
            if (sqlite3_prepare_v2(db, uq.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(info.conceptId));
                sqlite3_bind_text(stmt, 2, info.localConceptId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, info.contentId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, info.titleName.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    // 1c. tbl_concepticoninfo_<uid>
    std::vector<std::string> conceptTables;
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_concepticoninfo_%';", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (n) conceptTables.push_back(n);
            }
            sqlite3_finalize(stmt);
        }
    }

    for (const auto& tbl : conceptTables) {
        bool exists = false;
        sqlite3_stmt* stmt = nullptr;
        std::string q = "SELECT 1 FROM " + tbl + " WHERE primaryTitleId = ?;";
        if (sqlite3_prepare_v2(db, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) exists = true;
            sqlite3_finalize(stmt);
        }

        if (!exists) {
            std::map<std::string, ColumnVal> modelRow;
            std::string mq = "SELECT * FROM " + tbl + " ORDER BY (CASE WHEN primaryTitleId = 'PPSA22520' THEN 0 WHEN primaryTitleId = 'PPSA01922' THEN 1 ELSE 2 END) LIMIT 1;";
            if (sqlite3_prepare_v2(db, mq.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    modelRow = readCurrentRow(stmt);
                }
                sqlite3_finalize(stmt);
            }
            if (!modelRow.empty()) {
                updateCol(modelRow, "localConceptId", {SQLITE_TEXT, 0, 0.0, info.localConceptId, {}});
                updateCol(modelRow, "conceptId", {SQLITE_INTEGER, static_cast<int64_t>(info.conceptId), 0.0, "", {}});
                updateCol(modelRow, "conceptName", {SQLITE_TEXT, 0, 0.0, info.titleName, {}});
                updateCol(modelRow, "primaryTitleId", {SQLITE_TEXT, 0, 0.0, info.titleId, {}});
                updateCol(modelRow, "primaryTitleName", {SQLITE_TEXT, 0, 0.0, info.titleName, {}});
                updateCol(modelRow, "deeplinkUri", {SQLITE_TEXT, 0, 0.0, "psgm:play?id=" + info.titleId, {}});
                updateCol(modelRow, "hubAppUri", {SQLITE_TEXT, 0, 0.0, "pshome:gamehub?titleId=" + info.titleId, {}});
                updateCol(modelRow, "metaDataPath", {SQLITE_TEXT, 0, 0.0, info.metaDataPath, {}});
                updateCol(modelRow, "icon0Info", {SQLITE_TEXT, 0, 0.0, info.icon0Path, {}});
                updateCol(modelRow, "pic0Info", {SQLITE_TEXT, 0, 0.0, info.pic0Path, {}});
                updateCol(modelRow, "snd0Info", {SQLITE_TEXT, 0, 0.0, "/user/app/" + info.titleId + "/sce_sys/snd0.at9", {}});
                executeInsertRow(db, tbl, modelRow);
                XC_LOGI("trophy db: inserted %s into %s", info.titleId.c_str(), tbl.c_str());
            }
        } else {
            std::string uq = "UPDATE " + tbl + " SET conceptId = ?, conceptName = ?, primaryTitleName = ? WHERE primaryTitleId = ?;";
            if (sqlite3_prepare_v2(db, uq.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(info.conceptId));
                sqlite3_bind_text(stmt, 2, info.titleName.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, info.titleName.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
    sqlite3_close(db);

    // 2. Process appinfo.db
    struct stat stInfo{};
    if (::stat(appinfoDb.c_str(), &stInfo) == 0) {
        sqlite3* infoDb = nullptr;
        if (sqlite3_open_v2(appinfoDb.c_str(), &infoDb, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK) {
            setupDbPragmas(infoDb);
            if (sqlite3_exec(infoDb, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
                XC_LOGW("trophy db: failed to begin transaction on %s: %s", appinfoDb.c_str(), sqlite3_errmsg(infoDb));
                sqlite3_close(infoDb);
                return true;
            }

            int count = 0;
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(infoDb, "SELECT COUNT(*) FROM tbl_appinfo WHERE titleId = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
            }

            if (count == 0) {
                std::string modelTitle;
                // Query priority: PPSA22520, PPSA01922, any title with APP_TYPE=1, or any title at all
                const char* modelQueries[] = {
                    "SELECT titleId FROM tbl_appinfo WHERE titleId = 'PPSA22520' LIMIT 1;",
                    "SELECT titleId FROM tbl_appinfo WHERE titleId = 'PPSA01922' LIMIT 1;",
                    "SELECT titleId FROM tbl_appinfo WHERE key = 'APP_TYPE' AND val = '1' LIMIT 1;",
                    "SELECT titleId FROM tbl_appinfo LIMIT 1;"
                };
                for (const char* mq : modelQueries) {
                    sqlite3_stmt* s = nullptr;
                    if (sqlite3_prepare_v2(infoDb, mq, -1, &s, nullptr) == SQLITE_OK) {
                        if (sqlite3_step(s) == SQLITE_ROW) {
                            const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
                            if (t) modelTitle = t;
                        }
                        sqlite3_finalize(s);
                    }
                    if (!modelTitle.empty()) break;
                }

                struct AppInfoEntry {
                    int64_t metaDataId = 0;
                    std::string key;
                    std::string val;
                };
                std::vector<AppInfoEntry> entries;

                if (!modelTitle.empty()) {
                    if (sqlite3_prepare_v2(infoDb, "SELECT metaDataId, key, val FROM tbl_appinfo WHERE titleId = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
                        sqlite3_bind_text(stmt, 1, modelTitle.c_str(), -1, SQLITE_TRANSIENT);
                        while (sqlite3_step(stmt) == SQLITE_ROW) {
                            AppInfoEntry e;
                            e.metaDataId = sqlite3_column_int64(stmt, 0);
                            const char* k = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                            const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                            if (k) e.key = k;
                            if (v) e.val = v;
                            entries.push_back(std::move(e));
                        }
                        sqlite3_finalize(stmt);
                    }
                }

                if (!entries.empty()) {
                    for (const auto& e : entries) {
                        std::string newVal = e.val;
                        if (e.key == "TITLE_ID") {
                            newVal = info.titleId;
                        } else if (e.key == "CONTENT_ID") {
                            newVal = info.contentId;
                        } else if (e.key == "CONCEPT_ID") {
                            newVal = std::to_string(info.conceptId);
                        } else if (e.key == "_local_concept_id") {
                            newVal = info.localConceptId;
                        } else if (e.key == "APP_TYPE") {
                            newVal = "1";
                        } else if (e.key == "ATTRIBUTE3") {
                            newVal = "1835012";
                        } else if (e.key == "DISPLAYLOCATION") {
                            newVal = "0";
                        } else if (e.key == "DEEPLINK_URI") {
                            newVal = "psgm:play?id=" + info.titleId;
                        } else if (e.key == "HUBAPP_URI") {
                            newVal = "pshome:gamehub?titleId=" + info.titleId;
                        } else if (e.key == "_metadata_path") {
                            newVal = info.metaDataPath;
                        } else if (e.key == "_org_path") {
                            newVal = "/user/app/" + info.titleId;
                        } else if (e.key == "icon0Info") {
                            newVal = info.icon0Path;
                        } else if (e.key == "pic0Info") {
                            newVal = info.pic0Path;
                        } else if (e.key == "TITLE" || (e.key.rfind("TITLE_", 0) == 0 && e.key != "TITLE_ID")) {
                            newVal = info.titleName;
                        } else if (!modelTitle.empty()) {
                            size_t pos = 0;
                            while ((pos = newVal.find(modelTitle, pos)) != std::string::npos) {
                                newVal.replace(pos, modelTitle.length(), info.titleId);
                                pos += info.titleId.length();
                            }
                        }

                        const char* iSql = "INSERT OR REPLACE INTO tbl_appinfo (titleId, metaDataId, key, val) VALUES (?, ?, ?, ?);";
                        if (sqlite3_prepare_v2(infoDb, iSql, -1, &stmt, nullptr) == SQLITE_OK) {
                            sqlite3_bind_text(stmt, 1, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int64(stmt, 2, e.metaDataId);
                            sqlite3_bind_text(stmt, 3, e.key.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 4, newVal.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_step(stmt);
                            sqlite3_finalize(stmt);
                        }
                    }
                    XC_LOGI("trophy db: inserted %zu entries for %s into tbl_appinfo", entries.size(), info.titleId.c_str());
                } else {
                    auto insertEntry = [&](const std::string& key, const std::string& val) {
                        sqlite3_stmt* s = nullptr;
                        const char* iSql = "INSERT OR REPLACE INTO tbl_appinfo (titleId, metaDataId, key, val) VALUES (?, 0, ?, ?);";
                        if (sqlite3_prepare_v2(infoDb, iSql, -1, &s, nullptr) == SQLITE_OK) {
                            sqlite3_bind_text(s, 1, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(s, 2, key.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(s, 3, val.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_step(s);
                            sqlite3_finalize(s);
                        }
                    };
                    insertEntry("TITLE_ID", info.titleId);
                    insertEntry("TITLE", info.titleName);
                    insertEntry("CONTENT_ID", info.contentId);
                    insertEntry("CONCEPT_ID", std::to_string(info.conceptId));
                    insertEntry("_local_concept_id", info.localConceptId);
                    insertEntry("APP_TYPE", "1");
                    insertEntry("ATTRIBUTE3", "1835012");
                    insertEntry("DISPLAYLOCATION", "0");
                    insertEntry("DEEPLINK_URI", "psgm:play?id=" + info.titleId);
                    insertEntry("HUBAPP_URI", "pshome:gamehub?titleId=" + info.titleId);
                    insertEntry("_metadata_path", info.metaDataPath);
                    insertEntry("_org_path", "/user/app/" + info.titleId);
                    insertEntry("icon0Info", info.icon0Path);
                    insertEntry("pic0Info", info.pic0Path);
                    static const char* kLocales[] = {
                        "TITLE_AR", "TITLE_CS", "TITLE_DA", "TITLE_DE", "TITLE_EL", "TITLE_EN",
                        "TITLE_EN_GB", "TITLE_EN_US", "TITLE_ES", "TITLE_ES_419", "TITLE_FI",
                        "TITLE_FR", "TITLE_FR_CA", "TITLE_HU", "TITLE_IT", "TITLE_JA",
                        "TITLE_KO", "TITLE_NL", "TITLE_NO", "TITLE_PL", "TITLE_PT", "TITLE_PT_BR",
                        "TITLE_RO", "TITLE_RU", "TITLE_SV", "TITLE_TH", "TITLE_TR", "TITLE_VI",
                        "TITLE_ZH_CN", "TITLE_ZH_TW"
                    };
                    for (const char* loc : kLocales) {
                        insertEntry(loc, info.titleName);
                    }
                    XC_LOGI("trophy db: synthesized default entries for %s into tbl_appinfo", info.titleId.c_str());
                }
            } else {
                auto updateVal = [&](const char* key, const std::string& val) {
                    sqlite3_stmt* s = nullptr;
                    if (sqlite3_prepare_v2(infoDb, "UPDATE tbl_appinfo SET val = ? WHERE titleId = ? AND key = ?;", -1, &s, nullptr) == SQLITE_OK) {
                        sqlite3_bind_text(s, 1, val.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(s, 2, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(s, 3, key, -1, SQLITE_TRANSIENT);
                        sqlite3_step(s);
                        sqlite3_finalize(s);
                    }
                };
                updateVal("CONTENT_ID", info.contentId);
                updateVal("CONCEPT_ID", std::to_string(info.conceptId));
                updateVal("TITLE_ID", info.titleId);
                updateVal("APP_TYPE", "1");
                updateVal("ATTRIBUTE3", "1835012");
                updateVal("_local_concept_id", info.localConceptId);
                updateVal("DISPLAYLOCATION", "0");
                updateVal("TITLE", info.titleName);
                updateVal("_metadata_path", info.metaDataPath);
                updateVal("_org_path", "/user/app/" + info.titleId);
                updateVal("icon0Info", info.icon0Path);
                updateVal("pic0Info", info.pic0Path);

                sqlite3_stmt* sLoc = nullptr;
                if (sqlite3_prepare_v2(infoDb, "UPDATE tbl_appinfo SET val = ? WHERE titleId = ? AND key LIKE 'TITLE_%' AND key != 'TITLE_ID';", -1, &sLoc, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(sLoc, 1, info.titleName.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(sLoc, 2, info.titleId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(sLoc);
                    sqlite3_finalize(sLoc);
                }
            }

            sqlite3_exec(infoDb, "COMMIT;", nullptr, nullptr, nullptr);
            sqlite3_wal_checkpoint_v2(infoDb, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
            sqlite3_close(infoDb);
        }
    }

    XC_LOGI("trophy db: registration completed successfully for %s (%s)", info.titleName.c_str(), info.titleId.c_str());
    return true;
}

bool removeTitleFromDatabase(const std::string& titleId,
                             const std::string& appDbPath,
                             const std::string& appinfoDbPath) {
    if (titleId.empty() || titleId == "PPSA99810" || titleId == "PPSA22520") {
        return false;
    }
    if (appDbPath.empty() && isRetailInstalledOnConsole(titleId)) {
        XC_LOGI("trophy db: preserving retail installed title %s during remove", titleId.c_str());
        return false;
    }

    std::string appDb = appDbPath.empty() ? "/system_data/priv/mms/app.db" : appDbPath;
    std::string appinfoDb = appinfoDbPath.empty() ? "/system_data/priv/mms/appinfo.db" : appinfoDbPath;

    struct stat stApp{};
    if (::stat(appDb.c_str(), &stApp) != 0) {
        return false;
    }

    backupIfMissing(appDb);
    backupIfMissing(appinfoDb);

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(appDb.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK) {
        setupDbPragmas(db);
        if (sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            XC_LOGW("trophy db: failed to begin transaction on %s: %s", appDb.c_str(), sqlite3_errmsg(db));
            sqlite3_close(db);
            return false;
        }

        // Never delete a retail installed game (dispLocation != 0)
        sqlite3_stmt* stmt = nullptr;
        int existingDispLocation = 0;
        if (sqlite3_prepare_v2(db, "SELECT dispLocation FROM tbl_contentinfo WHERE titleId = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, titleId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                existingDispLocation = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        if (existingDispLocation != 0) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            sqlite3_close(db);
            XC_LOGI("trophy db: refusing to remove retail installed title %s (dispLocation=%d)",
                    titleId.c_str(), existingDispLocation);
            return true;
        }

        // tbl_contentinfo
        if (sqlite3_prepare_v2(db, "DELETE FROM tbl_contentinfo WHERE titleId = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, titleId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        // tbl_iconinfo_%
        std::vector<std::string> iconTables;
        if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_iconinfo_%';", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (n) iconTables.push_back(n);
            }
            sqlite3_finalize(stmt);
        }
        for (const auto& tbl : iconTables) {
            std::string q = "DELETE FROM " + tbl + " WHERE titleId = ?;";
            if (sqlite3_prepare_v2(db, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, titleId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }

        // tbl_concepticoninfo_%
        std::vector<std::string> conceptTables;
        if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_concepticoninfo_%';", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (n) conceptTables.push_back(n);
            }
            sqlite3_finalize(stmt);
        }
        for (const auto& tbl : conceptTables) {
            std::string q = "DELETE FROM " + tbl + " WHERE primaryTitleId = ?;";
            if (sqlite3_prepare_v2(db, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, titleId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }

        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
        sqlite3_close(db);
        XC_LOGI("trophy db: removed %s from app.db", titleId.c_str());
    }

    struct stat stInfo{};
    if (::stat(appinfoDb.c_str(), &stInfo) == 0) {
        sqlite3* infoDb = nullptr;
        if (sqlite3_open_v2(appinfoDb.c_str(), &infoDb, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK) {
            setupDbPragmas(infoDb);
            if (sqlite3_exec(infoDb, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
                XC_LOGW("trophy db: failed to begin transaction on %s: %s", appinfoDb.c_str(), sqlite3_errmsg(infoDb));
                sqlite3_close(infoDb);
                return true;
            }
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(infoDb, "DELETE FROM tbl_appinfo WHERE titleId = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, titleId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            sqlite3_exec(infoDb, "COMMIT;", nullptr, nullptr, nullptr);
            sqlite3_wal_checkpoint_v2(infoDb, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
            sqlite3_close(infoDb);
            XC_LOGI("trophy db: removed %s from appinfo.db", titleId.c_str());
        }
    }

    return true;
}

}  // namespace xc::trophy
