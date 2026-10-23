"""Read-only PE32+ inventory using only the standard library.

Opens the target file read-only, never writes to it, and emits a JSON report
(headers, sections + entropy, imports, delay imports, exports, debug/PDB info,
TLS callbacks, mitigation flags). Paths in the output are sanitized by the caller.

Usage: python -I pe_inventory.py <path-to-exe> [--out report.json]
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

MACHINES = {0x8664: "AMD64", 0x14C: "I386", 0xAA64: "ARM64"}
SUBSYSTEMS = {2: "WINDOWS_GUI", 3: "WINDOWS_CUI"}
DLLCHAR = {
    0x0020: "HIGH_ENTROPY_VA", 0x0040: "DYNAMIC_BASE", 0x0080: "FORCE_INTEGRITY",
    0x0100: "NX_COMPAT", 0x0200: "NO_ISOLATION", 0x0400: "NO_SEH", 0x0800: "NO_BIND",
    0x1000: "APPCONTAINER", 0x2000: "WDM_DRIVER", 0x4000: "GUARD_CF", 0x8000: "TERMINAL_SERVER_AWARE",
}
SCN = {0x20: "CODE", 0x40: "IDATA", 0x80: "UDATA", 0x20000000: "EXEC", 0x40000000: "READ", 0x80000000: "WRITE"}
DIR_NAMES = ["EXPORT", "IMPORT", "RESOURCE", "EXCEPTION", "SECURITY", "BASERELOC", "DEBUG", "ARCHITECTURE",
             "GLOBALPTR", "TLS", "LOAD_CONFIG", "BOUND_IMPORT", "IAT", "DELAY_IMPORT", "CLR", "RESERVED"]


class PEError(ValueError):
    pass


def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    n = len(data)
    return -sum(c / n * math.log2(c / n) for c in counts if c)


class PE:
    def __init__(self, data: bytes):
        self.d = data
        if data[:2] != b"MZ":
            raise PEError("missing MZ signature")
        self.pe_off = struct.unpack_from("<I", data, 0x3C)[0]
        if data[self.pe_off:self.pe_off + 4] != b"PE\0\0":
            raise PEError("missing PE signature")
        coff = self.pe_off + 4
        (self.machine, self.nsections, self.timestamp, _, _, self.opt_size,
         self.characteristics) = struct.unpack_from("<HHIIIHH", data, coff)
        self.opt = coff + 20
        self.magic = struct.unpack_from("<H", data, self.opt)[0]
        if self.magic != 0x20B:
            raise PEError(f"not PE32+ (magic 0x{self.magic:x})")
        self.entry = struct.unpack_from("<I", data, self.opt + 16)[0]
        self.image_base = struct.unpack_from("<Q", data, self.opt + 24)[0]
        self.size_of_image = struct.unpack_from("<I", data, self.opt + 56)[0]
        self.checksum = struct.unpack_from("<I", data, self.opt + 64)[0]
        self.subsystem, self.dllchar = struct.unpack_from("<HH", data, self.opt + 68)
        ndirs = struct.unpack_from("<I", data, self.opt + 108)[0]
        self.dirs = [struct.unpack_from("<II", data, self.opt + 112 + 8 * i) for i in range(min(ndirs, 16))]
        sec = self.opt + self.opt_size
        self.sections = []
        for i in range(self.nsections):
            o = sec + 40 * i
            name = data[o:o + 8].rstrip(b"\0").decode("latin-1")
            vsize, va, rsize, rptr = struct.unpack_from("<IIII", data, o + 8)
            ch = struct.unpack_from("<I", data, o + 36)[0]
            self.sections.append((name, vsize, va, rsize, rptr, ch))

    def rva2off(self, rva: int) -> int | None:
        for _, vsize, va, rsize, rptr, _ in self.sections:
            if va <= rva < va + max(vsize, rsize):
                off = rva - va + rptr
                return off if off < len(self.d) else None
        return None

    def cstr(self, rva: int, limit: int = 512) -> str:
        off = self.rva2off(rva)
        if off is None:
            return "<unmapped>"
        end = self.d.find(b"\0", off, off + limit)
        return self.d[off:end if end != -1 else off + limit].decode("latin-1")

    def _thunks(self, rva: int) -> list[str]:
        names, off = [], self.rva2off(rva)
        while off is not None and off + 8 <= len(self.d):
            v = struct.unpack_from("<Q", self.d, off)[0]
            if v == 0:
                break
            names.append(f"#ord{v & 0xFFFF}" if v & (1 << 63) else self.cstr((v & 0x7FFFFFFF) + 2))
            off += 8
            if len(names) > 20000:
                break
        return names

    def imports(self) -> list[dict]:
        rva, size = self.dirs[1]
        out, off = [], self.rva2off(rva) if rva else None
        while off is not None:
            oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", self.d, off)
            if name_rva == 0:
                break
            funcs = self._thunks(oft or ft)
            out.append({"dll": self.cstr(name_rva), "count": len(funcs), "functions": sorted(funcs)})
            off += 20
        return out

    def delay_imports(self) -> list[dict]:
        if len(self.dirs) <= 13 or not self.dirs[13][0]:
            return []
        out, off = [], self.rva2off(self.dirs[13][0])
        while off is not None:
            attrs, name_rva, _, _, int_rva = struct.unpack_from("<IIIII", self.d, off)
            if name_rva == 0:
                break
            funcs = self._thunks(int_rva)
            out.append({"dll": self.cstr(name_rva), "count": len(funcs), "functions": sorted(funcs)})
            off += 32
        return out

    def exports(self) -> dict:
        rva, _ = self.dirs[0]
        if not rva:
            return {"count": 0, "names": []}
        off = self.rva2off(rva)
        name_rva, _, nfuncs, nnames, _, names_rva, _ = struct.unpack_from("<IIIIIII", self.d, off + 12)
        noff = self.rva2off(names_rva)
        names = [self.cstr(struct.unpack_from("<I", self.d, noff + 4 * i)[0]) for i in range(nnames)] if noff else []
        return {"dll_name": self.cstr(name_rva), "count": nfuncs, "named": nnames, "names": names[:200]}

    def debug(self) -> list[dict]:
        rva, size = self.dirs[6]
        off = self.rva2off(rva) if rva else None
        out = []
        for i in range((size // 28) if off is not None else 0):
            _, ts, _, _, typ, dsize, _, ptr = struct.unpack_from("<IIHHIIII", self.d, off + 28 * i)
            entry = {"type": typ, "timestamp": ts, "size": dsize}
            if typ == 2 and self.d[ptr:ptr + 4] == b"RSDS":
                guid = self.d[ptr + 4:ptr + 20].hex()
                age = struct.unpack_from("<I", self.d, ptr + 20)[0]
                end = self.d.find(b"\0", ptr + 24, ptr + 24 + 512)
                entry.update(codeview="RSDS", pdb_guid=guid, pdb_age=age,
                             pdb_path=self.d[ptr + 24:end].decode("latin-1"))
            out.append(entry)
        return out

    def tls(self) -> dict:
        rva, _ = self.dirs[9]
        if not rva:
            return {"present": False}
        off = self.rva2off(rva)
        cb_va = struct.unpack_from("<Q", self.d, off + 24)[0]
        callbacks, coff = 0, self.rva2off(cb_va - self.image_base) if cb_va else None
        while coff is not None and struct.unpack_from("<Q", self.d, coff + 8 * callbacks)[0]:
            callbacks += 1
            if callbacks > 64:
                break
        return {"present": True, "callback_count": callbacks}


def inventory(path: Path) -> dict:
    data = path.read_bytes()  # read-only access
    pe = PE(data)
    secs = []
    for name, vsize, va, rsize, rptr, ch in pe.sections:
        secs.append({
            "name": name, "virtual_size": vsize, "virtual_address": f"0x{va:x}", "raw_size": rsize,
            "flags": [v for k, v in SCN.items() if ch & k],
            "entropy": round(entropy(data[rptr:rptr + rsize]), 3),
        })
    imps = pe.imports()
    return {
        "format": "PE32+",
        "machine": MACHINES.get(pe.machine, f"0x{pe.machine:x}"),
        "coff_timestamp": pe.timestamp,
        "image_base": f"0x{pe.image_base:x}",
        "entry_point_rva": f"0x{pe.entry:x}",
        "entry_point_section": next((s[0] for s in pe.sections if s[2] <= pe.entry < s[2] + max(s[1], s[3])), None),
        "size_of_image": pe.size_of_image,
        "header_checksum": f"0x{pe.checksum:x}",
        "subsystem": SUBSYSTEMS.get(pe.subsystem, pe.subsystem),
        "dll_characteristics": [v for k, v in DLLCHAR.items() if pe.dllchar & k],
        "data_directories": {DIR_NAMES[i]: {"rva": f"0x{r:x}", "size": s} for i, (r, s) in enumerate(pe.dirs) if r},
        "sections": secs,
        "imports": imps,
        "import_dll_count": len(imps),
        "import_function_count": sum(i["count"] for i in imps),
        "delay_imports": pe.delay_imports(),
        "exports": pe.exports(),
        "debug": pe.debug(),
        "tls": pe.tls(),
        "authenticode_present": bool(pe.dirs[4][0]),
    }


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("exe", type=Path)
    ap.add_argument("--out", type=Path)
    a = ap.parse_args(argv)
    try:
        rep = inventory(a.exe)
    except (OSError, PEError, struct.error) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    text = json.dumps(rep, indent=2)
    if a.out:
        a.out.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
