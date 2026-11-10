import json, io, shutil, os, sys

P = r"D:\dsh\明日方舟\代码\MaaAssistantArknights\resource\roguelike\BlackFlow\recruitment.json"
BAK = P + ".bak-start-myrtle"

def main(apply):
    d = json.load(io.open(P, encoding="utf-8"))
    hit = None
    for g in d["priority"]:
        for o in g.get("opers", []):
            if o.get("name") in ("桃金娘",):
                hit = (g.get("name"), o)
    if not hit:
        print("not found")
        return 1
    gname, o = hit
    print("before:", gname, {k: o.get(k) for k in ("recruit_priority", "is_start", "is_key")})
    if apply:
        if not os.path.exists(BAK):
            shutil.copy2(P, BAK)
        o["is_start"] = True
        o["recruit_priority"] = 996
        io.open(P, "w", encoding="utf-8").write(
            json.dumps(d, ensure_ascii=False, indent=4) + "\n")
    print("after:", gname, {k: o.get(k) for k in ("recruit_priority", "is_start", "is_key")})
    return 0

if __name__ == "__main__":
    sys.exit(main("--apply" in sys.argv))
