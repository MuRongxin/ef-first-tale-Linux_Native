"""Resource naming and offline script-reference audit. No archive I/O or format guesses."""
from collections import defaultdict
import re


def normalized(name: str) -> str:
    name = name.replace("\\", "/")
    if not name or "\0" in name or any(p in ("", ".", "..") for p in name.split("/")):
        raise ValueError(f"非法资源名：{name!r}")
    # The archive names are ASCII; do not apply Unicode case folding to dialogue.
    name = name.translate(str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz"))
    return name


def image_alias(name):
    for suffix in (".png.tmp", ".png-"):
        if name.endswith(suffix):
            return name[:-len(suffix)] + ".png"
    return name


class ResourceIndex:
    def __init__(self, entries):
        self.entries = entries
        self.names = defaultdict(list)
        self.aliases = defaultdict(list)
        for id, item in entries.items():
            kind, name = item["kind"], normalized(item["name"])
            self.names[(kind, name)].append(id)
            self.aliases[(kind, image_alias(name) if kind == "image" else name)].append(id)

    def candidates(self, kind, name, r18=False):
        name = normalized(name)
        exact = self._eligible(self.names.get((kind, name), []), r18)
        if exact:
            return exact
        return self._eligible(self.aliases.get((kind, image_alias(name) if kind == "image" else name), []), r18)

    def _eligible(self, ids, r18):
        ids = [id for id in ids if r18 or not self.entries[id]["tag"].startswith("r18_")]
        # A patch replaces only the matching archive family, never unrelated bg/st.
        replaced = {self.entries[id]["tag"][4:] for id in ids if self.entries[id]["tag"].startswith("r18_")}
        return sorted(id for id in ids if self.entries[id]["tag"] not in replaced)

    def resolve(self, kind, name, r18=False):
        ids = self.candidates(kind, name, r18)
        if not ids:
            return {"status": "missing", "candidates": []}
        if len({self.entries[id]["source_sha256"] for id in ids}) > 1:
            # Native stage-stand loader (0x4df99c -> 0x5081a0 -> 0x5068e0) probes the st
            # archive reader first (slot 3 / 0x5e0898 at 0x506cb0) and only falls back to
            # the bg reader (slot 2 / 0x5e0628 at 0x506d2e) when st.paz lacks the file.
            # Every cross-archive bg/st duplicate in this content set is a tachie image.
            if kind == "image" and len(ids) == 2 and {self.entries[id]["tag"] for id in ids} == {"bg", "st"}:
                st = next(id for id in ids if self.entries[id]["tag"] == "st")
                return {"status": "resolved", "id": st}
            return {"status": "ambiguous", "candidates": ids}
        return {"status": "resolved", "id": ids[0]}


def references(text):
    """Yield physical line, variant, command, kind, name; dialogue is never token-scanned."""
    for line_number, raw in enumerate(text.lstrip("\ufeff").splitlines(), 1):
        line, variant = raw.lstrip(" \t"), ""
        if line.startswith(("[e].", "[j].")):
            variant, line = line[1], line[3:]
        if not line.startswith((".", "#include ", "#include\t")):
            continue
        head = re.split(r"[ \t]", line, maxsplit=1)
        cmd, rest = head[0], head[1] if len(head) == 2 else ""
        args = rest.split(" ", 3) if cmd == ".message" else rest.split()
        refs = []
        if cmd == ".message":
            if len(args) != 4:
                raise ValueError(f"第 {line_number} 行 message 字段不完整")
            refs = [("voice", args[1])]
        elif cmd in (".playBGM", ".playSE", ".playSE2", ".playVoice", ".chain", "#include"):
            if not args:
                raise ValueError(f"第 {line_number} 行 {cmd} 缺少参数")
            kind = {".playBGM": "bgm", ".playSE": "se", ".playSE2": "se", ".playVoice": "voice", ".chain": "script", "#include": "script"}[cmd]
            refs = [(kind, args[0])]
        elif cmd == ".movie":
            if len(args) < 2:
                raise ValueError(f"第 {line_number} 行 movie 字段不完整")
            refs = [("video", args[1])]
        else:
            # Covers stage, transition masks, overlay, screen, frame and effect textures.
            refs = [("image", arg) for arg in args if re.search(r"\.png(?:-|\.tmp)?$", arg, re.I)]
            refs += [("animation", arg) for arg in args if arg.lower().endswith(".ani")]
        for kind, name in refs:
            if name and name != "*":
                yield {"line": line_number, "variant": variant, "command": cmd, "kind": kind, "name": name}


def audit(entries, scripts):
    index = ResourceIndex(entries)
    issues, unique = [], set()
    count = 0
    for id, text in sorted(scripts.items()):
        for ref in references(text):
            count += 1
            unique.add((ref["kind"], normalized(ref["name"])))
            result = index.resolve(ref["kind"], ref["name"], id.startswith("r18_scr/"))
            if result["status"] != "resolved":
                issues.append({"script": id, **ref, **result})
    return {"scripts": len(scripts), "references": count, "unique_references": len(unique),
            "missing": sum(i["status"] == "missing" for i in issues),
            "ambiguous": sum(i["status"] == "ambiguous" for i in issues), "issues": issues}
