> **英文原稿（备份，供对照）** — 正式中文译本见同目录下同名文件（去掉 `.en`）。

# PAZ container format — minori engine (`ef - the first tale`, English Steam build)

Status: **complete and verified** against all 13 `.paz` files in this installation.
This document is written as an implementation specification for a clean-room Linux
port; `paz.py` in this directory is the reference implementation (only dependency:
`pycryptodome` for Blowfish).

Reverse-engineering evidence and reproduction tools are in `re_tools/`
(key dumps, exe disassembly excerpts, structural checks, extraction verifier).

---

## 0. TL;DR for the port

```
0x00  char   game_id[32]        NUL-terminated; rest is packer garbage (ignore)
0x20  u32le  toc_len_obf
0x24  u8     toc[toc_len]       obfuscated (XOR simple_key) + Blowfish-ECB encrypted
...   u8     entry data area    8-byte aligned entries, offsets absolute
```

* `simple_key = toc_len_obf >> 24`; `toc_len = toc_len_obf ^ (simple_key * 0x01010101)`
  (in this installation `simple_key == 0` for all 13 archives, i.e. no XOR layer).
* TOC is **Blowfish-ECB** encrypted with a per-archive-prefix 32-byte key
  (`bg`, `bgm`, `mov`, `scr`, `se`, `st`, `sys`, `voice`).
* `ef`'s Blowfish is the standard algorithm with one quirk: the 64-bit **data block
  is treated as two little-endian 32-bit words** (key words stay big-endian).
  Recipe: `plain = rev4( StandardBlowfish_ECB_decrypt( key = K,
  data = rev4(ciphertext) ) )`, where `rev4()` reverses bytes inside each 4-byte group.
* Entry payload: XOR `simple_key` → Blowfish-ECB with the per-prefix DATA key →
  (only for uncompressed entries whose name matches png/ogg/sc/avi/mpg or for
  bgm/se/voice archives) a per-entry RC4 with a name-derived key → truncate to
  `stored_size` → zlib-inflate if `is_packed`.
* `mov.paz` does **not** use Blowfish for entry data; it uses a 256-byte RC4 key
  derived from a seed table in the TOC + the per-entry name key.
* Keys for this exact game build are in §6. They come from the game's own exe
  (stored there byte-negated); r18_cn archives use the same keys.

---

## 1. Archive naming and key selection

The archive's lowercase base name without `.paz` selects the key pair, one of:

`bg bgm mov scr se st sys voice` (also `pmbgm pmse pmvoice` in other minori titles).

The **game build** selects the 32-byte key set. For this installation the exe is
the English Steam build (`ef_first_AA.exe`, version resource / RTTI / embedded key
table all consistent) → key set **“ef - the first tale (en steam)”** (§6.1).
This was verified by finding the key bytes (byte-negated) in the exe at file
offset `0x1a7ae0..0x1a7d20`, and then by successfully decrypting every TOC.

`r18_cn/*.paz` are the R18-patch data set; they use the **same** key set and format.

Engine behaviour for multi-part archives: it additionally opens `X.pazA`, `X.pazB`,
… in order and treats them as a concatenated stream (entry offsets are absolute in
that concatenation). This installation has no part files, so that path is
implemented but untested. `paz.py` supports it.

---

## 2. On-disk layout

| Offset | Size | Type | Meaning |
|---|---|---|---|
| `0x000` | 32 | bytes | game-id string, ASCII, NUL-terminated. Observed: `"ef_first_en_AA"` (root), `"ef_first_en_AA_R18Patch"` (r18_cn). Bytes after the NUL are **uninitialised packer garbage** (differ per file, ignored by the engine and by every known tool). |
| `0x020` | 4 | u32 LE | `toc_len_obf` — obfuscated TOC byte length |
| `0x024` | `toc_len` | bytes | TOC (obfuscated + encrypted), `toc_len` is always a multiple of 8 |
| `0x024 + toc_len` | rest | bytes | entry data area; every entry starts at an absolute, 8-byte-aligned file offset |

Header parsing (see `paz.py: PazArchive._read_header`):

```
v          = u32le @ 0x20
simple_key = v >> 24                      # single byte (0 in all 13 files here)
toc_len    = v ^ (simple_key * 0x01010101) if simple_key else v
assert toc_len % 8 == 0 and 0x24 + toc_len <= file_size
data_start = 0x24 + toc_len
```

The engine's own reader (`ef_first_AA.exe` @ 0x52E990) reads 0x20 bytes of header,
then 4 bytes, then the TOC buffer, and calls its "decrypt hook" @ 0x435390 — which
is the Blowfish-ECB decrypt below. The reference extractor (`Inori/FuckGalEngine
Minori/main.cpp`) shows the same layout as `struct PAZHDR { u8 unknown[32]; u32
toc_len; }` and derives the byte obfuscation key as `hdr.toc_len >> 24`.

**Observed header values** (all `simple_key = 0x00`):

| archive | game_id | toc_len | file size | data_start | entries |
|---|---|---:|---:|---:|---:|
| bg.paz | ef_first_en_AA | 143128 (0x22f18) | 1272342060 | 143164 | 3457 |
| bgm.paz | ef_first_en_AA | 1096 (0x448) | 51733692 | 1132 | 29 |
| mov.paz | ef_first_en_AA | 424 (0x1a8) | 51003452 | 460 | 4 |
| scr.paz | ef_first_en_AA | 2576 (0xa10) | 405268 | 2612 | 75 |
| se.paz | ef_first_en_AA | 3640 (0xe38) | 6494884 | 3676 | 89 |
| st.paz | ef_first_en_AA | 43672 (0xaa98) | 121894852 | 43708 | 1033 |
| sys.paz | ef_first_en_AA | 24936 (0x6168) | 20932372 | 24972 | 572 |
| voice.paz | ef_first_en_AA | 233216 (0x38f00) | 217589044 | 233252 | 5830 |
| r18_cn/bg.paz | ef_first_en_AA_R18Patch | 35848 (0x8c08) | 288256132 | 35884 | 868 |
| r18_cn/scr.paz | ef_first_en_AA_R18Patch | 680 (0x2a8) | 79868 | 716 | 19 |
| r18_cn/st.paz | ef_first_en_AA_R18Patch | 88 (0x58) | 125180 | 124 | 2 |
| r18_cn/sys.paz | ef_first_en_AA_R18Patch | 2896 (0xb50) | 1540276 | 2932 | 63 |
| r18_cn/voice.paz | ef_first_en_AA_R18Patch | 9936 (0x26d0) | 13178484 | 9972 | 248 |

**Header id field note.** The 32-byte field is a NUL-terminated string; the bytes
after the NUL are leftover memory from the packer. Evidence: root archives contain
`"ef_first_en_AA"` (14 chars) + NUL at 0x0e + 17 varying garbage bytes; r18 archives
contain `"ef_first_en_AA_R18Patch"` (23 chars) + NUL at 0x17 + 8 varying garbage
bytes. All decryption/parsing ignores those bytes. The engine apparently only uses
the string as a game/version identifier.

---

## 3. Byte-obfuscation layer (trivial XOR)

The engine can optionally XOR the whole header + TOC + data region with one byte,
derived from the *stored* TOC length:

```
simple_key = toc_len_obf >> 24          # 0x00 in every archive of this installation
```

Decoding the TOC on disk (per the reference extractor and the engine's
`read_unobfuscate` @ 0x52D7F0):

```
blob = file[0x24 : 0x24 + toc_len]
if simple_key:
    blob[i] ^= simple_key   for every i          # 1-byte key
```

(In the engine the same step is also applied to the 36-byte header after the key is
derived, and to every entry payload.) Because `simple_key == 0` for all 13 files
here, this layer is a no-op in practice; the non-zero path is untested. Note the
value you read at 0x20 is itself already XORed, which is why the key is taken from
its high byte: `stored_high_byte = (toc_len>>24) ^ simple_key = simple_key` as long
as `toc_len < 2^24` (true here: max 233216).

---

## 4. Blowfish variant (critical detail)

The game uses the **standard Blowfish** block cipher (Bruce Schneier's algorithm,
16 rounds, P-array + 4 S-boxes = the standard digits-of-π constants; present in the
exe at `.rdata` file offset 0x170dd8) in **ECB mode, no IV, no padding**, with a
**32-byte key**, and these two byte-order conventions:

* **key material**: standard Blowfish key schedule, i.e. the 32 key bytes are read
  in 4-byte groups interpreted **big-endian** (as in the reference test vectors);
* **data block**: the 8 ciphertext bytes are loaded as two **little-endian** u32
  words (x86 native loads). Equivalently, the data words are byte-reversed with
  respect to the usual big-endian convention.

Why: the engine's decrypt function (`ef_first_AA.exe` VA 0x435393, key schedule
0x435040) loads `mov ecx,[edx]` and then indexes S-boxes as
`S0[byte3] + S1[byte2] ^ S2[byte1] + S3[byte0]`, i.e. standard F() applied to the
little-endian word; the key schedule assembles `key[j]<<24 | key[j+1]<<16 | …`
(big-endian). This mismatched "half-ported" reference implementation is exactly what
the original `exf2paz` / FuckGalEngine `blowfish.cpp` reproduces, and GARbro's C#
`Blowfish` class behaves the same way.

### Portable recipe (recommended: delegate to any standard Blowfish)

```
def rev4(b):                    # reverse bytes within each 4-byte group
    return b''.join(b[i:i+4][::-1] for i in range(0,len(b),4))

def paz_blowfish_decrypt(key32, ciphertext):      # len % 8 == 0, ECB
    assert len(ciphertext) % 8 == 0
    return rev4( std_blowfish_ecb_decrypt(key32, rev4(ciphertext)) )
```

`std_blowfish_ecb_decrypt` can be OpenSSL `BF-ECB` (legacy provider), Crypto++,
pycryptodome (`Crypto.Cipher.Blowfish.MODE_ECB`), libtomcrypt, or any Blowfish
implementation that passes the standard big-endian test vectors. Both the TOC and
the entry data use this same function with different keys.

### Self-contained pseudocode (if you don't want a Blowfish dependency)

Standard Blowfish with these two substitutions:

```
/* key schedule: build P[0..17] from key bytes, 4 bytes per word, BIG-endian */
data = (key[j]<<24) | (key[j+1]<<16) | (key[j+2]<<8) | key[j+3];  P[i] ^= data

/* block load/store: LITTLE-endian words */
L = b[0] | b[1]<<8 | b[2]<<16 | b[3]<<24
R = b[4] | b[5]<<8 | b[6]<<16 | b[7]<<24
... standard 16-round Feistel with decryption order (P[17], P[16..1], P[0]) ...
```

Equivalently: keep the standard big-endian convention everywhere, but byte-reverse
each 4-byte group of *both* the input ciphertext and the output plaintext, and
byte-reverse each 4-byte group of the key before the key schedule. (Do **not** use
CBC: there is no chaining and no IV.)

Verified: decrypting `scr.paz`'s TOC this way gives `count = 75` and the first file
name `test.sc`; all 13 archives decrypt to plausible, fully consistent TOCs.

---

## 5. TOC structure

Plaintext of the decrypted TOC (all integers little-endian):

```
u32  entry_count
if archive prefix == "mov":
    u8  seed[256]                    # seed table for the MOV entry cipher
repeat entry_count times:
    char name[]                      # NUL-terminated; CP932/Shift-JIS, ASCII in practice
    u64  offset                      # absolute offset in the (concatenated) archive
    u32  original_size               # unpacked size
    u32  stored_size                 # payload size: compressed size if packed, else raw size
    u32  aligned_size                # on-disk payload size = align8(stored_size)
    u32  is_packed                   # 0 = raw, 1 = zlib-compressed
u8  zero_padding[]                   # up to toc_len; toc_len is toc_len_obf deobfuscated, 8-aligned
```

`offset` is absolute (`0x24 + toc_len + relative`), i.e. the first entry's offset
equals `data_start`.

**Structural invariants (verified for all 13 archives, see
`re_tools/toc_check.json`):**

1. `toc_len == align8(4 + (256 if mov else 0) + Σ_i (len(name_i) + 1 + 24))`
2. `min(entry.offset) == 0x24 + toc_len` (data starts immediately after the TOC)
3. `max(entry.offset + entry.aligned_size) == file_size` (no gaps, no trailer)
4. every `aligned_size % 8 == 0`, `stored_size <= aligned_size`, `is_packed ∈ {0,1}`

Note for porters comparing with IDA output: the 24-byte record can be seen as
`u32 a, b, c, d, e, f` where `(a,b)` are the low/high halves of the 64-bit offset
and `c,d,e,f = original_size, stored_size, aligned_size, is_packed`.

Names are CP932 (Shift-JIS) in the engine; in these archives they are plain ASCII.
`voice.paz` entries have **no file extension** (they are Ogg Vorbis audio, sniff
`OggS`). Some names intentionally contain `.png-`, `.png.tmp` or backslash
directory separators, e.g. `thumb\th_216_05.png`.

---

## 6. Keys (this game build)

Keys are 32 bytes each, per archive prefix: **TOC key** (decrypts the TOC) and
**DATA key** (decrypts entry payloads). The engine stores them in its exe
**byte-negated (two's-complement per byte)**: for the table below,
`exe_byte = (~real_byte) & 0xFF`, found in `.data` at file offset
0x1a7ae0 + 32·k. GARbro and FuckGalEngine list the same keys in positive form.

### 6.1 `ef - the first tale (en steam)` — **the key set used by this installation**

Type passwords ("crud"): `png = a7b77qgx`, `ogg = 52AUwF8B`, `sc = 102ycz1s`, `avi = qmzj4Y9C`.

| prefix | TOC key (hex) | DATA key (hex) |
|---|---|---|
| `bg` | `8c7919517e51a0fd8348dc85cb68858e68905bd7d5b96336ea3699c15b683f83` | `ca3c7817e1c631e5f88de7aff702a3697cb9d5e337e54eda45546c2910c75fde` |
| `bgm` | `78e52f7768fa49ea1b0ef53f7b38014a835bc8c948eb78d0ab2688b85d46f307` | `8b7731e7aa9dae4c1f216750c32b9d395794b257c353a3c9dfa880733026d32b` |
| `mov` | `5a4caf2034cc9510780ce2b9bae97b9c2026274e7fdd36c58d10325f0337e1ea` | `5a4caf2034cc9510780ce2b9bae97b9c2026274e7fdd36c58d10325f0337e1ea`² |
| `scr` | `72634e755d2c980b205f82828bde4379f37bd2b66d4390b6436a4d9f8389ca5a` | `b7d2955cf7a0d3ecc20ccbf887a8c1b6d498ecf5f9b8361e897e4dbc7bad1465` |
| `st` | `b891bb7b469812fe98cc267bf1356131c694d340d79d8cd10db45c5d81c44a9c` | `630bdf10c9e57ef99126b50476f59800240cd1b3766a1416e31ebfee27694f93` |
| `sys` | `0112c9880d9dd23a0573bac209226d0e80a8ae1ef8b33a76494962531bd1110b` | `fd399525e2b2762ff9bc115dd145f6eb9e6de3574574d04bcd21146be6537951` |
| `voice` | `2fd5d774760b34126f7c3e964fe6ca4cfcdf6a730b891cb1618335270feb79df` | `151694fdcf2ce124a14e6d1e46e1a7b17395c0cc172da47ca3ad8cae42043258` |
| `se` | `c3011a3ed51fcd2bd3904dd1772e469709b10d5dc0cb4652cff5b3a57cde9038` | `3dca17d9518de518087077409c8bcb396a6be53da76e650238e09905096d1aa8` |

² the `mov` DATA key is unused by the engine (MOV uses the RC4 scheme of §8.3); the
table just repeats the TOC key. `r18_cn/*.paz` use this same set.

Other minori titles / builds in this engine family use different key sets; the full
23-title table extracted from the reference tool is in
`re_tools/reference_keys_all23games.json` (includes `ef - the first tale (en)` and
`(jp)` variants, in case a different release needs porting).

---

## 7. Entry data — normal archives (everything except `mov`)

```
def read_entry(e):
    buf = read_file_range(e.offset, e.aligned_size)      # across .pazA.. parts if any
    if simple_key: buf ^= simple_key                    # no-op here
    buf = paz_blowfish_decrypt(DATA_KEY[prefix], buf)   # whole aligned buffer

    pw = ""
    if e.is_packed == 0:                                # per-entry RC4 only for raw entries
        pw = type_password(e.name, prefix)              # see table below

    if pw:
        key = (e.name.lower() + " " + "%08X" % e.stored_size + " " + pw).encode("cp932")
        buf = rc4_standard(buf, key)                    # standard RC4, keystream starts at 0

    payload = buf[0:e.stored_size]
    if e.is_packed:
        out = zlib_inflate(payload)                     # standard zlib, e.g. header 78 9C
        assert len(out) == e.original_size
        return out
    assert len(payload) == e.original_size
    return payload


def type_password(name, prefix):                        # exact engine rule
    low = name.lower()
    if ".png" in low:                     return png_pw
    elif ".ogg" in low or prefix in {"bgm","se","voice"}: return ogg_pw
    elif ".sc" in low:                    return sc_pw
    elif ".avi" in low or ".mpg" in low:  return avi_pw
    else:                                 return ""
```

Notes:

* The check is a **substring** test on the *original-case* name (the engine uses
  `find`, case-sensitive); the RC4 seed string uses the **lowercased** name.
  `%08X` is the *stored_size* (equivalently the size after decrypt; for raw entries
  it equals `original_size`).
* The RC4 key is the CP932/ASCII byte string of
  `"<lower name> <8 uppercase hex digits> <password>"` — note the single spaces.
* `zlib` streams are normal zlib (RFC1950, `78 9C` at the start; level 9).
  The game's packer only compresses *some* files (scripts, `.ani`, some `.png`);
  see EXTRACTION_REPORT.md for per-archive counts.
* **Known quirk**: in `st.paz` and `r18_cn/st.paz`, every compressed `.ani` entry
  inflates to `original_size + 8` bytes whose last 8 bytes are `00`. The engine
  (and the original packer) consume only `original_size` bytes. A correct extractor
  must truncate to `original_size` (the reference `paz.py` records this) — verified
  506/506 `st.paz` compressed entries and 1/1 in the r18 archive; all other
  compressed entries inflate to exactly `original_size`.
* The aligned padding after `stored_size` decrypts to zeros in all observed files.

### 7.1 Optional `simple_key` byte — applied to the payload as well

Engine source shows the payload is also XORed with `simple_key` before the Blowfish
step (implemented in `paz.py`; no-op in this installation).

---

## 8. Entry data — `mov.paz` (special)

`mov.paz` is the *only* archive that does not use Blowfish for its entries. The TOC
contains an extra 256-byte `seed` table right after `entry_count`; entries are
handled as:

```
seed2 = (e.name.lower() + " " + "%08X" % e.stored_size + " " + type_password).encode("cp932")
rc4_key[i] = seed[i] ^ seed2[i % len(seed2)]      # i = 0..255
ks         = rc4_standard_keystream(rc4_key, 0x10000)   # RC4 applied to 0x10000 zero bytes
for k in range(e.aligned_size):
    buf[k] ^= ks[k % 0x10000]
payload = buf[0:e.stored_size]
# is_packed == 0 for all 4 MOV entries in this game; if set, zlib as usual
```

The TOC seed and the entry key string are exactly as in FuckGalEngine's
`decrypt_mov()` / GARbro's `MovPazArchive`. All 4 MOV entries in this game are
uncompressed (`is_packed = 0`) and contain MPEG-PS or AVI files.

---

## 9. Obfuscation/encryption summary per region

| Region | Transform (in order) |
|---|---|
| whole header+TOC+data | XOR single byte `simple_key` (from stored TOC-length high byte; 0 here) |
| TOC | Blowfish-ECB, TOC key, 32-byte key; `toc_len` multiple of 8 |
| entry payload | Blowfish-ECB, DATA key, over `aligned_size` bytes (except `mov`) |
| entry payload, raw entries with png/ogg/sc/avi/mpg name (or audio archive) | RC4 with name/size/password key |
| `mov` entries | 256-byte RC4 key stream (seed⊕name-key), 0x10000-byte keystream XOR |
| packed entries | zlib inflate of the first `stored_size` bytes |

There is no LZ77/other custom compression. `0x435390`/`0x435393` in the exe is the
Blowfish decrypt routine (the "optional decompression hook" seen in IDA is
decryption, not decompression); zlib decompression is a separate Crypto++
`ZlibDecompressor` call per packed entry.

---

## 10. `r18_cn/` override set

* Same container format and same keys; `game_id = "ef_first_en_AA_R18Patch"`.
* It is a *secondary data set* (installed by the R18 patch), not a full copy:
  868/19/2/63/248 entries for bg/scr/st/sys/voice. The engine's exe has a registry
  flag `SOFTWARE\minori\ef_first_en_r18patch`; the patch directory is searched
  before/merged with the base archives (by file name). Contents: new R18 CG and
  standing images (`evh*`, `ev*`, `st_*`), name tables, 6 new scripts and 13
  overridden scripts, 248 new voice files, 59 new UI thumbnails + 4 overriding
  `MemoriesSelect*` images.
* `r18_cn/bg.paz` contains two plain-text files (also standard PAZ entries):
  * `names.txt` — CP932/Shift-JIS table mapping standing-image ids to Japanese
    names, e.g. `st_miyako<TAB>みやこ`;
  * `log.txt` — a leftover 2011 `dir` listing from the original packing machine
    (source PSD file names/sizes/timestamps) — evidence of the packer's inputs.

---

## 11. Cross-implementation confirmation

| Source | What it contributes | Verification |
|---|---|---|
| This installation's files | ground truth | all 13 TOCs decrypt; structural invariants §5 hold exactly |
| `ef_first_AA.exe` (this game) | key table in `.data` (byte-negated), Blowfish tables in `.rdata` @0x170dd8, Blowfish decrypt @VA 0x435393, key schedule @VA 0x435040, PAZ open @VA 0x52E990, `.paz` string @VA 0x55ffbc | keys match §6 after negation; decrypt function's byte order explains the needed `rev4` transform; ECB loop has no chaining |
| GARbro `ArcFormats/Musica/ArcPAZ.cs` (morkt) | same header/TOC/pipeline semantics; GARbro `Blowfish.cs` reproduces the same word-order quirk | algorithms match |
| FuckGalEngine `Minori/Minori/fuckpaz/main.cpp` + `blowfish.cpp`/`blowfish.h2` (asmodean/Fuyin/Inori) | `PAZHDR`/`PAZENTRY` structs, key table for 23 minori titles, `unobfuscate3`=RC4, `decrypt_mov`, zlib handling | key table values reproduce the exe's negated bytes for the "en steam" entry; extraction matches |

`re_tools/` contains the reference key JSON, the exe key-table hexdump, the
disassembly excerpt of the Blowfish ECB loop, the script-command token dump, the
TOC structural check results, and the extraction verifier.

---

## 12. Unknowns / unverified corners (honest list)

1. `simple_key != 0` path is **inferred** from the engine/reference-tool source
   (XOR of header + TOC + data); every file here has `simple_key == 0`, so it is
   untested. Implement it, but don't be surprised if a future archive needs a twist.
2. Exact semantics of the 32-byte header game-id string (where the engine uses it).
3. Multi-part `.pazA/.pazB` archives: supported by the engine; none exist here, so
   untested (implemented in `paz.py`).
4. The `version >= 2` RC4 "skip rounds" mechanism in the reference tool
   (`skip = (crc32(name) >> 12) & 0xFF`, after `not`-negated key bytes) does **not**
   apply to this game (`version == 1`), so it was not verified.
5. Whether the engine gives r18_cn files precedence by identical name across
   archives (merge order) — inferred from content; not observed at runtime.
6. `.ani` internal structure, script VM semantics, PSD role in the engine — out of
   scope of the container format (see EXTRACTION_REPORT.md).

---

## 13. Porting checklist

1. Read header, decode `toc_len` (8-aligned), read TOC blob, XOR with `simple_key`.
2. Implement/link a standard Blowfish-ECB decrypt; wrap it with `rev4()` on input
   and output (or write the little-endian-word variant directly).
3. Decrypt TOC with the TOC key for the archive prefix; parse §5.
4. For each entry: read `aligned_size` bytes at absolute `offset`; XOR
   `simple_key`; Blowfish-ECB-DATA key; if the archive is `mov` use §8 instead;
   if unpacked and name/pw rule matches, RC4 with §7 seed; truncate to
   `stored_size`; inflate if `is_packed` (truncate `st.paz/*.ani` results by 8).
5. Verify: first entry offset == `0x24 + toc_len`; last end == file size. If not,
   the key set or endianness is wrong.
