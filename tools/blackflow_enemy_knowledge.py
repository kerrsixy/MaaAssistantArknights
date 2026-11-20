"""Audit every BWIKI enemy category page and archive leader mechanics.

This research index is not a live enemy recognizer or a source of stage routes.
"""

import argparse
import hashlib
import json
import re
import time
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from html.parser import HTMLParser
from pathlib import Path

BASE = "https://wiki.biligame.com"
API = BASE + "/arknights/api.php?"


def fetch(url):
    request = urllib.request.Request(
        url, headers={"User-Agent": "Mozilla/5.0", "Referer": BASE + "/arknights/"}
    )
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return response.read().decode("utf-8")
        except (OSError, TimeoutError):
            if attempt == 2:
                raise
            time.sleep(attempt + 1)


def api(**params):
    data = json.loads(fetch(API + urllib.parse.urlencode({"format": "json", **params})))
    if "error" in data:
        raise ValueError(data["error"])
    return data


class CategoryPage(HTMLParser):
    def __init__(self, source):
        super().__init__()
        self.in_pages = False
        self.pages_depth = 0
        self.category_depth = 0
        self.entries = []
        self.next_url = None
        self.anchor = None
        self.text = []
        self.feed(source)
        count = re.search(
            r"以下\s*([\d,]+)\s*个页面.*?共\s*([\d,]+)\s*个页面", "".join(self.text)
        )
        if count is None:
            raise ValueError("Category page has no verifiable page/total count")
        self.page_count, self.total = (int(x.replace(",", "")) for x in count.groups())
        if len(self.entries) != self.page_count:
            raise ValueError(
                f"Category entries truncated: {len(self.entries)} != {self.page_count}"
            )

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if attrs.get("id") == "mw-pages":
            self.in_pages = True
            self.pages_depth = 1
        elif tag == "div" and self.in_pages:
            self.pages_depth += 1
        if not self.in_pages:
            return
        if tag == "div":
            if self.category_depth:
                self.category_depth += 1
            elif "mw-category" in attrs.get("class", "").split():
                self.category_depth = 1
        if tag == "a":
            self.anchor = {
                "title": attrs.get("title"),
                "href": attrs.get("href", ""),
                "text": "",
            }
            if self.category_depth and self.anchor["title"]:
                self.entries.append(
                    {
                        "title": self.anchor["title"],
                        "url": urllib.parse.urljoin(BASE, self.anchor["href"]),
                    }
                )

    def handle_data(self, data):
        if self.in_pages:
            self.text.append(data)
            if self.anchor is not None:
                self.anchor["text"] += data

    def handle_endtag(self, tag):
        if tag == "div" and self.category_depth:
            self.category_depth -= 1
        if tag == "div" and self.in_pages:
            self.pages_depth -= 1
            if self.pages_depth == 0:
                self.in_pages = False
        if tag == "a" and self.anchor is not None:
            if self.anchor["text"].strip() == "下一页":
                self.next_url = urllib.parse.urljoin(BASE, self.anchor["href"])
            self.anchor = None


def write_json(path, data):
    path.write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def collect(output, refresh=True):
    output.mkdir(parents=True, exist_ok=True)
    write_json(
        output / "manifest.json",
        {
            "category_complete": False,
            "leader_source_complete": False,
            "status": "collecting",
        },
    )
    sources = output / "sources"
    sources.mkdir(exist_ok=True)
    url = BASE + "/arknights/" + urllib.parse.quote("分类:敌方")
    pages, entries, visited = [], {}, set()
    expected = None
    while url:
        if url in visited:
            raise ValueError("Category pagination loop")
        visited.add(url)
        cache = sources / f"category_{len(pages) + 1:02}.html"
        source = (
            cache.read_text(encoding="utf-8")
            if cache.exists() and not refresh
            else fetch(url)
        )
        page = CategoryPage(source)
        if expected is not None and expected != page.total:
            raise ValueError("Category changed during traversal; repeat the audit")
        expected = page.total
        filename = f"category_{len(pages) + 1:02}.html"
        (sources / filename).write_text(source, encoding="utf-8")
        pages.append(
            {
                "url": url,
                "count": len(page.entries),
                "first": page.entries[0]["title"],
                "last": page.entries[-1]["title"],
                "sha256": hashlib.sha256(source.encode()).hexdigest(),
                "source": filename,
            }
        )
        for entry in page.entries:
            if entry["title"] in entries:
                raise ValueError(f"Duplicate category member: {entry['title']}")
            entries[entry["title"]] = {**entry, "category_page": len(pages)}
        print(
            f"category page {len(pages)}: {len(page.entries)}; unique {len(entries)}/{expected}",
            flush=True,
        )
        url = page.next_url
        if url:
            query = urllib.parse.parse_qs(urllib.parse.urlsplit(url).query)
            if query.get("title") == ["分类:敌方"] and "pagefrom" in query:
                url = (
                    BASE
                    + "/arknights/"
                    + urllib.parse.quote("分类:敌方")
                    + "?"
                    + urllib.parse.urlencode({"pagefrom": query["pagefrom"][0]})
                )
    if len(entries) != expected:
        raise ValueError(f"Incomplete category: {len(entries)}/{expected}")
    write_json(
        output / "category_index.json",
        {
            "expected_total": expected,
            "verified_complete": True,
            "pages": pages,
            "entries": list(entries.values()),
        },
    )

    leaders, offset = {}, 0
    while True:
        result = api(
            action="ask",
            query=f"[[分类:敌方]][[地位::领袖]]|?敌方名称|?编号|?地位|limit=500|offset={offset}",
        )
        leaders.update(result["query"]["results"])
        next_offset = result.get("query-continue-offset")
        if next_offset is None:
            break
        if next_offset <= offset:
            raise ValueError("Leader query continuation did not advance")
        offset = next_offset
    if not leaders or not set(leaders).issubset(entries):
        raise ValueError("Leader query does not agree with the full category")
    records = []
    titles = sorted(leaders)
    for start in range(0, len(titles), 20):
        batch = titles[start : start + 20]
        result = api(
            action="query",
            prop="revisions",
            titles="|".join(batch),
            rvprop="ids|timestamp|content",
            rvslots="main",
        )
        found = set()
        for page in result["query"]["pages"].values():
            title = page["title"]
            revision = page["revisions"][0]
            raw = revision["slots"]["main"]["*"]
            filename = f"enemy_{page['pageid']}_{revision['revid']}.wikitext"
            (sources / filename).write_text(raw, encoding="utf-8")
            records.append(
                {
                    "title": title,
                    "url": entries[title]["url"],
                    "category_page": entries[title]["category_page"],
                    "page_id": page["pageid"],
                    "revision_id": revision["revid"],
                    "revision_time": revision["timestamp"],
                    "properties": leaders[title]["printouts"],
                    "source": filename,
                    "mechanics_review": "pending",
                    "stage_routes": [],
                    "live_recognition_verified": False,
                }
            )
            found.add(title)
        if found != set(batch):
            raise ValueError(f"Missing leader source pages: {set(batch) - found}")
        print(f"leader sources: {len(records)}/{len(titles)}", flush=True)
    write_json(output / "leaders.json", records)
    manifest = {
        "retrieved_at": datetime.now(timezone.utc).isoformat(),
        "category_pages": len(pages),
        "enemy_entries": len(entries),
        "leader_sources": len(records),
        "category_complete": True,
        "leader_source_complete": True,
        "mechanics_review_complete": False,
        "live_recognition_implemented": False,
        "stage_routes_verified": False,
    }
    write_json(output / "manifest.json", manifest)
    print(json.dumps(manifest, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--refresh",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Refresh category pages by default; --no-refresh reuses the archive",
    )
    args = parser.parse_args()
    collect(args.output, refresh=args.refresh)
