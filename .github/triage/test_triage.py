# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# The issue triage's rules on made-up issues: python3 -m unittest discover .github/triage
import json
import os
import unittest

import triage

RULES = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "rules.json")))["rules"]


def form(what="", release="", firmware="", log=""):
    def section(title, text):
        return f"### {title}\n\n{text or '_No response_'}\n\n"
    return (section("What happened?", what) + section("App release", release) + section("PS5 firmware", firmware) +
            section("Game(s) affected", "") + section("Network", "Wired") +
            section("xcloud.log", f"```text\n{log}\n```" if log else ""))


LOG = "10:00:00 [I] === PSBox Cloud Gaming starting ===\n"


class Triage(unittest.TestCase):
    def run_triage(self, body, title="A problem", latest="1.3.2"):
        return triage.triage({"title": title, "body": body}, RULES, latest)

    def test_known_error_fixed_in_newer_release(self):
        labels, comment = self.run_triage(form("It doesn't start", "v0.9.5", "13.00",
                                               LOG + "10:00:01 [E] TLS setup: certificates cacert.pem: (-15872)"),
                                          latest="0.9.6")
        self.assertIn("area: certificates", labels)
        self.assertIn("outdated-version", labels)
        self.assertIn("fw: 13.00", labels)
        self.assertNotIn("needs-info", labels)
        self.assertIn("please update to v0.9.6", comment)
        self.assertNotIn("Does it still happen there", comment)  # already asked to update
        self.assertTrue(comment.startswith(triage.MARKER))

    def test_clock_error_and_missing_release(self):
        labels, comment = self.run_triage(form("error at start", "any version", "5.50",
                                               LOG + "10:00:01 [E] TLS handshake: Certificate verification failed (-9984)"))
        self.assertIn("area: certificates", labels)
        self.assertIn("needs-info", labels)
        self.assertIn("date and time", comment)
        self.assertIn("app release", comment)

    def test_controller_words_only_in_the_description(self):
        # "vibration" in the log alone isn't a controller problem.
        labels, _ = self.run_triage(form("It froze", "1.3.2", "13.60", LOG + "10:00:02 [I] pad 0 vibration suppression"))
        self.assertNotIn("area: controller", labels)
        labels, comment = self.run_triage(form("The select button does nothing", "1.3.2", "13.60", LOG))
        self.assertIn("area: controller", labels)
        self.assertIn("touchpad click", comment)

    def test_no_false_crash_from_installed(self):
        labels, _ = self.run_triage(form("app installed without issue", "1.3.2", "13.60", LOG))
        self.assertNotIn("crash", labels)
        labels, _ = self.run_triage(form("Closed unexpectedly: signal 11 (SIGSEGV) while home", "1.3.2", "13.60", LOG))
        self.assertIn("crash", labels)

    def test_up_to_date_and_complete_says_nothing(self):
        labels, comment = self.run_triage(form("Something odd", "v1.3.2", "13.60", LOG))
        self.assertEqual(labels, ["fw: 13.60"])
        self.assertIsNone(comment)

    def test_app_version_typed_as_firmware_is_ignored(self):
        labels, _ = self.run_triage(form("x", "0.9.5", "0.9.5", LOG))
        self.assertFalse(any(label.startswith("fw: ") for label in labels))

    def test_not_the_form_asks_for_nothing(self):
        labels, comment = self.run_triage("Could you add favourites?", title="Idea: favourites")
        self.assertEqual(labels, [])
        self.assertIsNone(comment)

    def test_region_and_wake(self):
        labels, _ = self.run_triage(form("no login", "1.3.2", "", LOG + "10:00:03 [E] sign-in failed: xCloud login failed: HTTP 403"))
        self.assertIn("area: region", labels)
        labels, comment = self.run_triage(form("my xbox", "1.3.2", "", LOG + "10:00:03 [I] State WaitingForServerToRegister"))
        self.assertIn("area: remote play", labels)
        self.assertIn("Power options", comment)

    def test_dns_blocked_and_xbox_profile(self):
        labels, comment = self.run_triage(
            form("can't sign in", "1.3.0", "13.60",
                 LOG + "10:00:03 [E] sign-in failed: device code request failed: DNS lookup failed for login.live.com: 0x8041013d")
        )
        self.assertIn("area: network", labels)
        self.assertIn("NanoDNS", comment)
        self.assertIn("please update to v1.3.2", comment)

        labels, comment = self.run_triage(
            form("sign in failed", "1.3.0", "13.60",
                 LOG + "10:00:03 [E] sign-in failed: XSTS authorization for http://gssv.xboxlive.com/ failed: HTTP 401 (XErr 2148916233)")
        )
        self.assertIn("area: auth", labels)
        self.assertIn("Gamertag", comment)

    def test_multilingual_spanish_and_portuguese(self):
        labels, comment = self.run_triage(
            form("Error al adquirir el juego gratuito: 9P22THR57FV7", "1.3.0", "13.60", LOG),
            title="Error al adquirir el juego gratuito"
        )
        self.assertIn("area: catalog", labels)
        self.assertIn("Triaje automático", comment)
        self.assertIn("Error al adquirir un juego gratuito", comment)
        self.assertIn("actualiza a la v1.3.2", comment)

        labels, comment = self.run_triage(
            form("Não consigo resgatar jogo gratuito no controle", "1.3.0", "13.60",
                 LOG + "10:00:01 [I] settings: language pt-BR, best, region auto\n10:00:05 [E] erro ao resgatar 9P22THR57FV7"),
            title="Erro ao resgatar jogo"
        )
        self.assertIn("area: catalog", labels)
        self.assertIn("Triagem automática", comment)
        self.assertIn("Falha ao resgatar jogo gratuito", comment)
        self.assertIn("atualize para a v1.3.2", comment)

        # Fallback to log language when user text has no prose
        labels, comment = self.run_triage(
            form("", "1.3.0", "13.60",
                 LOG + "10:00:01 [I] settings: language pt-BR\n10:00:02 [E] DNS lookup failed: 0x8041013d"),
            title="0x8041013d"
        )
        self.assertIn("Triagem automática", comment)
        self.assertIn("DNS bloqueado", comment)

    def test_rules_are_valid(self):
        import re
        for rule in RULES:
            self.assertTrue(rule["label"] and rule["reply"] and rule["patterns"], rule["id"])
            self.assertTrue(rule.get("reply_pt") and rule.get("reply_es"), rule["id"])
            for pattern in rule["patterns"]:
                re.compile(pattern)
            for where in rule.get("where", []):
                self.assertIn(where, ("title", "what", "log"))


if __name__ == "__main__":
    unittest.main()
