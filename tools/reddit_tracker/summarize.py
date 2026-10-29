#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Reddit Feedback Timeline Analyzer & Delta Tracker
Parses exported JSON/MD timelines from the Reddit Feedback Extractor tool.

Usage:
  python3 tools/reddit_tracker/summarize.py [path_to_export.json]
  python3 tools/reddit_tracker/summarize.py --diff old_export.json new_export.json
"""

import sys
import os
import json
import glob
from datetime import datetime

def format_ts(sec):
    if not sec:
        return "N/A"
    return datetime.fromtimestamp(sec).strftime("%Y-%m-%d %H:%M:%S")

def format_delta(c_sec, p_sec):
    if not c_sec or not p_sec:
        return ""
    diff = c_sec - p_sec
    if diff < 0:
        return "before post"
    m = diff // 60
    if m < 60:
        return f"+{m}m"
    h = m // 60
    rem_m = m % 60
    if h < 24:
        return f"+{h}h {rem_m}m"
    d = h // 24
    rem_h = h % 24
    return f"+{d}d {rem_h}h"

def analyze_json(data):
    post = data.get("post", {})
    comments = data.get("comments", [])
    
    post_time = post.get("createdUtc", 0)
    comments_sorted = sorted(comments, key=lambda c: c.get("createdUtc", 0))
    
    crashes = [c for c in comments_sorted if any(t.get("id") == "crash" for t in c.get("tags", []))]
    working = [c for c in comments_sorted if any(t.get("id") == "working" for t in c.get("tags", []))]
    ps4_comments = [c for c in comments_sorted if any(t.get("id") == "ps4" for t in c.get("tags", []))]
    ps5_comments = [c for c in comments_sorted if any(t.get("id") == "ps5" for t in c.get("tags", []))]
    
    print("\n" + "=" * 70)
    print(f"📊 REDDIT FEEDBACK REPORT: {post.get('title', 'Unknown')}")
    print(f"🔗 URL: {post.get('permalink', '')}")
    print(f"👤 Author: u/{post.get('author', '')} | Subreddit: {post.get('subreddit', '')}")
    print(f"📅 Post Date: {format_ts(post_time)} | Score: {post.get('score', 0)}")
    print("=" * 70)
    print(f"\n📈 METRICS OVERVIEW:")
    print(f"  • Total comments:     {len(comments_sorted)}")
    print(f"  • 🚨 Crashes / Errors: {len(crashes)}")
    print(f"  • ✅ Confirmed Works:  {len(working)}")
    print(f"  • 🕹️ PS4 Discussions:  {len(ps4_comments)}")
    print(f"  • 🎮 PS5 Discussions:  {len(ps5_comments)}")
    
    if crashes:
        print("\n" + "—" * 70)
        print("🚨 CRASHES & ERRORS REPORTED:")
        for c in crashes:
            ts = format_ts(c.get("createdUtc", 0))
            delta = format_delta(c.get("createdUtc", 0), post_time)
            print(f"\n  [{delta}] {ts} - u/{c.get('author')}:")
            first_lines = "\n    ".join(c.get("body", "").strip().splitlines()[:4])
            print(f"    {first_lines}")
            
    print("\n" + "—" * 70)
    print("🕒 COMPLETE CHRONOLOGICAL TIMELINE (Oldest -> Newest):")
    for c in comments_sorted:
        ts = format_ts(c.get("createdUtc", 0))
        delta = format_delta(c.get("createdUtc", 0), post_time)
        tags = " ".join(f"[{t.get('label')}]" for t in c.get("tags", []))
        op = " [OP]" if c.get("isOp") else ""
        parent = f" -> u/{c.get('parentAuthor')}" if c.get("parentAuthor") else ""
        print(f"\n[{delta}] {ts} | u/{c.get('author')}{op}{parent} {tags}")
        first_lines = "\n  ".join(c.get("body", "").strip().splitlines()[:3])
        print(f"  {first_lines}")
    print("\n" + "=" * 70 + "\n")

def diff_snapshots(old_path, new_path):
    with open(old_path, "r", encoding="utf-8") as f:
        old_data = json.load(f)
    with open(new_path, "r", encoding="utf-8") as f:
        new_data = json.load(f)
        
    old_ids = {c["id"] for c in old_data.get("comments", []) if "id" in c}
    new_comments = [c for c in new_data.get("comments", []) if c.get("id") not in old_ids]
    new_comments.sort(key=lambda c: c.get("createdUtc", 0))
    
    print("\n" + "=" * 70)
    print(f"🔄 DELTA COMPARISON: {old_path} -> {new_path}")
    print(f"🆕 NEW COMMENTS SINCE LAST CHECK: {len(new_comments)}")
    print("=" * 70)
    
    post_time = new_data.get("post", {}).get("createdUtc", 0)
    for c in new_comments:
        ts = format_ts(c.get("createdUtc", 0))
        delta = format_delta(c.get("createdUtc", 0), post_time)
        tags = " ".join(f"[{t.get('label')}]" for t in c.get("tags", []))
        op = " [OP]" if c.get("isOp") else ""
        print(f"\n⚡ NEW [{delta}] {ts} | u/{c.get('author')}{op} {tags}")
        lines = "\n  ".join(c.get("body", "").strip().splitlines())
        print(f"  {lines}")
    print("\n" + "=" * 70 + "\n")

def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "--diff":
        diff_snapshots(sys.argv[2], sys.argv[3])
        return

    target_file = None
    if len(sys.argv) >= 2:
        target_file = sys.argv[1]
    else:
        # Check reddit/ directory for any exported json files
        json_files = glob.glob("reddit/*.json") + glob.glob("reddit/**/*.json", recursive=True)
        if json_files:
            target_file = max(json_files, key=os.path.getmtime)
            print(f"Auto-selected latest file: {target_file}")
            
    if not target_file or not os.path.exists(target_file):
        print("Usage:")
        print("  python3 tools/reddit_tracker/summarize.py <file.json>")
        print("  python3 tools/reddit_tracker/summarize.py --diff <old.json> <new.json>")
        print("\nNo JSON file found in reddit/ folder.")
        sys.exit(1)

    with open(target_file, "r", encoding="utf-8") as f:
        data = json.load(f)
    analyze_json(data)

if __name__ == "__main__":
    main()
