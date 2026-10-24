#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""paz.py - lister/extractor for minori "PAZ" resource archives.

Target: "ef - the first tale" (minori engine, English Steam build).
Reverse-engineered and verified against the real game files; full spec with
evidence is in PAZ_FORMAT.md next to this script.

Quick usage
-----------
    python3 paz.py list ARCHIVE.paz                  # list entries
    python3 paz.py extract ARCHIVE.paz OUTDIR        # extract everything
    python3 paz.py extract ARCHIVE.paz OUTDIR --filter '\.sc$' --limit 5
    python3 paz.py list-all                          # all archives in the game dir
    python3 paz.py list-all --root /path/to/game     # or from another root

Dependencies: pycryptodome  (pip install pycryptodome)

Format summary (details in PAZ_FORMAT.md)
-----------------------------------------
    +0x00  32-byte NUL-terminated game id string, e.g. b"ef_first_en_AA"
    +0x20  u32  toc_len_obf      simple_key = toc_len_obf >> 24
                                 toc_len    = toc_len_obf ^ (simple_key*0x01010101)
    +0x24  toc_len bytes: TOC, every byte XOR simple_key, Blowfish-ECB encrypted
           TOC plaintext: u32 entry_count [mov only: 256-byte seed]
                          entry_count * { cstring name, u64 offset, u32 raw_size,
                                         u32 stored_size, u32 aligned_size, u32 is_packed }
    entry data at entry.offset, aligned to 8: XOR simple_key, then Blowfish-ECB
    with the archive data key (except mov, which uses a 256-byte RC4 scheme),
    then, for uncompressed entries whose name matches .png/.ogg/.sc/.avi/.mpg,
    RC4 with key = "<lowercase name> <stored_size:08X> <type_password>";
    finally zlib-inflate when is_packed.

Game Blowfish note: the engine uses the standard Blowfish tables/rounds but
loads the 64-bit data block as *little-endian* 32-bit words (the reference
implementation's big-endian convention with each 4-byte word byte-reversed),
while the key words are assembled big-endian.  See _bf_decrypt().
"""
import argparse
import os
import re
import struct
import sys
import zlib

try:
    from Crypto.Cipher import Blowfish
except ImportError:  # pragma: no cover
    sys.exit("error: pycryptodome is required (pip install pycryptodome)")

# ---------------------------------------------------------------------------
# Keys for the three "ef - the first tale" variants known from the engine key
# table (reverse-engineered from the game's own executable; see PAZ_FORMAT.md).
#   'keys' -> archive prefix -> (toc_key_hex, data_key_hex)   32-byte Blowfish keys
#   'crud' -> (png, ogg, sc, avi) per-entry RC4 password strings
# ---------------------------------------------------------------------------
GAMES = {
    'ef - the first tale (en steam)': {
        'version': 1,
        'crud': ('a7b77qgx', '52AUwF8B', '102ycz1s', 'qmzj4Y9C'),
        'keys': {
            'bg': ('8c7919517e51a0fd8348dc85cb68858e68905bd7d5b96336ea3699c15b683f83', 'ca3c7817e1c631e5f88de7aff702a3697cb9d5e337e54eda45546c2910c75fde'),
            'bgm': ('78e52f7768fa49ea1b0ef53f7b38014a835bc8c948eb78d0ab2688b85d46f307', '8b7731e7aa9dae4c1f216750c32b9d395794b257c353a3c9dfa880733026d32b'),
            'mov': ('5a4caf2034cc9510780ce2b9bae97b9c2026274e7fdd36c58d10325f0337e1ea', '5a4caf2034cc9510780ce2b9bae97b9c2026274e7fdd36c58d10325f0337e1ea'),
            'scr': ('72634e755d2c980b205f82828bde4379f37bd2b66d4390b6436a4d9f8389ca5a', 'b7d2955cf7a0d3ecc20ccbf887a8c1b6d498ecf5f9b8361e897e4dbc7bad1465'),
            'st': ('b891bb7b469812fe98cc267bf1356131c694d340d79d8cd10db45c5d81c44a9c', '630bdf10c9e57ef99126b50476f59800240cd1b3766a1416e31ebfee27694f93'),
            'sys': ('0112c9880d9dd23a0573bac209226d0e80a8ae1ef8b33a76494962531bd1110b', 'fd399525e2b2762ff9bc115dd145f6eb9e6de3574574d04bcd21146be6537951'),
            'voice': ('2fd5d774760b34126f7c3e964fe6ca4cfcdf6a730b891cb1618335270feb79df', '151694fdcf2ce124a14e6d1e46e1a7b17395c0cc172da47ca3ad8cae42043258'),
            'se': ('c3011a3ed51fcd2bd3904dd1772e469709b10d5dc0cb4652cff5b3a57cde9038', '3dca17d9518de518087077409c8bcb396a6be53da76e650238e09905096d1aa8'),
            '': ('0000000000000000000000000000000000000000000000000000000000000000', '0000000000000000000000000000000000000000000000000000000000000000'),
        },
    },
    'ef - the first tale (en)': {
        'version': 1,
        'crud': ('8xvC4k7J', '0tR6yq5P', 'B3b09urx', 'n1hJKx3X'),
        'keys': {
            'bg': ('9e66c3f93640a4d62bbe5d8155f462a8b31d9091f4216d666bd5ab306095416a', '7be5b44f64f5647730a433a3d15c99a29185509cfe2f8f443e00fbed8efabc88'),
            'bgm': ('04216a65cc9d74bd66e4c23410c0723ce3807e4ea029f96ab9e15198cf30ae89', 'd0a6761fae8fb75941e2224b970038d2011babd5be23711ce455093cfd85a16c'),
            'mov': ('7514c91d9c2f32dc7bccd46b08d78362f413c6fff8ff5109eb1c7807cf499198', '17d485d8f912f26241967479e46f797852fa5d83fef617e596be2366b18aacbf'),
            'scr': ('38e80bb028a4f744d45487d07ff8b4034604c22006b15fb9aec03c200c70c7ed', 'dc2347c20d1e4993f71f261dfb98cd7c609c266dc0b79ca972156a4a3e5d5100'),
            'st': ('cb1bd4de643f4280a08d07e0ecceb287eee7b63071da88ebf3017e72f8ea5459', '8db161246a8d81cfbfcba00d8095ccb3f4287d7dffc0701a37b62cd3a98d5fff'),
            'sys': ('acf425d30dd06b68436368b1840a65783ba8b8e1c318a60c393ce71f8c0233d7', '3762140ab76298f1bc493587bdf49568d37039eb7cb3ba2030f7461da9606ae5'),
            'voice': ('9fe3341ab541615ab65a99df8b34a8fa3d164f797639408f7ede32369f95d006', '284566be27ed59e5b55b766967137ebb9c1226bfb9cded3ee9186711cd30ded3'),
            'se': ('01062403c267ec8261f0debdd80cea54d08e781af46ca54d735eafeb0f00a3ab', 'd2da31ca4c493c3408cb7dab1dc4947e4e39b961fe753cfd2e03fe4099df29aa'),
            '': ('0000000000000000000000000000000000000000000000000000000000000000', '0000000000000000000000000000000000000000000000000000000000000000'),
        },
    },
    'ef - the first tale (jp)': {
        'version': 1,
        'crud': ('', '', '', ''),
        'keys': {
            'bg': ('7884ada5b51200bf569a68f83d54faea4becd33f70cf8272ce7416fda364dbd4', 'bb5f930e98b44f7858bf62fd0aec99eb28bf323693aad3bb1f12d205f39f3ebc'),
            'bgm': ('9b00bbce7c48a1c9760cb1da2eb6a160917d4aaf3b14edbd158252346df600df', '06f0c336bdd9805453ec9361bae7bd53d04b63684d1e7230e6eaa11819fe28a7'),
            'mov': ('188a8b2fbb321a7e5a25abae7f8a34d44309e0b26a388fabf8ad641566dcad61', '0000000000000000000000000000000000000000000000000000000000000000'),
            'scr': ('81c83415a210179668bb42a6d56dee584cef73db1267367d3a2b65a7596599ae', '47ffb489bddab8f51938fc8af36a8a453b5142b47cecc8130be36d9cd9a3e3ce'),
            'st': ('fde91cbc754750d032a7eba17fdc36ccbae7df9634a8fe0bf50c94eba663f393', 'd14c074b3156164aec494ff51635b95a2daf4baa08993a7d5a050297981a5bfa'),
            'sys': ('7e403b26c90e587ebd2c3d2e83370a7b3c6ae77b71827cd60f04f898bcb09d59', '01c3fe52254c3af0850ab7a6dcd08c4808985dfad3809b10c234c2feccb93e7c'),
            'voice': ('4fd62bb631638214d452e7342fc657ff79ab21e51c627ae865335e0bb562e2e4', 'cc52d4e748cab0655efddac4934cee7bbf32603ef3e553f58044d6b9f05b6053'),
            'se': ('a17e474962c04d7179d7af99bf0ba32b21a755f1efd9c658526172cfd89aeb07', ''),
        },
    },
}
DEFAULT_GAME = 'ef - the first tale (en steam)'
GAME_CHOICES = tuple(GAMES)
AUDIO_PREFIXES = {'bgm', 'se', 'voice'}
VIDEO_PREFIXES = {'mov'}
KNOWN_ARCHIVES = ('bg', 'bgm', 'mov', 'scr', 'se', 'st', 'sys', 'voice')


# ---------------------------------------------------------------------------
# Crypto primitives
# ---------------------------------------------------------------------------
def _rev4(data):
    """Reverse byte order inside every 4-byte group (= little-endian words)."""
    n = len(data)
    if n % 4:
        raise ValueError('length not a multiple of 4')
    out = bytearray(n)
    for i in range(0, n, 4):
        out[i:i + 4] = data[i:i + 4][::-1]
    return bytes(out)


def _bf_decrypt(key, data):
    """Decrypt with the engine's Blowfish variant (LE data words, ECB)."""
    if len(data) % 8:
        raise ValueError('Blowfish data length not a multiple of 8')
    cipher = Blowfish.new(key, Blowfish.MODE_ECB)
    return _rev4(cipher.decrypt(_rev4(data)))


def rc4(data, key):
    """Standard RC4 (the engine's 'unobfuscate3')."""
    if not key:
        return bytes(data)
    s = list(range(256))
    j = 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) & 0xFF
        s[i], s[j] = s[j], s[i]
    out = bytearray(len(data))
    i = j = 0
    for k, b in enumerate(data):
        i = (i + 1) & 0xFF
        j = (j + s[i]) & 0xFF
        s[i], s[j] = s[j], s[i]
        out[k] = b ^ s[(s[i] + s[j]) & 0xFF]
    return bytes(out)


def rc4_keystream_block(key, size):
    """RC4 keystream of `size` bytes (RC4 applied to zeroes)."""
    return rc4(b'\0' * size, key)


# ---------------------------------------------------------------------------
# Archive model
# ---------------------------------------------------------------------------
def decode_name(raw):
    """Entry names are CP932 (JIS) in the original engine; fall back to GBK for
    fan-translation builds."""
    try:
        return raw.decode('cp932')
    except UnicodeDecodeError:
        try:
            return raw.decode('gbk')
        except UnicodeDecodeError:
            return raw.decode('cp932', 'replace')


class PazEntry(object):
    __slots__ = ('name', 'offset', 'raw_size', 'stored_size', 'aligned_size',
                 'is_packed')

    def __init__(self, name, offset, raw_size, stored_size, aligned_size,
                 is_packed):
        self.name = name
        self.offset = offset
        self.raw_size = raw_size
        self.stored_size = stored_size
        self.aligned_size = aligned_size
        self.is_packed = is_packed

    def __repr__(self):
        return '<PazEntry %r off=%d raw=%d stored=%d aligned=%d packed=%d>' % (
            self.name, self.offset, self.raw_size, self.stored_size,
            self.aligned_size, self.is_packed)


class PazArchive(object):
    """One .paz archive (optionally followed by .pazA, .pazB, ... part files)."""

    def __init__(self, path, game=None, verbose=False):
        self.path = os.path.abspath(path)
        base = os.path.basename(self.path)
        if not base.lower().endswith('.paz'):
            raise ValueError('not a .paz file: %s' % self.path)
        self.prefix = base[:-4].lower()
        self.entries = []
        self.adjusted_entries = []
        self.mov_seed = None
        self.game = None
        self._keys = None
        self._parts = [open(self.path, 'rb')]
        i = 0
        while True:  # optional split parts: bg.pazA, bg.pazB, ...
            part = self.path + chr(ord('A') + i)
            if not os.path.isfile(part):
                break
            self._parts.append(open(part, 'rb'))
            i += 1
        self.part_sizes = [os.fstat(f.fileno()).st_size for f in self._parts]
        self.total_size = sum(self.part_sizes)
        try:
            self._read_header()
            self._load(game, verbose)
        except Exception:
            self.close()
            raise

    # -- low level ---------------------------------------------------------
    def close(self):
        for f in getattr(self, '_parts', ()):
            try:
                f.close()
            except Exception:
                pass

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def _read_range(self, offset, size):
        """Read absolute bytes spanning the archive and its .pazA.. part files."""
        if offset < 0 or size < 0 or offset + size > self.total_size:
            raise ValueError('range 0x%x+0x%x outside archive (size 0x%x)' %
                             (offset, size, self.total_size))
        out = bytearray()
        pos = 0
        for f, fsize in zip(self._parts, self.part_sizes):
            if size == 0:
                break
            if offset >= pos + fsize:
                pos += fsize
                continue
            local = offset - pos if offset > pos else 0
            if local >= fsize:
                pos += fsize
                continue
            n = min(fsize - local, size)
            f.seek(local)
            chunk = f.read(n)
            if len(chunk) != n:
                raise IOError('short read in %s' % f.name)
            out += chunk
            size -= n
            offset += n
            pos += fsize
        return bytes(out)

    # -- header / TOC ------------------------------------------------------
    def _read_header(self):
        hdr = self._read_range(0, 0x24)
        self.raw_id = hdr[:32].split(b'\0')[0]
        self.game_id = self.raw_id.decode('ascii', 'replace')
        v = struct.unpack('<I', hdr[0x20:0x24])[0]
        self.simple_key = v >> 24
        self.toc_len = v ^ (self.simple_key * 0x01010101) if self.simple_key else v
        if self.toc_len <= 0 or self.toc_len % 8:
            raise ValueError('bad TOC length 0x%x' % self.toc_len)
        if 0x24 + self.toc_len > self.total_size:
            raise ValueError('TOC length 0x%x exceeds file size 0x%x' %
                             (self.toc_len, self.total_size))
        self.toc_offset = 0x24
        self.data_start = 0x24 + self.toc_len

    def _parse_toc(self, dec):
        """Parse plaintext TOC; return list of entries or None if implausible."""
        if len(dec) < 4:
            return None
        count = struct.unpack('<I', dec[:4])[0]
        if not 0 < count <= 200000:
            return None
        p = 4
        seed = None
        if self.prefix in VIDEO_PREFIXES:
            if len(dec) < p + 256:
                return None
            seed = dec[p:p + 256]
            p += 256
        entries = []
        prev_end = self.data_start - 1
        for _ in range(count):
            z = dec.find(b'\0', p)
            if z < 0 or z == p or z - p > 512:
                return None
            raw = dec[p:z]
            p = z + 1
            if p + 24 > len(dec):
                return None
            offset, raw_size, stored, aligned, packed = struct.unpack(
                '<QIIII', dec[p:p + 24])
            p += 24
            if aligned == 0 or aligned % 8 or stored > aligned:
                return None
            if raw_size > 0x20000000 or stored > 0x20000000:
                return None
            if packed not in (0, 1):
                return None
            if offset < self.data_start or offset + aligned > self.total_size:
                return None
            entries.append(PazEntry(decode_name(raw), offset, raw_size, stored,
                                    aligned, packed))
        # the TOC is zero padded to an 8-byte boundary; the implicit padding
        # must fit inside toc_len
        if p > self.toc_len:
            return None
        return entries, seed

    def _load(self, game, verbose):
        self._parts[0].seek(self.toc_offset)
        blob = bytearray(self._read_range(self.toc_offset, self.toc_len))
        if self.simple_key:
            blob = bytearray(b ^ self.simple_key for b in blob)
        blob = bytes(blob)
        candidates = [game] if game else list(GAMES)
        last_error = None
        for g in candidates:
            keys = GAMES[g]['keys'].get(self.prefix)
            if not keys:
                continue
            try:
                dec = _bf_decrypt(bytes.fromhex(keys[0]), blob)
            except Exception as exc:  # pragma: no cover
                last_error = exc
                continue
            parsed = self._parse_toc(dec)
            if parsed is not None:
                self.game = g
                self._keys = keys
                self.entries, self.mov_seed = parsed
                self.data_start_observed = min(
                    (e.offset for e in self.entries), default=self.data_start)
                if verbose:
                    print('# game=%r entries=%d simple_key=%#x' %
                          (g, len(self.entries), self.simple_key),
                          file=sys.stderr)
                return
            last_error = 'TOC decryption with %r keys failed validation' % g
        raise ValueError('cannot decrypt TOC of %s (%s)' %
                         (self.path, last_error))

    # -- entries -----------------------------------------------------------
    def _crud_for(self, name):
        crud = GAMES[self.game]['crud']
        low = name.lower()
        if '.png' in low:
            return crud[0]
        if '.ogg' in low or self.prefix in AUDIO_PREFIXES:
            return crud[1]
        if '.sc' in low:
            return crud[2]
        if '.avi' in low or '.mpg' in low:
            return crud[3]
        return ''

    def read(self, entry):
        """Return the fully decrypted and decompressed bytes of one entry."""
        buff = bytearray(self._read_range(entry.offset, entry.aligned_size))
        if self.simple_key:
            buff = bytearray(b ^ self.simple_key for b in buff)
        crud = '' if entry.is_packed else self._crud_for(entry.name)
        if self.prefix in VIDEO_PREFIXES:
            if self.mov_seed is None:
                raise ValueError('missing MOV seed table')
            seed = (entry.name.lower() + ' %08X ' % entry.stored_size +
                    crud).encode('ascii')
            rc4_key = bytes(self.mov_seed[i] ^ seed[i % len(seed)]
                            for i in range(256))
            block = rc4_keystream_block(rc4_key, 0x10000)
            for i in range(len(buff)):
                buff[i] ^= block[i % 0x10000]
        else:
            dec = _bf_decrypt(bytes.fromhex(self._keys[1]), bytes(buff))
            if crud:
                seed = (entry.name.lower() + ' %08X ' % entry.stored_size +
                        crud).encode('ascii')
                dec = rc4(dec, seed)
            buff = bytearray(dec)
        data = bytes(buff[:entry.stored_size])
        if entry.is_packed:
            try:
                out = zlib.decompressobj().decompress(data)
            except zlib.error as exc:
                raise ValueError('zlib error on %r: %s' % (entry.name, exc))
            if len(out) != entry.raw_size:
                # Known quirk (st.paz and r18_cn/st.paz only): every packed .ani
                # inflates to raw_size + 8 bytes with 8 trailing NUL bytes; the
                # engine and the original packer read only raw_size bytes, so the
                # trailing NULs are not part of the logical file.  Drop them and
                # record the adjustment.
                extra = out[entry.raw_size:]
                if len(out) > entry.raw_size and not extra.strip(b'\0'):
                    self.adjusted_entries.append(entry.name)
                    return out[:entry.raw_size]
                raise ValueError('size mismatch on %r after inflate: %d != %d' %
                                 (entry.name, len(out), entry.raw_size))
            return out
        if len(data) != entry.raw_size:
            raise ValueError('size mismatch on %r: %d != %d' %
                             (entry.name, len(data), entry.raw_size))
        return data


# ---------------------------------------------------------------------------
# CLI commands
# ---------------------------------------------------------------------------
def _print_archive_summary(arc):
    packed = sum(1 for e in arc.entries if e.is_packed)
    print('# %s' % arc.path)
    print('#   game_id=%r  game=%r  prefix=%r' %
          (arc.game_id, arc.game, arc.prefix))
    print('#   file_size=%d  toc_len=%d (%#x)  simple_key=%#x  data_start=%#x'
          % (arc.total_size, arc.toc_len, arc.toc_len, arc.simple_key,
             arc.data_start))
    print('#   entries=%d  packed=%d  unpacked=%d  parts=%d' %
          (len(arc.entries), packed, len(arc.entries) - packed, len(arc._parts)))


def _iter_selected(arc, pattern, limit):
    rx = re.compile(pattern) if pattern else None
    n = 0
    for e in arc.entries:
        if rx is not None and not rx.search(e.name):
            continue
        yield e
        n += 1
        if limit is not None and n >= limit:
            break


def cmd_list(args):
    with PazArchive(args.archive, args.game) as arc:
        _print_archive_summary(arc)
        if args.stats:
            raw = sum(e.raw_size for e in arc.entries)
            stored = sum(e.aligned_size for e in arc.entries)
            print('#   total raw bytes=%d  total stored/aligned bytes=%d'
                  % (raw, stored))
        for e in _iter_selected(arc, args.filter, args.limit):
            print('%12d %10d %10d %s %s' % (
                e.offset, e.stored_size, e.raw_size,
                'P' if e.is_packed else '-', e.name))
    return 0


def _safe_relpath(name):
    rel = name.replace('\\', '/')
    rel = rel.lstrip('/')
    norm = os.path.normpath(rel)
    if norm.startswith('..') or os.path.isabs(norm):
        return None
    return norm


def cmd_extract(args):
    os.makedirs(args.outdir, exist_ok=True)
    ok = fail = 0
    with PazArchive(args.archive, args.game) as arc:
        _print_archive_summary(arc)
        for e in _iter_selected(arc, args.filter, args.limit):
            rel = _safe_relpath(e.name)
            if rel is None:
                print('warning: unsafe entry name %r skipped' % e.name,
                      file=sys.stderr)
                fail += 1
                continue
            dest = os.path.join(args.outdir, rel)
            parent = os.path.dirname(dest)
            if parent:
                os.makedirs(parent, exist_ok=True)
            try:
                data = arc.read(e)
            except Exception as exc:
                print('error: %r: %s' % (e.name, exc), file=sys.stderr)
                fail += 1
                continue
            with open(dest, 'wb') as f:
                f.write(data)
            ok += 1
            if args.verbose:
                print('%d\t%s' % (len(data), rel))
    print('extracted %d entries (%d failed) to %s' % (ok, fail, args.outdir))
    return 0 if fail == 0 else 1


def cmd_list_all(args):
    roots = []
    root = os.path.abspath(args.root or os.getcwd())
    roots.append(('', root))
    r18 = os.path.join(root, 'r18_cn')
    if os.path.isdir(r18):
        roots.append(('r18_cn/', r18))
    total_files = total_entries = total_bytes = 0
    failures = []
    print('%-8s %-26s %8s %8s %14s %14s  %s' % (
        'archive', 'game', 'entries', 'packed', 'raw bytes', 'file bytes',
        'game_id'))
    for tag, base in roots:
        for name in KNOWN_ARCHIVES:
            path = os.path.join(base, name + '.paz')
            if not os.path.isfile(path):
                continue
            try:
                arc = PazArchive(path)
                try:
                    packed = sum(1 for e in arc.entries if e.is_packed)
                    raw = sum(e.raw_size for e in arc.entries)
                    print('%-8s %-26s %8d %8d %14d %14d  %s' % (
                        tag + name, arc.game, len(arc.entries), packed, raw,
                        arc.total_size, arc.game_id))
                    total_files += 1
                    total_entries += len(arc.entries)
                    total_bytes += raw
                finally:
                    arc.close()
            except Exception as exc:
                failures.append((tag + name, str(exc)))
    print('-' * 100)
    print('TOTAL: %d archives, %d entries, %d raw bytes' %
          (total_files, total_entries, total_bytes))
    if failures:
        print('FAILED archives:')
        for name, err in failures:
            print('  %s: %s' % (name, err))
        return 1
    return 0


def build_parser():
    p = argparse.ArgumentParser(
        description='List/extract minori PAZ archives (ef - the first tale).',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    sub = p.add_subparsers(dest='command')

    pl = sub.add_parser('list', help='list archive entries')
    pl.add_argument('archive')
    pl.add_argument('--game', choices=GAME_CHOICES, default=None,
                    help='force a key set instead of auto-detecting')
    pl.add_argument('--filter', default=None, metavar='REGEX',
                    help='only entries whose name matches REGEX')
    pl.add_argument('--limit', type=int, default=None)
    pl.add_argument('--stats', action='store_true')
    pl.set_defaults(func=cmd_list)

    pe = sub.add_parser('extract', help='extract archive entries')
    pe.add_argument('archive')
    pe.add_argument('outdir')
    pe.add_argument('--game', choices=GAME_CHOICES, default=None)
    pe.add_argument('--filter', default=None, metavar='REGEX')
    pe.add_argument('--limit', type=int, default=None)
    pe.add_argument('-v', '--verbose', action='store_true')
    pe.set_defaults(func=cmd_extract)

    pa = sub.add_parser('list-all', help='list every .paz under the game dir')
    pa.add_argument('--root', default=None,
                    help='game directory (default: current directory)')
    pa.set_defaults(func=cmd_list_all)

    return p


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if not getattr(args, 'command', None):
        parser.print_help()
        return 2
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
