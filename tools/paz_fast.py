#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""paz_fast.py - accelerated drop-in PAZ reader for ef - the first tale.

`paz.py` (same directory) is the *reference* implementation: correct, simple,
pure-Python RC4 + per-entry cipher setup. It is the ground truth for
cross-checking. This module keeps exactly the same semantics but replaces the
three hot spots:

  * `_rev4`          pure-Python 4-byte reversal  -> numpy byteswap (~20x)
  * RC4              pure-Python PRGA loop         -> Crypto.Cipher.ARC4 (C)
  * Blowfish setup   new cipher per entry          -> one cipher per archive

It is a **library**, imported by `toc_scan.py` / `extract_assets.py`.
Fallbacks: if numpy or pycryptodome ARC4 are unavailable, the pure-Python
implementation from paz.py is used, so results stay identical (only slower).

Usage:
    import paz_fast
    arc = paz_fast.PazArchivePlus('/path/bg.paz')
    data = arc.read(arc.entries[0])
"""

import os
import sys
import zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import paz  # noqa: E402  (reference implementation, same directory)

try:
    import numpy as _np
except ImportError:  # pragma: no cover
    _np = None

try:
    from Crypto.Cipher import ARC4 as _ARC4
except Exception:  # pragma: no cover
    _ARC4 = None

_HAVE_NUMPY = _np is not None
_HAVE_ARC4 = _ARC4 is not None


def rev4(data):
    """Reverse byte order inside every 4-byte group (engine Blowfish variant)."""
    n = len(data)
    if n % 4:
        raise ValueError('length not a multiple of 4')
    if not _HAVE_NUMPY:
        return paz._rev4(bytes(data))
    return _np.frombuffer(data, dtype='<u4').byteswap().tobytes()


def rc4(data, key):
    """Standard RC4, identical output to paz.rc4()."""
    if not key:
        return bytes(data)
    if _HAVE_ARC4 and 5 <= len(key) <= 256:
        return _ARC4.new(key).decrypt(bytes(data))
    return paz.rc4(data, key)


class PazArchivePlus(paz.PazArchive):
    """Same API as paz.PazArchive, optimized read path."""

    def __init__(self, path, game=None, verbose=False):
        super().__init__(path, game=game, verbose=verbose)
        self._data_key = bytes.fromhex(self._keys[1])
        self._bf = None
        if self.prefix not in paz.VIDEO_PREFIXES:
            from Crypto.Cipher import Blowfish
            self._bf = Blowfish.new(self._data_key, Blowfish.MODE_ECB)

    # -- exact equivalent of paz.PazArchive.read() -------------------------
    def read(self, entry):
        if self.prefix in paz.VIDEO_PREFIXES or self._bf is None:
            return super().read(entry)

        buff = bytearray(self._read_range(entry.offset, entry.aligned_size))
        if self.simple_key:
            buff = bytearray(b ^ self.simple_key for b in buff)
        crud = '' if entry.is_packed else self._crud_for(entry.name)

        dec = rev4(self._bf.decrypt(rev4(bytes(buff))))
        if crud:
            seed = (entry.name.lower() + ' %08X ' % entry.stored_size +
                    crud).encode('ascii')
            dec = rc4(dec, seed)
        data = dec[:entry.stored_size]

        if entry.is_packed:
            try:
                out = zlib.decompressobj().decompress(data)
            except zlib.error as exc:
                raise ValueError('zlib error on %r: %s' % (entry.name, exc))
            if len(out) != entry.raw_size:
                # Known quirk (st.paz, r18_cn/st.paz): packed .ani inflates to
                # raw_size + 8 with 8 trailing NUL bytes; only raw_size counts.
                extra = out[entry.raw_size:]
                if len(out) > entry.raw_size and not extra.strip(b'\0'):
                    self.adjusted_entries.append(entry.name)
                    return out[:entry.raw_size]
                raise ValueError(
                    'size mismatch on %r after inflate: %d != %d' %
                    (entry.name, len(out), entry.raw_size))
            return out

        if len(data) != entry.raw_size:
            raise ValueError('size mismatch on %r: %d != %d' %
                             (entry.name, len(data), entry.raw_size))
        return data


def open_fast(path, game=None, verbose=False):
    return PazArchivePlus(path, game=game, verbose=verbose)


if __name__ == '__main__':
    # Tiny self-test: compare against the reference reader on one entry.
    import hashlib
    import argparse
    ap = argparse.ArgumentParser(description='paz_fast self-test')
    ap.add_argument('archive')
    ap.add_argument('--entry-regex', default=r'\.png$')
    ap.add_argument('--samples', type=int, default=3)
    a = ap.parse_args()
    import re
    rx = re.compile(a.entry_regex)
    fast = PazArchivePlus(a.archive)
    ref = paz.PazArchive(a.archive)
    picked = [e for e in fast.entries if rx.search(e.name)][:a.samples]
    ok = True
    for e in picked:
        fa = fast.read(e)
        ra = ref.read(e)
        same = hashlib.sha256(fa).hexdigest() == hashlib.sha256(ra).hexdigest()
        print('%-40s %8d bytes  identical=%s' % (e.name, len(fa), same))
        ok = ok and same and len(fa) == e.raw_size
    print('self-test:', 'PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)
