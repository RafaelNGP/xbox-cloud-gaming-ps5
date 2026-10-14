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
    def run_triage(self, body, title="A problem", latest="0.9.6"):
        return triage.triage({"title": title, "body": body}, RULES, latest)

    def test_known_error_fixed_in_newer_release(self):
        labels, comment = self.run_triage(form("It doesn't start", "v0.9.5", "13.00",
                                               LOG + "10:00:01 [E] TLS setup: certificates cacert.pem: (-15872)"))
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
        labels, _ = self.run_triage(form("It froze", "0.9.6", "13.60", LOG + "10:00:02 [I] pad 0 vibration suppression"))
        self.assertNotIn("area: controller", labels)
        labels, comment = self.run_triage(form("The select button does nothing", "0.9.6", "13.60", LOG))
        self.assertIn("area: controller", labels)
        self.assertIn("touchpad click", comment)

    def test_no_false_crash_from_installed(self):
        labels, _ = self.run_triage(form("app installed without issue", "0.9.6", "13.60", LOG))
        self.assertNotIn("crash", labels)
        labels, _ = self.run_triage(form("Closed unexpectedly: signal 11 (SIGSEGV) while home", "0.9.6", "13.60", LOG))
        self.assertIn("crash", labels)

    def test_up_to_date_and_complete_says_nothing(self):
        labels, comment = self.run_triage(form("Something odd", "v0.9.6", "13.60", LOG))
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
        labels, _ = self.run_triage(form("no login", "0.9.6", "", LOG + "10:00:03 [E] sign-in failed: xCloud login failed: HTTP 403"))
        self.assertIn("area: region", labels)
        labels, comment = self.run_triage(form("my xbox", "0.9.6", "", LOG + "10:00:03 [I] State WaitingForServerToRegister"))
        self.assertIn("area: remote play", labels)
        self.assertIn("Power options", comment)

    def test_rules_are_valid(self):
        import re
        for rule in RULES:
            self.assertTrue(rule["label"] and rule["reply"] and rule["patterns"], rule["id"])
            for pattern in rule["patterns"]:
                re.compile(pattern)
            for where in rule.get("where", []):
                self.assertIn(where, ("title", "what", "log"))


if __name__ == "__main__":
    unittest.main()
