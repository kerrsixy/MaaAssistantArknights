"""Reject incomplete enemy archives and incompatible stage route data."""

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from blackflow_enemy_knowledge import CategoryPage, collect, collect_details
from blackflow_stage_routes import export


def category(names, total, next_url=None):
    following = f'<a href="{next_url}">下一页</a>' if next_url else ""
    members = "".join(
        f'<li><a title="{name}" href="/arknights/{name}">{name}</a></li>'
        for name in names
    )
    return f'<a title="Navigation" href="/">导航</a><div id="mw-pages"><p>以下{len(names)}个页面属于本分类，共{total}个页面。</p>{following}<div class="mw-category"><div class="mw-category-group"><ul>{members}</ul></div></div></div><a href="/wrong">下一页</a>'


class EnemyKnowledgeTests(unittest.TestCase):
    def test_nonleader_hyperlinks_have_full_revision_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            index = {
                "verified_complete": True,
                "expected_total": 1,
                "entries": [
                    {
                        "title": "雷遁忍者",
                        "url": "https://wiki.biligame.com/arknights/雷遁忍者",
                        "category_page": 9,
                    }
                ],
            }
            (root / "category_index.json").write_text(
                json.dumps(index), encoding="utf-8"
            )
            revision = {
                "query": {
                    "pages": {
                        "8": {
                            "pageid": 8,
                            "title": "雷遁忍者",
                            "revisions": [
                                {
                                    "revid": 10,
                                    "timestamp": "2026-11-21",
                                    "slots": {
                                        "main": {
                                            "*": "{{Enemy/common|能力=遇到阻挡时起飞}}"
                                        }
                                    },
                                }
                            ],
                        }
                    }
                }
            }
            with patch("blackflow_enemy_knowledge.api", return_value=revision) as fetch:
                collect_details(root)
            self.assertEqual(fetch.call_args.kwargs["titles"], "雷遁忍者")
            record = json.loads(
                (root / "enemy_details.json").read_text(encoding="utf-8")
            )[0]
            self.assertIn(
                "遇到阻挡时起飞",
                (root / "sources" / record["source"]).read_text(encoding="utf-8"),
            )
            manifest = json.loads((root / "details_manifest.json").read_text())
            self.assertTrue(manifest["detail_sources_complete"])
            self.assertFalse(manifest["mechanics_review_complete"])
            # A damaged local source must be fetched again rather than accepted by its filename.
            (root / "sources" / record["source"]).write_text(
                "damaged", encoding="utf-8"
            )
            with patch("blackflow_enemy_knowledge.api", side_effect=OSError("offline")):
                with self.assertRaisesRegex(ValueError, "Incomplete enemy details"):
                    collect_details(root)
            self.assertFalse(
                json.loads(
                    (root / "details_manifest.json").read_text(encoding="utf-8")
                )["detail_sources_complete"]
            )

    def test_category_ignores_navigation_and_external_pagination(self):
        page = CategoryPage(category(["甲", "乙"], 3, "/arknights/next"))
        self.assertEqual([entry["title"] for entry in page.entries], ["甲", "乙"])
        self.assertEqual(page.next_url, "https://wiki.biligame.com/arknights/next")
        self.assertEqual(page.total, 3)

    def test_partial_html_is_rejected(self):
        source = category(["甲"], 1).replace("以下1个", "以下200个")
        with self.assertRaisesRegex(ValueError, "truncated"):
            CategoryPage(source)

    def test_failed_refresh_cannot_leave_a_complete_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "manifest.json").write_text(
                '{"category_complete":true}', encoding="utf-8"
            )
            with patch(
                "blackflow_enemy_knowledge.fetch", return_value=category(["甲"], 2)
            ):
                with self.assertRaisesRegex(ValueError, "Incomplete category"):
                    collect(root)
            self.assertFalse(
                json.loads((root / "manifest.json").read_text())["category_complete"]
            )

    def test_repeated_next_page_is_rejected(self):
        url = "https://wiki.biligame.com/arknights/next"
        with tempfile.TemporaryDirectory() as directory:
            with patch(
                "blackflow_enemy_knowledge.fetch",
                side_effect=[category(["甲"], 3, url), category(["乙"], 3, url)],
            ):
                with self.assertRaisesRegex(ValueError, "pagination loop"):
                    collect(Path(directory))

    def test_route_coordinates_require_matching_tiles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            level = {
                "levelId": "test",
                "mapData": {
                    "map": [[0], [1]],
                    "tiles": [{"tileKey": "end"}, {"tileKey": "start"}],
                },
                "routes": [
                    {
                        "startPosition": {"row": 0, "col": 0},
                        "endPosition": {"row": 1, "col": 0},
                        "checkpoints": None,
                        "allowDiagonalMove": True,
                        "motionMode": 0,
                    }
                ],
            }
            native = {
                "name": "test",
                "tiles": [[{"tileKey": "end"}], [{"tileKey": "start"}]],
            }
            for name, data in [
                ("level.json", level),
                ("tiles.json", native),
                ("handbook.json", {"enemyData": {}}),
            ]:
                (root / name).write_text(json.dumps(data), encoding="utf-8")
            result = export(
                root / "level.json", root / "tiles.json", root / "handbook.json"
            )
            self.assertEqual(result["routes"][0]["start"], [0, 1])
            self.assertEqual(result["routes"][0]["end"], [0, 0])
            native["tiles"][0][0]["tileKey"] = "incompatible"
            (root / "tiles.json").write_text(json.dumps(native), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "differ"):
                export(root / "level.json", root / "tiles.json", root / "handbook.json")


if __name__ == "__main__":
    unittest.main()
