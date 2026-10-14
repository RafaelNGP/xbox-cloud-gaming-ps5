#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
"""Triage of a new or edited issue, by rules (no AI, nothing to pay).

Reads the bug form's fields (What happened?, App release, PS5 firmware,
xcloud.log), matches rules.json's known problems, and then on the issue:
adds labels (never removes one), and writes or updates ONE comment (marked
with MARKER) saying what is known, asking to update when the release is
older than the fix or the latest, and asking for what is missing.

The issue's text is someone else's: it is only matched with regular
expressions here, never run or put in a command.

  GitHub Actions: triage.py            (GITHUB_EVENT_PATH, GITHUB_TOKEN, GITHUB_REPOSITORY;
                                        TRIAGE_ISSUE=<number> for one already there)
  Locally:        triage.py --dry-run issue.json --latest 0.9.6
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

    labels, notes = [], []
    asked_update = False
    for rule in rules:
        where = rule.get("where", ["title", "what", "log"])
        text = "\n".join(parts[w] for w in where)
        if not any(re.search(p, text, re.I) for p in rule["patterns"]):
            continue
        if rule["label"] not in labels:
            labels.append(rule["label"])
        note = rule["reply"]
        fixed = version_tuple(rule.get("fixed_in"))
        if fixed and release and release < fixed:
            note += f" You're on v{'.'.join(map(str, release))}: please update to v{rule['fixed_in']} or newer."
            asked_update = True
        notes.append(note)

    asks = []
    if not fields:  # not the bug form (a question, an idea): nothing to ask for
        pass
    elif not release:
        asks.append("the **app release** (Settings > Updates shows it, e.g. v0.9.6)")
    if fields and not looks_like_log(log):
        asks.append("the **xcloud.log** (in `/data/homebrew/PPSA99810/` over FTP, or use *Report a problem* "
                    "(△ in Settings or on the error) to fill this form from the app)")
    if asks:
        labels.append("needs-info")
    outdated = release and latest_t and release < latest_t
    if outdated:
        labels.append("outdated-version")
    if firmware:
        labels.append(f"fw: {firmware}")

    if not notes and not asks and not outdated:
        return labels, None
    out = [MARKER, "Automatic triage (rules, not a person; the maintainer will read this too):", ""]
    for note in notes:
        out.append(f"- {note}")
    if outdated and not asked_update:
        out.append(f"- This was reported on v{'.'.join(map(str, release))}; the latest is **v{latest}** "
                   "(the app offers it after sign-in, or Settings > Updates). Does it still happen there?")
    if asks:
        out.append("- To look into it, please add " + " and ".join(asks) + ".")
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
