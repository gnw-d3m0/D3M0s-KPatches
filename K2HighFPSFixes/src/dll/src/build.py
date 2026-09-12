#!/usr/bin/env python3
"""Rebuild K2HighFPSFixes.kpatch with Python 3.11+, Clang and lld-link.
No game executable is needed to build. --exe optionally validates the original.
Only the unmodified GOG Aspyr SHA-256 below is registered by this package.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import tomllib
import zipfile

TARGET_HASH = "777BEE235A9E8BDD9863F6741BC3AC54BB6A113B62B1D2E4D12BBE6DB963A914"
# The post-combat replacement is the current K1 all-in-one algorithm remapped
# to K2's EBP locals, +0xEC status member and original K2 continuations.
# The half-frame comparisons and LEA before JG are intentional: do not replace
# that LEA with ADD, which would destroy the comparison flags.
SPECS = [{'label': 'Post-combat: preserve initial delay and 60-FPS retry window',
  'address': 4570434,
  'type': 'replace',
  'original': '8B 95 A0 F9 FF FF D9 82 A4 02 00 00 D8 65 08 8B 85 A0 F9 FF FF D9 98 A4 02 00 00 8B 4D BC E8 1B DA FC '
              'FF 89 45 EC C7 45 E8 01 00 00 00 83 7D EC 00 74 0C 8B 4D EC 8B 91 EC 00 00 00 89 55 E8 8B 85 A0 F9 FF '
              'FF D9 80 A4 02 00 00 DC 1D 00 5B 98 00 DF E0 F6 C4 41 0F 8A EE 00 00 00 83 7D E8 00 0F 84 E2 00 00 00',
  'assembly': '\n'
              '    mov eax, dword ptr [ebp-0x660]\n'
              '    mov edx, dword ptr [eax+0x2A4]\n'
              '    fld dword ptr [eax+0x2A4]\n'
              '    fsub dword ptr [ebp+8]\n'
              '    fstp dword ptr [eax+0x2A4]\n'
              '    push edx\n'
              '    mov ecx, dword ptr [ebp-0x44]\n'
              '    mov eax, 0x00429780\n'
              '    call eax\n'
              '    mov dword ptr [ebp-0x14], eax\n'
              '    mov ecx, 1\n'
              '    test eax, eax\n'
              '    jz have_status\n'
              '    mov ecx, dword ptr [eax+0xEC]\n'
              'have_status:\n'
              '    mov dword ptr [ebp-0x18], ecx\n'
              '    pop edx\n'
              '    mov eax, dword ptr [ebp-0x660]\n'
              '    cmp edx, 0x3F800000\n'
              '    jg retry\n'
              '    cmp dword ptr [eax+0x2A4], 0\n'
              '    jg wait\n'
              '    test ecx, ecx\n'
              '    jz bypass\n'
              '    jmp check\n'
              'retry:\n'
              '    sub esp, 8\n'
              '    fld dword ptr [ebp+8]\n'
              '    push 0x3F000000\n'
              '    fmul dword ptr [esp]\n'
              '    lea esp, [esp+4]\n'
              '    fst dword ptr [esp]\n'
              '    push 0x3F800000\n'
              '    fadd dword ptr [esp]\n'
              '    lea esp, [esp+4]\n'
              '    fstp dword ptr [esp+4]\n'
              '    mov edx, dword ptr [eax+0x2A4]\n'
              '    cmp edx, dword ptr [esp+4]\n'
              '    jle expired\n'
              '    fld dword ptr [esp]\n'
              '    push 0x3F8CCCCD\n'
              '    fadd dword ptr [esp]\n'
              '    lea esp, [esp+4]\n'
              '    fstp dword ptr [esp+4]\n'
              '    cmp edx, dword ptr [esp+4]\n'
              '    lea esp, [esp+8]\n'
              '    jg bypass\n'
              '    jmp wait\n'
              'expired:\n'
              '    lea esp, [esp+8]\n'
              '    mov dword ptr [eax+0x2A4], 0\n'
              '    test ecx, ecx\n'
              '    jz bypass\n'
              '    jmp check\n'
              'wait:\n'
              '    push 0x0045BE8C\n'
              '    ret\n'
              'bypass:\n'
              '    push 0x0045BE96\n'
              '    ret\n'
              'check:\n'
              '    push 0x0045BDA8\n'
              '    ret\n'},
 {'label': 'Post-combat: local tagged retry value, not the shared 0.1 constant',
  'address': 4570750,
  'type': 'replace',
  'original': 'D9 05 84 60 98 00',
  'assembly': 'push 0x3F8EEEEF\nfld dword ptr [esp]\nlea esp, [esp+4]'},
 {'label': 'Letterbox: reset on a new opening transition',
  'address': 5960908,
  'type': 'detour',
  'original': 'C7 81 88 00 00 00 01 00 00 00',
  'function': 'ResetLetterbox'},
 {'label': 'Letterbox: reset on a new closing transition',
  'address': 5961297,
  'type': 'detour',
  'original': 'C7 81 88 00 00 00 02 00 00 00',
  'function': 'ResetLetterbox'},
 {'label': 'Letterbox: opening pixel step with persistent fractional carry',
  'address': 5962921,
  'type': 'detour',
  'original': '8B 45 FC 8B 4D FC 8B 50 74 2B 91 84 00 00 00 89 55 F8 DB 45 F8 DC 35 28 61 98 00 8B 45 08 D8 08 E8 C2 '
              '47 37 00',
  'function': 'LetterboxOpeningStep',
  'skip': True},
 {'label': 'Letterbox: closing pixel step with persistent fractional carry',
  'address': 5962985,
  'type': 'detour',
  'original': '8B 45 FC 8B 4D FC 8B 50 6C 2B 51 7C 89 55 F8 DB 45 F8 DC 35 28 61 98 00 8B 45 08 D8 08 E8 85 47 37 00',
  'function': 'LetterboxClosingStep',
  'skip': True},
 {'label': 'Mouse yaw only; keyboard and controller turning are unchanged',
  'address': 4633153,
  'type': 'replace',
  'original': 'D9 45 A0 D8 4D 0C',
  'assembly': 'fld dword ptr [ebp-0x60]\npush 0x3C888889\nfmul dword ptr [esp]\nlea esp, [esp+4]'},
 {'label': 'Mouse pitch only; keyboard and controller smoothing are unchanged',
  'address': 5145832,
  'type': 'replace',
  'original': 'D9 40 2C D8 4D 08',
  'assembly': 'fld dword ptr [eax+0x2C]\npush 0x3C888889\nfmul dword ptr [esp]\nlea esp, [esp+4]'},
 {'label': 'UV scrolling: time-scaled U',
  'address': 9412058,
  'type': 'replace',
  'original': 'D8 81 3C 01 00 00',
  'assembly': 'fld dword ptr [ecx+0x13C]\n'
              'fmul dword ptr [0x009F6C40]\n'
              'push 0x42700000\n'
              'fmul dword ptr [esp]\n'
              'lea esp, [esp+4]\n'
              'faddp st(1), st(0)'},
 {'label': 'UV scrolling: time-scaled V',
  'address': 9412078,
  'type': 'replace',
  'original': 'D8 81 40 01 00 00',
  'assembly': 'fld dword ptr [ecx+0x140]\n'
              'fmul dword ptr [0x009F6C40]\n'
              'push 0x42700000\n'
              'fmul dword ptr [esp]\n'
              'lea esp, [esp+4]\n'
              'faddp st(1), st(0)'},
 {'label': 'UV jitter: gate random evolution without dropping base scrolling',
  'address': 9411983,
  'type': 'detour',
  'original': 'C7 45 FC 00 00 00 00',
  'function': 'GateTextureJitter'},
 {'label': 'Saber trails: remove the 10-ms minimum age delta',
  'address': 9413353,
  'type': 'simple',
  'original': '7A 08',
  'replacement': '90 90'},
 {'label': 'Dangly meshes: remove the 12.5-ms floor, retain the upper clamp',
  'address': 9414264,
  'type': 'simple',
  'original': '75 09',
  'replacement': 'EB 09'},
 {'label': 'Water: virtual frame gate, preserving pending time at threshold crossings',
  'address': 9283647,
  'type': 'detour',
  'original': '8B 85 38 FF FF FF',
  'function': 'GateWaterControllerFrame'},
 {'label': 'Water: supply the matching local virtual delta before native scaling',
  'address': 9283876,
  'type': 'detour',
  'original': 'D9 85 34 FF FF FF',
  'function': 'FixWaterVirtualFrameDelta'}]


def run(args: list[str]) -> None:
    subprocess.run(args, check=True)


def coff_text(path: Path) -> bytes:
    data = path.read_bytes()
    machine, sections = struct.unpack_from("<HH", data)
    if machine != 0x14C:
        raise ValueError("Expected x86 COFF")
    optional = struct.unpack_from("<H", data, 16)[0]
    for index in range(sections):
        pos = 20 + optional + 40 * index
        if data[pos:pos+8].rstrip(b"\0") == b".text":
            size, offset = struct.unpack_from("<II", data, pos+16)
            relocations = struct.unpack_from("<H", data, pos+32)[0]
            if relocations:
                raise ValueError("Inline hook contains unresolved relocations")
            return data[offset:offset+size]
    raise ValueError("Missing .text section")


def assemble(clang: str, assembly: str, tmp: Path, index: int) -> bytes:
    src = tmp / f"hook{index}.S"
    obj = tmp / f"hook{index}.obj"
    src.write_text(".intel_syntax noprefix\n.text\n" + assembly + "\n", encoding="utf-8")
    run([clang, "--target=i686-w64-windows-gnu", "-x", "assembler", "-c", str(src), "-o", str(obj)])
    return coff_text(obj)


def format_bytes(data: bytes) -> str:
    rows = ["    " + ", ".join(f"0x{x:02X}" for x in data[i:i+12]) + ","
            for i in range(0, len(data), 12)]
    return "[\n" + "\n".join(rows) + "\n]"


def verify_exe(path: Path, hooks: list[dict]) -> None:
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest().upper() != TARGET_HASH:
        raise ValueError("EXE is not the supported unmodified GOG Aspyr build")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe+6)[0]
    optional_size = struct.unpack_from("<H", data, pe+20)[0]
    base = struct.unpack_from("<I", data, pe+24+28)[0]
    table = pe+24+optional_size
    for hook in hooks:
        va = hook["address"]
        for i in range(count):
            pos = table+40*i
            rva, size, offset = struct.unpack_from("<III", data, pos+12)
            if base+rva <= va < base+rva+size:
                pos = offset+va-base-rva
                original = bytes.fromhex(hook["original"])
                if data[pos:pos+len(original)] != original:
                    raise ValueError(f"Original-byte mismatch at {va:#x}")
                break
        else:
            raise ValueError(f"Hook outside executable image: {va:#x}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, help="Optional unmodified swkotor2.exe for byte verification")
    parser.add_argument("--clang", default="clang++")
    parser.add_argument("--linker", default="lld-link")
    options = parser.parse_args()
    for command in (options.clang, options.linker):
        if not shutil.which(command):
            parser.error(f"Required build tool not found: {command}")
    src = Path(__file__).resolve().parents[2]
    output = src.parent / "K2HighFPSFixes.kpatch"
    if options.exe:
        verify_exe(options.exe, SPECS)
    with tempfile.TemporaryDirectory(prefix="k2-high-fps-") as temp:
        tmp = Path(temp)
        obj = tmp / "K2HighFPSFixes.obj"
        dll = src / "dll/windows_x86.dll"
        run([options.clang, "--target=i686-w64-windows-gnu", "-std=c++17", "-O2",
             "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-stack-protector",
             "-fno-threadsafe-statics", "-fno-builtin", "-mno-sse", "-mno-sse2", "-mfpmath=387",
             "-c", str(src / "dll/src/K2HighFPSFixes.cpp"), "-o", str(obj)])
        run([options.linker, "/dll", "/noentry", "/machine:x86", "/nodefaultlib", "/lldmingw",
             "/dynamicbase", "/nxcompat", "/opt:ref", "/opt:icf", "/timestamp:0",
             "/out:"+str(dll), "/implib:"+str(tmp / "unused.lib"), str(obj)])
        text = '[metadata]\ntarget_versions = ["' + TARGET_HASH + '"]\n'
        for index, hook in enumerate(SPECS):
            original = bytes.fromhex(hook["original"])
            text += f'\n# {hook["label"]}\n[[hooks]]\naddress = 0x{hook["address"]:08X}\n'
            text += f'type = "{hook["type"]}"\noriginal_bytes = ' + format_bytes(original) + "\n"
            if hook["type"] == "detour":
                text += f'function = "{hook["function"]}"\n'
                text += 'skip_original_bytes = ' + str(hook.get("skip", False)).lower() + "\n"
                text += 'exclude_from_restore = []\n'
            else:
                replacement = (assemble(options.clang, hook["assembly"], tmp, index)
                               if "assembly" in hook else bytes.fromhex(hook["replacement"]))
                if hook["type"] == "simple" and len(original) != len(replacement):
                    raise ValueError("Simple hook must preserve its byte length")
                text += 'replacement_bytes = ' + format_bytes(replacement) + "\n"
        hooks_path = src / "kotor2-gog-aspyr.hooks.toml"
        hooks_path.write_text(text, encoding="utf-8", newline="\n")
        tomllib.loads(text)
        tomllib.loads((src / "manifest.toml").read_text())
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, path in [("manifest.toml", src / "manifest.toml"),
                               ("kotor2-gog-aspyr.hooks.toml", hooks_path),
                               ("binaries/windows_x86.dll", dll)]:
                entry = zipfile.ZipInfo(name, date_time=(2026, 9, 12, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, path.read_bytes())
    print(f"Built {output} ({len(SPECS)} hooks)")
    print("SHA-256:", hashlib.sha256(output.read_bytes()).hexdigest())

if __name__ == "__main__":
    main()
