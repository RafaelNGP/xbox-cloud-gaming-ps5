#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
"""Triage of a new or edited issue, by rules (no AI, nothing to pay).

Reads the bug form's fields (What happened?, App release, PS5 firmware,
xcloud.log), matches rules.json's known problems, and then on the issue:
adds labels (never removes one), and writes or updates ONE comment (marked
with MARKER) in the reporter's language (English, Portuguese, or Spanish)
saying what is known, asking to update when the release is older than the
fix or the latest, and asking for what is missing.

The issue's text is someone else's: it is only matched with regular
expressions here, never run or put in a command.

  GitHub Actions: triage.py            (GITHUB_EVENT_PATH, GITHUB_TOKEN, GITHUB_REPOSITORY;
                                        TRIAGE_ISSUE=<number> for one already there)
  Locally:        triage.py --dry-run issue.json --latest 1.3.2
                  (issue.json: {"title": ..., "body": ...}, e.g. from `gh issue view N --json title,body`)
"""
import json
import os
import re
import sys
import urllib.error
import urllib.request

MARKER = "<!-- psbox-triage -->"
HERE = os.path.dirname(os.path.abspath(__file__))
MAX_TEXT = 200_000  # characters of an issue looked at

LABEL_COLOURS = {
    "needs-info": "fbca04",
    "outdated-version": "c5def5",
    "crash": "b60205",
}
AREA_COLOUR = "1d76db"
FIRMWARE_COLOUR = "ededed"

STRINGS = {
    "en": {
        "header": "Automatic triage (rules, not a person; the maintainer will read this too):",
        "update_rule": " You're on v{current}: please update to v{fixed} or newer.",
        "outdated_latest": (
            "- This was reported on v{current}; the latest is **v{latest}** "
            "(the app offers it after sign-in, or Settings > Updates). Does it still happen there?"
        ),
        "ask_release": "the **app release** (Settings > Updates shows it, e.g. v1.3.2)",
        "ask_log": (
            "the **xcloud.log** (`/data/psbox/xcloud.log` over FTP; before v0.9.7 in `/data/homebrew/PPSA99810/`, "
            "or use *Report a problem* (△ in Settings or on the error) to fill this form from the app)"
        ),
        "ask_join": " and ",
        "ask_line": "- To look into it, please add {asks}.",
    },
    "pt": {
        "header": "Triagem automática (regras automáticas; o mantenedor também lerá esta issue):",
        "update_rule": " Você está na v{current}: por favor, atualize para a v{fixed} ou mais recente.",
        "outdated_latest": (
            "- Este relato foi enviado na v{current}; a versão mais recente é a **v{latest}** "
            "(o app oferece a atualização após o login ou em Configurações > Atualizações). O problema ainda ocorre nela?"
        ),
        "ask_release": "a **versão do app** (mostrada em Configurações > Atualizações, ex.: v1.3.2)",
        "ask_log": (
            "o **xcloud.log** (`/data/psbox/xcloud.log` via FTP, ou use *Relatar um problema* "
            "(△ nas Configurações ou na tela de erro) para preencher este formulário pelo app)"
        ),
        "ask_join": " e ",
        "ask_line": "- Para investigarmos melhor, por favor adicione {asks}.",
    },
    "es": {
        "header": "Triaje automático (reglas automáticas; el mantenedor también leerá este reporte):",
        "update_rule": " Estás en la v{current}: por favor, actualiza a la v{fixed} o superior.",
        "outdated_latest": (
            "- Este reporte fue enviado en la v{current}; la versión más reciente es la **v{latest}** "
            "(la app ofrece actualizar tras iniciar sesión o en Configuración > Actualizaciones). ¿Sigue ocurriendo ahí?"
        ),
        "ask_release": "la **versión de la app** (se muestra en Configuración > Actualizaciones, ej.: v1.3.2)",
        "ask_log": (
            "el **xcloud.log** (`/data/psbox/xcloud.log` por FTP, o usa *Informar de un problema* "
            "(△ en Configuración o en la pantalla de error) para rellenar este formulario desde la app)"
        ),
        "ask_join": " y ",
        "ask_line": "- Para revisarlo, por favor añade {asks}.",
    },
}

PT_WORDS = re.compile(
    r"[ãõç]|\b(n[aã]o|consigo|jogo|jogos|resgatar|erro ao|iniciar sess[aã]o|posso|quando|"
    r"aparece|tela|ajuda|ol[aá]|obrigado|bot[aã]o|controle|carrega|logar|minha|meu|fazer)\b",
    re.I,
)
ES_WORDS = re.compile(
    r"[¿¡ñ]|\b(no me|juego|juegos|adquirir|error al|iniciar sesi[oó]n|puedo|consola|cuando|"
    r"sale|pantalla|ayuda|hola|gracias|bot[oó]n|mando|carga|guarda|hacer|est[aá]n)\b",
    re.I,
)
EN_WORDS = re.compile(
    r"\b(the|when|doesn't|does not|can't|cannot|failed|while|after|before|screen|button|"
    r"controller|game|sign-in|login|started| freezes|froze|crashed|issue|without)\b",
    re.I,
)


def detect_lang(title, what, log):
    """Detects 'pt', 'es', or 'en' from the reporter's text or the log's language setting."""
    user_text = f"{title or ''}\n{what or ''}"
    pt_hits = len(PT_WORDS.findall(user_text))
    es_hits = len(ES_WORDS.findall(user_text))
    en_hits = len(EN_WORDS.findall(user_text))
    if pt_hits > es_hits and pt_hits >= en_hits and pt_hits > 0:
        return "pt"
    if es_hits > pt_hits and es_hits >= en_hits and es_hits > 0:
        return "es"
    if en_hits == 0:
        m = re.search(r"settings:\s+language\s+(pt|es|en)\b", log or "", re.I)
        if m:
            return m.group(1).lower()
    return "en"


def parse_form(body):
    """The issue form's sections: '### Heading' -> text (without _No response_ or code fences)."""
    fields = {}
    current = None
    lines = []
    for line in (body or "")[:MAX_TEXT].splitlines():
        m = re.match(r"^###\s+(.+?)\s*$", line)
        if m:
            if current is not None:
                fields[current] = "\n".join(lines).strip()
            current, lines = m.group(1).lower(), []
        elif current is not None:
            lines.append(line)
    if current is not None:
        fields[current] = "\n".join(lines).strip()
    for key, value in fields.items():
        value = re.sub(r"^```[a-z]*\s*$", "", value, flags=re.M).strip()
        fields[key] = "" if value == "_No response_" else value
    return fields


def field(fields, *names):
    for key, value in fields.items():
        if any(key.startswith(n) for n in names):
            return value
    return ""


def version_tuple(text):
    m = re.search(r"(\d+)\.(\d+)\.(\d+)", text or "")
    return tuple(int(x) for x in m.groups()) if m else None


def firmware_of(text):
    """'13.60', '5.50' ... or None (a version of the app typed there isn't a firmware)."""
    m = re.search(r"\b(\d{1,2})\.(\d{2})\b", text or "")
    if not m or version_tuple(text):
        return None
    major = int(m.group(1))
    return f"{major}.{m.group(2)}" if 1 <= major <= 20 else None


def looks_like_log(text):
    return bool(re.search(r"\d\d:\d\d:\d\d \[[IWED]\]", text or ""))


def triage(issue, rules, latest):
    """-> (labels to add, comment text or None)."""
    title = (issue.get("title") or "")[:1000]
    body = (issue.get("body") or "")[:MAX_TEXT]
    fields = parse_form(body)
    what = field(fields, "what happened")
    log = field(fields, "xcloud.log")
    parts = {"title": title, "what": what, "log": log}
    if not fields:  # not the form: all of it is "what happened"
        parts["what"] = body
    release = version_tuple(field(fields, "app release"))
    firmware = firmware_of(field(fields, "ps5 firmware"))
    latest_t = version_tuple(latest) if latest else None
    lang = detect_lang(title, parts["what"], log)
    tr = STRINGS[lang]
    reply_key = f"reply_{lang}" if lang != "en" else "reply"

    labels, notes = [], []
    asked_update = False
    for rule in rules:
        where = rule.get("where", ["title", "what", "log"])
        text = "\n".join(parts[w] for w in where)
        if not any(re.search(p, text, re.I) for p in rule["patterns"]):
            continue
        if rule["label"] not in labels:
            labels.append(rule["label"])
        note = rule.get(reply_key) or rule["reply"]
        fixed = version_tuple(rule.get("fixed_in"))
        if fixed and release and release < fixed:
            note += tr["update_rule"].format(current=".".join(map(str, release)), fixed=rule["fixed_in"])
            asked_update = True
        notes.append(note)

    asks = []
    if not fields:  # not the bug form (a question, an idea): nothing to ask for
        pass
    elif not release:
        asks.append(tr["ask_release"])
    if fields and not looks_like_log(log):
        asks.append(tr["ask_log"])
    if asks:
        labels.append("needs-info")
    outdated = release and latest_t and release < latest_t
    if outdated:
        labels.append("outdated-version")
    if firmware:
        labels.append(f"fw: {firmware}")

    if not notes and not asks and not outdated:
        return labels, None
    out = [MARKER, tr["header"], ""]
    for note in notes:
        out.append(f"- {note}")
    if outdated and not asked_update:
        out.append(tr["outdated_latest"].format(current=".".join(map(str, release)), latest=latest))
    if asks:
        out.append(tr["ask_line"].format(asks=tr["ask_join"].join(asks)))
    return labels, "\n".join(out)


# --- GitHub ---------------------------------------------------------------------

def api(method, path, token, data=None):
    req = urllib.request.Request(
        "https://api.github.com" + path,
        method=method,
        data=None if data is None else json.dumps(data).encode(),
        headers={"Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json",
                 "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "psbox-triage"})
    with urllib.request.urlopen(req, timeout=30) as r:
        text = r.read().decode()
        return json.loads(text) if text else None


def label_colour(name):
    if name.startswith("area: "):
        return AREA_COLOUR
    if name.startswith("fw: "):
        return FIRMWARE_COLOUR
    return LABEL_COLOURS.get(name, "ededed")


def apply(repo, number, labels, comment, token):
    for name in labels:  # made the first time they're needed
        try:
            api("POST", f"/repos/{repo}/labels", token, {"name": name, "color": label_colour(name)})
        except urllib.error.HTTPError as e:
            if e.code != 422:  # 422: it exists
                raise
    if labels:
        api("POST", f"/repos/{repo}/issues/{number}/labels", token, {"labels": labels})
    if comment is None:
        return
    mine = [c for c in api("GET", f"/repos/{repo}/issues/{number}/comments?per_page=100", token)
            if MARKER in (c.get("body") or "") and c.get("user", {}).get("type") == "Bot"]
    if mine:
        if mine[0]["body"] != comment:
            api("PATCH", f"/repos/{repo}/issues/comments/{mine[0]['id']}", token, {"body": comment})
    else:
        api("POST", f"/repos/{repo}/issues/{number}/comments", token, {"body": comment})


def main(argv):
    rules = json.load(open(os.path.join(HERE, "rules.json")))["rules"]
    if len(argv) >= 3 and argv[1] == "--dry-run":
        issue = json.load(open(argv[2]))
        latest = argv[argv.index("--latest") + 1] if "--latest" in argv else None
        labels, comment = triage(issue, rules, latest)
        print("labels:", labels)
        print(comment or "(no comment)")
        return 0
    repo, token = os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_TOKEN"]
    number = os.environ.get("TRIAGE_ISSUE", "").strip()
    if number:  # by hand: an issue already there
        if not number.isdigit():
            print("TRIAGE_ISSUE must be a number")
            return 1
        issue = api("GET", f"/repos/{repo}/issues/{number}", token)
    else:
        issue = json.load(open(os.environ["GITHUB_EVENT_PATH"]))["issue"]
    if issue.get("pull_request") or issue.get("user", {}).get("type") == "Bot":
        return 0
    try:
        latest = api("GET", f"/repos/{repo}/releases/latest", token)["tag_name"].lstrip("v")
    except urllib.error.HTTPError:
        latest = None
    labels, comment = triage(issue, rules, latest)
    print(f"issue #{issue['number']}: labels {labels}, comment {'yes' if comment else 'no'}")
    apply(repo, issue["number"], labels, comment, token)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
