#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
save_dump.py - parser/dumper for "ef - the first tale" (minori / nnl engine,
English release ef_first_en_AA, Chinese fan translation) save & config files.

Read-only: only opens files for reading. Never writes to the game directory.

Supported inputs (auto-detected by content, extension is only a hint):
  *.sav        nnl_ft 1.00 save   (header + zlib -> text: <begin variables> ... <<begin backlog>>)
  *.tbr        per-script read-text bitmap (u32 tag + zlib -> 8192-byte bitmap)
  cgv.dat      CG-view registry (u32 tag + zlib -> sorted newline list of image names)
  sysparams.txt  plain text, CRLF, TAB separated config (values CP936/GBK)
  system.dat   u32 tag + 123 bytes binary, format UNKNOWN (reported, not decoded)

Usage:
  python3 save_dump.py FILE [FILE ...]            # summary + key fields
  python3 save_dump.py FILE --backlog N           # also show first N backlog entries
  python3 save_dump.py FILE --tail N              # also show last N backlog entries
  python3 save_dump.py FILE --bits                # for .tbr: print the set bit positions
  python3 save_dump.py FILE --json                # machine-readable JSON where possible
  python3 save_dump.py --dir DIR                  # summarise every supported file in DIR

Notes on proven field offsets: see SAVE_FORMAT.md in the same directory.
Text inside .sav/.tbr payloads is CP936/GBK for the Chinese translation
(the byte stream itself is ASCII-structured with GBK values).
"""

import argparse
import json
import os
import struct
import sys
import zlib

MAGIC_SAV = b"nnl_ft 1.00\x00"
ZLIB_HDR = (b"\x78\x01", b"\x78\x9c", b"\x78\xda", b"\x78\x5e")
BITMAP_SIZE = 8192
UNKNOWN = "unknown"


# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------
def read_file(path):
    with open(path, "rb") as fh:
        return fh.read()


def find_zlib(buf, start=0):
    """Return (offset, decompressed_bytes) of the first usable zlib stream."""
    for off in range(start, len(buf) - 1):
        if buf[off:off + 2] in ZLIB_HDR:
            try:
                d = zlib.decompressobj()
                out = d.decompress(buf[off:]) + d.flush()
                if out:
                    return off, out, len(d.unused_data)
            except zlib.error:
                continue
    return None, None, None


def gbk(data):
    """Decode CP936/GBK bytes; never raises."""
    if isinstance(data, str):
        data = data.encode("latin1", "replace")
    return data.decode("cp936", "replace")


def hexdump(data, base=0, width=16, maxlen=None):
    if maxlen is not None:
        data = data[:maxlen]
    lines = []
    for off in range(0, len(data), width):
        chunk = data[off:off + width]
        hx = " ".join("%02x" % c for c in chunk)
        asc = "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)
        lines.append("%08x  %-*s |%s|" % (base + off, width * 3, hx, asc))
    return "\n".join(lines)


def bit_positions(bitmap):
    """1-bit bitmap, Windows/MFC convention: bit index = i*8 + k with mask 0x80>>k."""
    return [i * 8 + k for i, v in enumerate(bitmap) for k in range(8) if v & (0x80 >> k)]


# --------------------------------------------------------------------------
# .sav
# --------------------------------------------------------------------------
def parse_sav(buf, path):
    r = {"type": "sav", "path": path, "file_size": len(buf)}

    if len(buf) < 32:
        r["error"] = "too short for a .sav"
        return r

    r["tag_u32"] = struct.unpack("<I", buf[0:4])[0]
    r["magic"] = buf[4:16]
    nul = buf.find(b"\x00", 16)
    if nul < 0:
        r["error"] = "no NUL terminator for the comment field"
        return r

    r["comment_raw"] = buf[16:nul]
    r["comment"] = gbk(buf[16:nul])
    r["reserved_u32"] = struct.unpack("<I", buf[nul + 1:nul + 5])[0] if nul + 5 <= len(buf) else None

    off, payload, unused = find_zlib(buf, max(16, nul + 1))
    if payload is None:
        r["error"] = "zlib stream not found after header"
        return r
    r["zlib_offset"] = off
    r["payload_size"] = len(payload)
    r["zlib_unused_trailing_bytes"] = unused

    text = payload.decode("latin1")  # byte-transparent view (GBK kept as bytes)
    r["has_variables"] = "<begin variables>" in text
    r["has_backlog"] = "<<begin backlog>>" in text

    # ---- variables section -------------------------------------------------
    variables = {}
    if r["has_variables"]:
        start = text.index("<begin variables>") + len("<begin variables>")
        end = text.index("<end variables>")
        body = text[start:end]
        for line in body.split("\n"):
            line = line.rstrip("\r")
            if not line:
                continue
            if "\t" in line:
                k, v = line.split("\t", 1)
            else:
                k, v = line, ""
            variables[k] = v
    r["variables"] = variables
    r["variable_count"] = len(variables)
    r["engine_variables"] = {k: v for k, v in variables.items() if k.startswith("!")}
    r["global_variables"] = {k: v for k, v in variables.items() if not k.startswith("!")}

    # ---- backlog section ---------------------------------------------------
    entries = []
    if r["has_backlog"]:
        start = text.index("<<begin backlog>>") + len("<<begin backlog>>")
        end = text.index("<<end backlog>>")
        body = text[start:end]
        cur = {}
        for line in body.split("\n"):
            line = line.rstrip("\r")
            if not line:
                continue
            if line == "ZZ":
                entries.append(cur)
                cur = {}
                continue
            if "\t" in line:
                k, v = line.split("\t", 1)
            else:
                k, v = line, ""
            cur.setdefault(k, []).append(v)
    r["backlog_entries"] = entries
    r["backlog_count"] = len(entries)

    scripts = []
    seen = set()
    for e in entries:
        sc = e.get("CS1", [""])[0]
        if sc and sc not in seen:
            seen.add(sc)
            scripts.append(sc)
    r["scripts_read_this_playthrough"] = scripts

    if entries:
        last = entries[-1]
        r["last_backlog"] = {k: (v[0] if len(v) == 1 else v) for k, v in last.items()}
    r["current_script"] = variables.get("!script_Filename", UNKNOWN)
    r["current_line_id"] = variables.get("!script_ID", UNKNOWN)
    r["current_pointer"] = variables.get("!script_Pointer", UNKNOWN)
    return r


def show_sav(r, args):
    out = []
    out.append("[.sav] %s  (%d bytes)" % (r["path"], r["file_size"]))
    if "error" in r:
        out.append("  ERROR: %s" % r["error"])
        return "\n".join(out)
    out.append("  tag_u32            = 0x%08x" % r["tag_u32"])
    out.append("  magic              = %r" % r["magic"])
    out.append("  comment/title      = %r  (GBK: %r)" % (r["comment_raw"], r["comment"]))
    out.append("  reserved u32       = %r  (always 0 in observed files)" % r["reserved_u32"])
    out.append("  zlib offset        = 0x%x, decompressed payload = %d bytes, trailing = %d"
               % (r["zlib_offset"], r["payload_size"], r["zlib_unused_trailing_bytes"]))
    out.append("  structure          = variables:%s backlog:%s"
               % (r["has_variables"], r["has_backlog"]))
    out.append("  engine vars        = %d, global (game flag) vars = %d"
               % (len(r["engine_variables"]), len(r["global_variables"])))
    out.append("  current position   = script=%s line_ID=%s pointer=%s"
               % (r["current_script"], r["current_line_id"], r["current_pointer"]))
    out.append("  backlog entries    = %d, scripts read in this playthrough = %d"
               % (r["backlog_count"], len(r["scripts_read_this_playthrough"])))
    if r["scripts_read_this_playthrough"]:
        out.append("  scripts            = %s" % ", ".join(r["scripts_read_this_playthrough"]))
    if r["global_variables"]:
        out.append("  game flag variables:")
        for k, v in r["global_variables"].items():
            out.append("      %-24s = %r" % (k, gbk(v) if any(c > 127 for c in v.encode("latin1", "replace")) else v))

    n = args.backlog or 0
    if n:
        out.append("  first %d backlog entr%s:" % (n, "y" if n == 1 else "ies"))
        out.extend(format_entries(r["backlog_entries"][:n]))
    n = args.tail or 0
    if n:
        out.append("  last %d backlog entr%s:" % (n, "y" if n == 1 else "ies"))
        out.extend(format_entries(r["backlog_entries"][-n:]))
    if args.variables and r.get("variables"):
        out.append("  variables:")
        for k, v in r["variables"].items():
            out.append("      %-26s = %r" % (k, gbk(v)))
    return "\n".join(out)


def format_entries(entries):
    out = []
    for i, e in enumerate(entries):
        line = []
        for k, vals in e.items():
            for v in vals:
                line.append("%s=%s" % (k, gbk(v) if k in ("L2", "L3") or k.startswith("L") or
                                       k in ("CT1", "CT2", "CO0", "CO1", "CO2", "Cs0", "Cs1",
                                             "Cs2", "CB1", "CE1", "CE2", "Frm", "Ct2", "CP2",
                                             "Ce2") else v))
        out.append("    #%d  %s" % (i, "  ".join(line)))
    return out


# --------------------------------------------------------------------------
# .tbr
# --------------------------------------------------------------------------
def parse_tbr(buf, path):
    r = {"type": "tbr", "path": path, "file_size": len(buf)}
    if len(buf) < 8:
        r["error"] = "too short for a .tbr"
        return r
    r["tag_u32"] = struct.unpack("<I", buf[0:4])[0]
    off, bitmap, unused = find_zlib(buf, 4)
    if bitmap is None:
        r["error"] = "zlib stream not found at offset 4"
        return r
    r["zlib_offset"] = off
    r["bitmap_size"] = len(bitmap)
    r["zlib_unused_trailing_bytes"] = unused
    r["nonzero_bytes"] = sum(1 for b in bitmap if b)
    bits = bit_positions(bitmap)
    r["set_bits"] = bits
    r["set_bit_count"] = len(bits)
    r["min_line_id"] = bits[0] if bits else None
    r["max_line_id"] = bits[-1] if bits else None
    # script name = file name without .tbr; text line IDs are normally 100 + 10*k
    r["script_guess"] = os.path.splitext(os.path.basename(path))[0] + ".sc"
    r["non_10_multiple_bits"] = [b for b in bits if b % 10]
    return r


def show_tbr(r, args):
    out = ["[.tbr] %s  (%d bytes)" % (r["path"], r["file_size"])]
    if "error" in r:
        out.append("  ERROR: %s" % r["error"])
        return "\n".join(out)
    out.append("  tag_u32            = 0x%08x  (0 in all 61 observed files)" % r["tag_u32"])
    out.append("  zlib offset        = 0x%x, bitmap size = %d bytes (fixed 65536 bits)"
               % (r["zlib_offset"], r["bitmap_size"]))
    out.append("  set bits           = %d  (read text lines)" % r["set_bit_count"])
    out.append("  line ID range      = %s .. %s" % (r["min_line_id"], r["max_line_id"]))
    out.append("  script (inferred)  = %s" % r["script_guess"])
    out.append("  non-10-multiple IDs= %d %s" % (len(r["non_10_multiple_bits"]),
                                                r["non_10_multiple_bits"][:10]))
    if args.bits:
        out.append("  set bit positions  = %s" % ", ".join(map(str, r["set_bits"])))
    return "\n".join(out)


# --------------------------------------------------------------------------
# cgv.dat
# --------------------------------------------------------------------------
def parse_cgv(buf, path):
    r = {"type": "cgv", "path": path, "file_size": len(buf)}
    r["tag_u32"] = struct.unpack("<I", buf[0:4])[0] if len(buf) >= 4 else None
    off, data, unused = find_zlib(buf, 4)
    if data is None:
        r["error"] = "zlib stream not found at offset 4"
        return r
    r["zlib_offset"] = off
    r["decompressed_size"] = len(data)
    r["zlib_unused_trailing_bytes"] = unused
    entries = [l.decode("latin1") for l in data.split(b"\n") if l]
    r["entries"] = entries
    r["entry_count"] = len(entries)
    r["sorted"] = entries == sorted(entries)
    r["duplicates"] = len(entries) - len(set(entries))
    return r


def show_cgv(r, args):
    out = ["[cgv.dat] %s  (%d bytes)" % (r["path"], r["file_size"])]
    if "error" in r:
        out.append("  ERROR: %s" % r["error"])
        return "\n".join(out)
    out.append("  tag_u32            = 0x%08x" % r["tag_u32"])
    out.append("  zlib offset        = 0x%x, decompressed = %d bytes"
               % (r["zlib_offset"], r["decompressed_size"]))
    out.append("  image-name entries = %d, sorted = %s, duplicates = %d"
               % (r["entry_count"], r["sorted"], r["duplicates"]))
    out.append("  first entries      = %s" % ", ".join(r["entries"][:8]))
    out.append("  last entries       = %s" % ", ".join(r["entries"][-5:]))
    return "\n".join(out)


# --------------------------------------------------------------------------
# sysparams.txt
# --------------------------------------------------------------------------
def parse_sysparams(buf, path):
    r = {"type": "sysparams", "path": path, "file_size": len(buf)}
    text = gbk(buf)
    r["crlf"] = "\r\n" in text
    values = {}
    order = []
    for line in text.split("\r\n") if r["crlf"] else text.split("\n"):
        if not line:
            continue
        if "\t" in line:
            k, v = line.split("\t", 1)
        else:
            k, v = line, ""
        values[k] = v
        order.append(k)
    r["values"] = values
    r["key_order"] = order
    r["key_count"] = len(values)
    return r


# semantic hints gathered from ef_first_AA.exe strings (see SAVE_FORMAT.md)
SYSPARAMS_HINTS = {
    "animation": "animation effects on/off",
    "autoSaveBacklogLimit": "backlog entries kept in the Auto save file (observed 100)",
    "autoSaveCycle": "advance (text lines) between automatic saves (observed 200)",
    "backlogVoicePlayback": "play voice when clicking backlog",
    "bgCacheCount": "background image cache entries",
    "bltOnce": "blit optimisation",
    "cls": "engine config key (purpose unknown)",
    "cursorControl": "mouse cursor control",
    "effectDrawInterval": "effect update interval",
    "etc": "extra 3-letter flag (exe string pool; semantics unknown)",
    "fontname": "message font (GBK name, e.g. '汉仪文黑-85W')",
    "fullScreenMode": "-1 = auto",
    "hir": "extra 3-letter flag (per-character? semantics unknown)",
    "joystickUsage": "joystick usage",
    "kei": "extra 3-letter flag (per-character? semantics unknown)",
    "kyo": "extra 3-letter flag (per-character? semantics unknown)",
    "lastSavePageNumber": "last page shown in the save/load screen",
    "messageSpeedAutoPlay": "auto-play speed",
    "messageSpeedRead": "speed for already-read text",
    "messageSpeedTBR": "speed used while skipping read text",
    "mid": "extra 3-letter flag (per-character? semantics unknown)",
    "miy": "extra 3-letter flag (per-character? semantics unknown)",
    "modeNNL": "nnl engine mode flag",
    "muteBGM": "BGM mute",
    "muteSE": "SE mute",
    "muteVoice": "voice mute",
    "playMode": "play mode",
    "progressInBackground": "advance while window inactive",
    "quickSaveFileNumber": "number of quick-save slots",
    "screenEffect": "screen effects",
    "screenMode": "screen mode",
    "scrollDrawInterval": "scroll draw interval",
    "setWindowPriority": "window priority",
    "shadow": "text shadow",
    "shakeDrawInterval": "shake draw interval",
    "skipShowing": "show the skip indicator",
    "stCacheCount": "standing-character image cache entries",
    "stopVoiceAtNext": "stop voice at the next line",
    "stretchAA": "anti-aliased screen stretching",
    "stretchAcc": "high precision screen stretching",
    "symbolFontname": "symbol/ruby font (GBK name, e.g. '黑体')",
    "transitionDrawInterval": "transition draw interval",
    "volumeBGM": "BGM volume 0-100",
    "volumeSE": "SE volume 0-100",
    "volumeVoice": "voice volume 0-100",
    "windowHeight": "window height (px)",
    "windowMode": "window mode",
    "windowWidth": "window width (px)",
    "windowXpos": "window X position",
    "windowYpos": "window Y position",
    "yuk": "extra 3-letter flag (per-character? semantics unknown)",
}


def show_sysparams(r, args):
    out = ["[sysparams.txt] %s  (%d bytes)" % (r["path"], r["file_size"])]
    out.append("  format             = plain text, %s line endings, TAB separated key/value"
               % ("CRLF" if r["crlf"] else "LF"))
    out.append("  key count          = %d" % r["key_count"])
    for k in r["key_order"]:
        v = r["values"][k]
        hint = SYSPARAMS_HINTS.get(k, "")
        out.append("    %-26s = %-22r %s" % (k, v, hint))
    return "\n".join(out)


# --------------------------------------------------------------------------
# system.dat
# --------------------------------------------------------------------------
def parse_systemdat(buf, path):
    r = {"type": "system.dat", "path": path, "file_size": len(buf),
         "tag_u32": struct.unpack("<I", buf[0:4])[0] if len(buf) >= 4 else None,
         "payload_hex": buf[4:].hex() if len(buf) > 4 else "",
         "status": UNKNOWN}
    return r


def show_systemdat(r, args):
    out = ["[system.dat] %s  (%d bytes)" % (r["path"], r["file_size"])]
    out.append("  tag_u32            = 0x%08x" % r["tag_u32"])
    out.append("  payload            = %d bytes of high-entropy binary" % (r["file_size"] - 4))
    out.append("  status             = UNKNOWN - not zlib/gzip/bz2/lzma/raw-deflate, not 8/4-byte")
    out.append("                       block aligned (no TEA/XXTEA), no single-byte XOR match")
    out.append("  hexdump:")
    out.append(hexdump(read_file(r["path"]), 0, 16))
    return "\n".join(out)



# --------------------------------------------------------------------------
# .png (save-slot thumbnails)
# --------------------------------------------------------------------------
PNG_COLOUR_TYPE = {0: "grey", 2: "RGB", 3: "palette", 4: "grey+alpha", 6: "RGBA"}


def parse_png(buf, path):
    r = {"type": "png", "path": path, "file_size": len(buf)}
    if buf[:8] != b"\x89PNG\r\n\x1a\n" or len(buf) < 33:
        r["error"] = "not a PNG file"
        return r
    w, h, depth, ctype, comp, filt, inter = struct.unpack(">IIBBBBB", buf[16:29])
    chunks = []
    idat = b""
    i = 8
    while i + 8 <= len(buf):
        ln = struct.unpack(">I", buf[i:i + 4])[0]
        typ = buf[i + 4:i + 8].decode("latin1")
        chunks.append((typ, ln))
        if typ == "IDAT":
            idat += buf[i + 8:i + 8 + ln]
        i += 12 + ln
    r.update(width=w, height=h, bit_depth=depth, colour_type=ctype,
             colour_type_name=PNG_COLOUR_TYPE.get(ctype, "?"), interlace=inter,
             chunks=chunks)
    try:
        raw = zlib.decompress(idat)
        r["raw_image_bytes"] = len(raw)
    except zlib.error:
        r["raw_image_bytes"] = None
    return r


def show_png(r, args):
    out = ["[.png] %s  (%d bytes)" % (r["path"], r["file_size"])]
    if "error" in r:
        out.append("  ERROR: %s" % r["error"])
        return "\n".join(out)
    out.append("  dimensions         = %dx%d px, bit depth %d, colour type %d (%s)"
               % (r["width"], r["height"], r["bit_depth"], r["colour_type"],
                  r["colour_type_name"]))
    out.append("  interlace          = %d, raw image bytes = %s"
               % (r["interlace"], r["raw_image_bytes"]))
    out.append("  chunks             = %s" % ", ".join("%s(%d)" % c for c in r["chunks"]))
    out.append("  note: save-slot thumbnail; pairs 1:1 with the .sav of the same base name")
    return "\n".join(out)


# --------------------------------------------------------------------------
# dispatch
# --------------------------------------------------------------------------
def classify(buf, path):
    name = os.path.basename(path).lower()
    if name == "sysparams.txt":
        return "sysparams"
    if name == "cgv.dat":
        return "cgv"
    if name == "system.dat":
        return "systemdat"
    if name.endswith(".tbr"):
        return "tbr"
    if name.endswith(".sav"):
        return "sav"
    if name.endswith(".png"):
        return "png"
    # fall back to content sniffing
    if len(buf) >= 16 and buf[4:16] == MAGIC_SAV:
        return "sav"
    if len(buf) >= 8 and buf[0:2] == b"\x78\x9c":
        return "tbr"
    if len(buf) >= 6 and buf[4:6] == b"\x78\x9c":
        return "cgv"
    return UNKNOWN


def process(path, args):
    buf = read_file(path)
    kind = classify(buf, path)
    if kind == "sav":
        r = parse_sav(buf, path)
        return show_sav(r, args), r
    if kind == "tbr":
        r = parse_tbr(buf, path)
        return show_tbr(r, args), r
    if kind == "cgv":
        r = parse_cgv(buf, path)
        return show_cgv(r, args), r
    if kind == "sysparams":
        r = parse_sysparams(buf, path)
        return show_sysparams(r, args), r
    if kind == "systemdat":
        r = parse_systemdat(buf, path)
        return show_systemdat(r, args), r
    if kind == "png":
        r = parse_png(buf, path)
        return show_png(r, args), r
    return ("[UNKNOWN] %s (%d bytes) - not a recognised ef save/config file\n%s"
            % (path, len(buf), hexdump(buf, 0, 16, 0x40))), {"path": path, "type": UNKNOWN}


def main(argv):
    ap = argparse.ArgumentParser(
        description="Dump/parse ef - the first tale save & config files (read-only).")
    ap.add_argument("files", nargs="*", help="files to dump")
    ap.add_argument("--dir", help="summarise every supported file in this directory")
    ap.add_argument("--backlog", type=int, default=0, metavar="N",
                    help="show first N backlog entries of a .sav")
    ap.add_argument("--tail", type=int, default=0, metavar="N",
                    help="show last N backlog entries of a .sav")
    ap.add_argument("--variables", action="store_true", help="show all variables of a .sav")
    ap.add_argument("--bits", action="store_true", help="show .tbr set-bit positions")
    ap.add_argument("--json", action="store_true", help="emit JSON for the structured parts")
    ap.add_argument("--head", type=int, default=0, metavar="N",
                    help="also hexdump the first N bytes of the file")
    args = ap.parse_args(argv)

    files = list(args.files)
    if args.dir:
        for name in sorted(os.listdir(args.dir)):
            if os.path.isfile(os.path.join(args.dir, name)):
                files.append(os.path.join(args.dir, name))
    if not files:
        ap.print_help()
        return 2

    rc = 0
    for path in files:
        try:
            text, rec = process(path, args)
        except Exception as exc:  # graceful on unknown/corrupt parts
            text, rec = "[ERROR] %s: %s" % (path, exc), {"path": path, "error": str(exc)}
            rc = 1
        if args.json:
            slim = dict(rec)
            if "backlog_entries" in slim:
                slim["backlog_entries"] = len(slim["backlog_entries"])
            if "entries" in slim:
                slim["entries"] = len(slim["entries"])
            if "set_bits" in slim:
                slim["set_bits"] = len(slim["set_bits"])
            if "engine_variables" in slim:
                slim["engine_variable_names"] = list(slim.pop("engine_variables"))
            if "scripts_read_this_playthrough" in slim:
                slim["scripts_read_this_playthrough"] = list(slim["scripts_read_this_playthrough"])
            print(json.dumps(slim, ensure_ascii=False, indent=1, default=str))
        else:
            print(text)
            if args.head:
                print(hexdump(read_file(path), 0, 16, args.head))
        print()
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
