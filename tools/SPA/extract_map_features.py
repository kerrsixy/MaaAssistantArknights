"""Extract source-backed terrain and devices without inventing their mechanics."""
import argparse
import hashlib
import json
from pathlib import Path


def inspect_level(path):
    raw = path.read_bytes()
    level = json.loads(raw)
    devices = []

    def walk(value, pointer=""):
        if isinstance(value, dict):
            inst = value.get("inst", {})
            if isinstance(inst, dict) and str(inst.get("characterKey", "")).startswith("trap_"):
                devices.append({"source_pointer": pointer, "configuration": value,
                                "effect_verified": False})
            for key, child in value.items():
                walk(child, pointer + "/" + str(key).replace("~", "~0").replace("/", "~1"))
        elif isinstance(value, list):
            for index, child in enumerate(value):
                walk(child, pointer + "/" + str(index))

    walk(level)
    data = level.get("mapData", {})
    definitions = data.get("tiles", [])
    cells = []
    for row, entries in enumerate(data.get("map", [])):
        for col, index in enumerate(entries):
            if type(index) is not int or not 0 <= index < len(definitions):
                raise ValueError(f"Invalid tile reference {path}:{row},{col}")
            tile = definitions[index]
            cells.append({"row": row, "col": col, "definition_index": index,
                          "tile": tile})
    return {"source": str(path.resolve()), "sha256": hashlib.sha256(raw).hexdigest(),
            "coordinate_space": "source_map_row_col_not_screen",
            "live_map_identity_verified": False, "cells": cells, "devices": devices}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--levels", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    maps = [inspect_level(p) for p in sorted(args.levels.glob("level_act3autochess_m*.json"))]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"maps": maps}, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"maps": len(maps), "cells": sum(len(m["cells"]) for m in maps),
                      "device_instances": sum(len(m["devices"]) for m in maps)}))


if __name__ == "__main__":
    main()
