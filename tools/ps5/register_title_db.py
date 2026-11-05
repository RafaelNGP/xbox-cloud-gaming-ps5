#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
"""
Registers a game title (e.g. Fortnite PPSA01922) into /system_data/priv/mms/app.db
and appinfo.db so the PS5 OS Trophy application enumerates and displays it.
Modeled on official PS5 titles in the user database (Animal Well PPSA22520).
"""

import io
import json
import os
import sqlite3
import sys
from ftplib import FTP

DEFAULT_HOST = os.environ.get("PS5_HOST", "192.168.15.17")
DEFAULT_PORT = int(os.environ.get("FTP_PORT", "2121"))


def get_ftp(host=DEFAULT_HOST, port=DEFAULT_PORT):
    ftp = FTP()
    ftp.connect(host, port, timeout=15)
    ftp.login()
    return ftp


def backup_and_download(ftp, remote_path, local_path):
    print(f"Downloading {remote_path} -> {local_path}...")
    bio = io.BytesIO()
    ftp.retrbinary(f"RETR {remote_path}", bio.write)
    data = bio.getvalue()
    with open(local_path, "wb") as f:
        f.write(data)
    # Also backup on PS5 if not already backed up
    bak_path = remote_path + ".bak"
    try:
        ftp.sendcmd(f"RNFR {remote_path}")
        # do not rename, upload copy as .bak
    except Exception:
        pass
    try:
        ftp.storbinary(f"STOR {bak_path}", io.BytesIO(data))
        print(f"Backed up {remote_path} to {bak_path} on console.")
    except Exception as e:
        print(f"Notice: backup to {bak_path} failed: {e}")
    return local_path


def upload_file(ftp, local_path, remote_path):
    print(f"Uploading {local_path} -> {remote_path}...")
    with open(local_path, "rb") as f:
        ftp.storbinary(f"STOR {remote_path}", f)
    print(f"Successfully uploaded {remote_path}")


def register_title_app_db(local_db_path, model_title_id="PPSA22520",
                          target_title_id="PPSA01922",
                          target_title_name="Fortnite",
                          target_content_id="UP1477-PPSA01922_00-FORTNITECORE0001",
                          target_concept_id=210118,
                          target_local_concept_id="cid:scp:00000000000334c6"):
    conn = sqlite3.connect(local_db_path)
    conn.row_factory = sqlite3.Row
    c = conn.cursor()

    # 1. tbl_contentinfo
    c.execute("SELECT * FROM tbl_contentinfo WHERE titleId = ?", (target_title_id,))
    if c.fetchone() is None:
        c.execute("SELECT * FROM tbl_contentinfo WHERE titleId = ?", (model_title_id,))
        model_row = dict(c.fetchone())
        new_row = dict(model_row)
        new_row["titleId"] = target_title_id
        new_row["contentId"] = target_content_id
        new_row["titleName"] = target_title_name
        new_row["metaDataPath"] = f"/system_data/priv/appmeta/{target_title_id}"
        new_row["conceptId"] = target_concept_id
        new_row["localConceptId"] = target_local_concept_id
        new_row["pprDeeplinkUri"] = f"psgm:play?id={target_title_id}"
        new_row["pprHubAppUri"] = f"pshome:gamehub?titleId={target_title_id}"
        new_row["snd0Info"] = f"/user/appmeta/{target_title_id}/snd0.at9"
        new_row["icon0Info"] = f"/user/app/{target_title_id}/sce_sys/icon0.png"
        new_row["pic0Info"] = f"/user/app/{target_title_id}/sce_sys/pic0.png"
        new_row["appType"] = 1  # 1 = Game
        new_row["dispLocation"] = 0  # Keep hidden from Game Library

        if new_row.get("AppInfoJson"):
            try:
                info = json.loads(new_row["AppInfoJson"])
                info["TITLE_ID"] = target_title_id
                info["TITLE"] = target_title_name
                info["CONTENT_ID"] = target_content_id
                info["CONCEPT_ID"] = target_concept_id
                info["APP_TYPE"] = 1
                info["ATTRIBUTE3"] = 1835012
                info["DISPLAYLOCATION"] = 0
                info["DEEPLINK_URI"] = f"psgm:play?id={target_title_id}"
                info["HUBAPP_URI"] = f"pshome:gamehub?titleId={target_title_id}"
                info["_metadata_path"] = f"/system_data/priv/appmeta/{target_title_id}"
                info["_org_path"] = f"/user/app/{target_title_id}"
                info["_local_concept_id"] = target_local_concept_id
                for k in list(info.keys()):
                    if k.startswith("TITLE_") and k != "TITLE_ID":
                        info[k] = target_title_name
                new_row["AppInfoJson"] = json.dumps(info)
            except Exception as e:
                print(f"Warning: AppInfoJson parse failed: {e}")

        cols = list(new_row.keys())
        placeholders = ", ".join(["?"] * len(cols))
        c.execute(f"INSERT INTO tbl_contentinfo ({', '.join(cols)}) VALUES ({placeholders})",
                  [new_row[col] for col in cols])
        print(f"Inserted {target_title_id} into tbl_contentinfo")
    else:
        print(f"{target_title_id} already exists in tbl_contentinfo, updating...")
        c.execute("SELECT AppInfoJson FROM tbl_contentinfo WHERE titleId = ?", (target_title_id,))
        row = c.fetchone()
        app_json_str = None
        if row and row["AppInfoJson"]:
            try:
                info = json.loads(row["AppInfoJson"])
                info["TITLE_ID"] = target_title_id
                info["TITLE"] = target_title_name
                info["CONTENT_ID"] = target_content_id
                info["CONCEPT_ID"] = target_concept_id
                info["APP_TYPE"] = 1
                info["ATTRIBUTE3"] = 1835012
                info["DISPLAYLOCATION"] = 0
                for k in list(info.keys()):
                    if k.startswith("TITLE_") and k != "TITLE_ID":
                        info[k] = target_title_name
                app_json_str = json.dumps(info)
            except Exception:
                pass
        if app_json_str:
            c.execute("UPDATE tbl_contentinfo SET appType = 1, dispLocation = 0, titleName = ?, contentId = ?, conceptId = ?, AppInfoJson = ? WHERE titleId = ?",
                      (target_title_name, target_content_id, target_concept_id, app_json_str, target_title_id))
        else:
            c.execute("UPDATE tbl_contentinfo SET appType = 1, dispLocation = 0, titleName = ?, contentId = ?, conceptId = ? WHERE titleId = ?",
                      (target_title_name, target_content_id, target_concept_id, target_title_id))

    # 2. All tbl_iconinfo_* tables
    c.execute("SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_iconinfo_%'")
    icon_tables = [r[0] for r in c.fetchall()]
    for tbl in icon_tables:
        c.execute(f"SELECT * FROM {tbl} WHERE titleId = ?", (target_title_id,))
        if c.fetchone() is None:
            c.execute(f"SELECT * FROM {tbl} WHERE titleId = ?", (model_title_id,))
            model_row = c.fetchone()
            if model_row:
                new_row = dict(model_row)
                new_row["titleId"] = target_title_id
                new_row["titleName"] = target_title_name
                new_row["contentId"] = target_content_id
                new_row["conceptId"] = target_concept_id
                new_row["localConceptId"] = target_local_concept_id
                new_row["visible"] = 0
                new_row["installStatus"] = 2
                new_row["appDrmType"] = 5
                new_row["deeplinkUri"] = f"psgm:play?id={target_title_id}"
                new_row["hubAppUri"] = f"pshome:gamehub?titleId={target_title_id}"

                cols = list(new_row.keys())
                placeholders = ", ".join(["?"] * len(cols))
                c.execute(f"INSERT INTO {tbl} ({', '.join(cols)}) VALUES ({placeholders})",
                          [new_row[col] for col in cols])
                print(f"Inserted {target_title_id} into {tbl}")
        else:
            print(f"{target_title_id} already exists in {tbl}, updating...")
            c.execute(f"UPDATE {tbl} SET conceptId = ?, localConceptId = ?, contentId = ?, titleName = ?, visible = 0, installStatus = 2, appDrmType = 5 WHERE titleId = ?",
                      (target_concept_id, target_local_concept_id, target_content_id, target_title_name, target_title_id))

    # 3. All tbl_concepticoninfo_* tables
    c.execute("SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_concepticoninfo_%'")
    concept_tables = [r[0] for r in c.fetchall()]
    for tbl in concept_tables:
        c.execute(f"SELECT * FROM {tbl} WHERE primaryTitleId = ?", (target_title_id,))
        if c.fetchone() is None:
            c.execute(f"SELECT * FROM {tbl} WHERE primaryTitleId = ?", (model_title_id,))
            model_row = c.fetchone()
            if model_row:
                new_row = dict(model_row)
                new_row["localConceptId"] = target_local_concept_id
                new_row["conceptId"] = target_concept_id
                new_row["conceptName"] = target_title_name
                new_row["primaryTitleId"] = target_title_id
                new_row["primaryTitleName"] = target_title_name
                new_row["deeplinkUri"] = f"psgm:play?id={target_title_id}"
                new_row["hubAppUri"] = f"pshome:gamehub?titleId={target_title_id}"
                new_row["metaDataPath"] = f"/system_data/priv/appmeta/{target_title_id}"
                new_row["icon0Info"] = f"/user/app/{target_title_id}/sce_sys/icon0.png"
                new_row["pic0Info"] = f"/user/app/{target_title_id}/sce_sys/pic0.png"
                new_row["snd0Info"] = f"/user/app/{target_title_id}/sce_sys/snd0.at9"

                cols = list(new_row.keys())
                placeholders = ", ".join(["?"] * len(cols))
                c.execute(f"INSERT INTO {tbl} ({', '.join(cols)}) VALUES ({placeholders})",
                          [new_row[col] for col in cols])
                print(f"Inserted {target_title_id} into {tbl}")
        else:
            print(f"{target_title_id} already exists in {tbl}, updating...")
            c.execute(f"UPDATE {tbl} SET conceptId = ?, conceptName = ?, primaryTitleName = ? WHERE primaryTitleId = ?",
                      (target_concept_id, target_title_name, target_title_name, target_title_id))

    conn.commit()
    # Check integrity
    c.execute("PRAGMA integrity_check")
    res = c.fetchone()[0]
    if res != "ok":
        raise RuntimeError(f"Database integrity check failed: {res}")
    print("app.db integrity check passed: ok")
    conn.close()


def register_title_appinfo_db(local_db_path, model_title_id="PPSA22520",
                              target_title_id="PPSA01922",
                              target_title_name="Fortnite",
                              target_content_id="UP1477-PPSA01922_00-FORTNITECORE0001",
                              target_concept_id=210118,
                              target_local_concept_id="cid:scp:00000000000334c6"):
    conn = sqlite3.connect(local_db_path)
    c = conn.cursor()

    c.execute("SELECT COUNT(*) FROM tbl_appinfo WHERE titleId = ?", (target_title_id,))
    count = c.fetchone()[0]
    if count == 0:
        c.execute("SELECT metaDataId, key, val FROM tbl_appinfo WHERE titleId = ?", (model_title_id,))
        rows = c.fetchall()
        for metaDataId, key, val in rows:
            new_val = val
            if isinstance(val, str):
                if val == model_title_id:
                    new_val = target_title_id
                elif "ANIMAL WELL" in val:
                    new_val = val.replace("ANIMAL WELL", target_title_name)
                elif "JP8399-PPSA22520_00-0036675864053061" in val:
                    new_val = target_content_id
                elif "cid:scp:000000000098a83e" in val:
                    new_val = target_local_concept_id
                elif f"/user/app/{model_title_id}" in val:
                    new_val = val.replace(f"/user/app/{model_title_id}", f"/user/app/{target_title_id}")
            elif key == "CONCEPT_ID":
                new_val = target_concept_id
            c.execute("INSERT OR REPLACE INTO tbl_appinfo (titleId, metaDataId, key, val) VALUES (?, ?, ?, ?)",
                      (target_title_id, metaDataId, key, new_val))
        print(f"Inserted {len(rows)} key/value entries for {target_title_id} into tbl_appinfo")
    else:
        print(f"{target_title_id} already has {count} entries in tbl_appinfo, updating values...")
        c.execute("UPDATE tbl_appinfo SET val = ? WHERE titleId = ? AND key = 'CONTENT_ID'",
                  (target_content_id, target_title_id))
        c.execute("UPDATE tbl_appinfo SET val = ? WHERE titleId = ? AND key = 'CONCEPT_ID'",
                  (target_concept_id, target_title_id))
        c.execute("UPDATE tbl_appinfo SET val = ? WHERE titleId = ? AND key = 'TITLE_ID'",
                  (target_title_id, target_title_id))
        c.execute("UPDATE tbl_appinfo SET val = ? WHERE titleId = ? AND key = 'APP_TYPE'",
                  (1, target_title_id))
        c.execute("UPDATE tbl_appinfo SET val = ? WHERE titleId = ? AND key = 'ATTRIBUTE3'",
                  (1835012, target_title_id))
        c.execute("UPDATE tbl_appinfo SET val = ? WHERE titleId = ? AND key = '_local_concept_id'",
                  (target_local_concept_id, target_title_id))
        c.execute("UPDATE tbl_appinfo SET val = 0 WHERE titleId = ? AND key = 'DISPLAYLOCATION'",
                  (target_title_id,))


    conn.commit()
    c.execute("PRAGMA integrity_check")
    res = c.fetchone()[0]
    if res != "ok":
        raise RuntimeError(f"appinfo.db integrity check failed: {res}")
    print("appinfo.db integrity check passed: ok")
    conn.close()


def ensure_ftp_dirs(ftp, paths):
    for path in paths:
        parts = path.strip("/").split("/")
        cur = ""
        for p in parts:
            cur += "/" + p
            try:
                ftp.mkd(cur)
            except Exception:
                pass


def main():
    host = os.environ.get("PS5_HOST", DEFAULT_HOST)
    port = int(os.environ.get("FTP_PORT", str(DEFAULT_PORT)))
    print(f"Connecting to PS5 FTP at {host}:{port}...")
    ftp = get_ftp(host, port)

    # 1. Register in app.db
    local_app_db = "/tmp/ps5_app.db"
    backup_and_download(ftp, "/system_data/priv/mms/app.db", local_app_db)
    register_title_app_db(local_app_db)
    upload_file(ftp, local_app_db, "/system_data/priv/mms/app.db")

    # 2. Register in appinfo.db
    local_appinfo_db = "/tmp/ps5_appinfo.db"
    backup_and_download(ftp, "/system_data/priv/mms/appinfo.db", local_appinfo_db)
    register_title_appinfo_db(local_appinfo_db)
    upload_file(ftp, local_appinfo_db, "/system_data/priv/mms/appinfo.db")

    # 3. Ensure directories for PPSA01922
    target_dirs = [
        "/system_data/priv/appmeta/PPSA01922",
        "/system_data/priv/appmeta/PPSA01922/trophy2",
        "/system_data/priv/appmeta/PPSA01922/uds",
        "/user/appmeta/PPSA01922",
        "/user/appmeta/PPSA01922/trophy2",
        "/user/appmeta/PPSA01922/uds",
        "/user/app/PPSA01922",
        "/user/app/PPSA01922/sce_sys",
        "/user/app/PPSA01922/sce_sys/trophy2",
        "/user/app/PPSA01922/sce_sys/uds",
        "/user/np_uds/nobackup/conf/NPWR20561_00",
    ]
    ensure_ftp_dirs(ftp, target_dirs)

    # 4. Upload genuine icon and banner if available locally
    for icon_name, local_path in [("icon0.png", "/tmp/fortnite_icon0.png"), ("pic0.png", "/tmp/fortnite_pic0.png")]:
        if os.path.exists(local_path):
            with open(local_path, "rb") as f:
                data = f.read()
            for remote_dir in ["/system_data/priv/appmeta/PPSA01922", "/user/app/PPSA01922/sce_sys"]:
                try:
                    ftp.storbinary(f"STOR {remote_dir}/{icon_name}", io.BytesIO(data))
                    print(f"Uploaded {icon_name} to {remote_dir}")
                except Exception as e:
                    print(f"Failed to upload {icon_name} to {remote_dir}: {e}")

    # 5. Upload UDS package if available locally
    if os.path.exists("/tmp/fortnite_uds.ucp"):
        with open("/tmp/fortnite_uds.ucp", "rb") as f:
            uds_data = f.read()
        try:
            ftp.storbinary("STOR /user/np_uds/nobackup/conf/NPWR20561_00/uds.ucp", io.BytesIO(uds_data))
            ftp.storbinary("STOR /user/app/PPSA01922/sce_sys/uds/uds00.ucp", io.BytesIO(uds_data))
            print("Uploaded uds.ucp to /user/np_uds/ and /user/app/uds/")
        except Exception as e:
            print(f"Failed to upload uds.ucp: {e}")

    ftp.quit()
    print("All title registrations and assets deployed successfully!")


if __name__ == "__main__":
    main()
