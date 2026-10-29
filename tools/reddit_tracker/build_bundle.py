#!/usr/bin/env python3
import re

with open("tools/reddit_tracker/content.js", "r", encoding="utf-8") as f:
    js = f.read()

with open("tools/reddit_tracker/styles.css", "r", encoding="utf-8") as f:
    css = f.read()

# Build standalone runner
runner = f"""(function(){{
  if(window.__redditFeedbackExtractorLoaded && document.getElementById('reddit-feedback-trigger-btn')){{
    document.getElementById('reddit-feedback-trigger-btn').click();
    return;
  }}
  var s = document.createElement('style');
  s.textContent = {repr(css)};
  document.head.appendChild(s);
  {js}
  setTimeout(function(){{
    var b = document.getElementById('reddit-feedback-trigger-btn');
    if(b) b.click();
  }}, 100);
}})();"""

# Clean up comments so that when minified into one line it won't break
lines = runner.splitlines()
clean = []
for l in lines:
    st = l.strip()
    if st.startswith("//") or st.startswith("/*"):
        continue
    # Replace trailing comments
    l = re.sub(r"\s+//.*$", "", l)
    clean.append(l)

one_line = " ".join(clean)
one_line = re.sub(r"\s+", " ", one_line)

with open("tools/reddit_tracker/console_snippet.js", "w", encoding="utf-8") as f:
    f.write(runner)

with open("tools/reddit_tracker/bookmarklet_firefox.txt", "w", encoding="utf-8") as f:
    f.write("javascript:" + one_line)

print("Created console_snippet.js and bookmarklet_firefox.txt successfully!")
