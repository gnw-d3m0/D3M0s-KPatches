import argparse
import os
import re
import shutil
import struct
import subprocess
import tempfile
import tomllib
import zipfile
from pathlib import Path


def dll_exports(data: bytes) -> set[str]:
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[:2] != b'MZ' or data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Invalid DLL')
    machine, sections = struct.unpack_from('<HH', data, pe + 4)
    optional_size, characteristics = struct.unpack_from('<HH', data, pe + 20)
    optional = pe + 24
    if machine != 0x14C or not characteristics & 0x2000 or struct.unpack_from('<H', data, optional)[0] != 0x10B:
        raise ValueError('Expected a Windows x86 DLL')
    if struct.unpack_from('<I', data, optional + 16)[0] != 0:
        raise ValueError('Unexpected DLL entry point')
    if struct.unpack_from('<II', data, optional + 104) != (0, 0):
        raise ValueError('Unexpected DLL imports')
    if struct.unpack_from('<II', data, optional + 136) == (0, 0):
        raise ValueError('Missing base relocations')
    table = optional + optional_size

    def offset(rva: int) -> int:
        for index in range(sections):
            virtual_size, address, raw_size, raw = struct.unpack_from('<IIII', data, table + index * 40 + 8)
            if address <= rva < address + max(virtual_size, raw_size):
                return raw + rva - address
        raise ValueError(f'Unmapped DLL RVA: {rva:#x}')

    export_rva = struct.unpack_from('<I', data, optional + 96)[0]
    directory = offset(export_rva)
    count = struct.unpack_from('<I', data, directory + 24)[0]
    names = offset(struct.unpack_from('<I', data, directory + 32)[0])
    exports = set()
    for index in range(count):
        name = offset(struct.unpack_from('<I', data, names + 4 * index)[0])
        exports.add(data[name:data.index(b'\0', name)].decode('ascii'))
    return exports


def package(source: Path) -> Path:
    manifest = tomllib.loads((source / 'manifest.toml').read_text())['patch']
    declared = set(manifest['supported_versions'].values())
    required = set()
    targeted = set()
    hooks_files = sorted(source.glob('*.hooks.toml'))
    for path in hooks_files:
        table = tomllib.loads(path.read_text())
        targets = set(table['metadata']['target_versions'])
        if targeted & targets or not targets <= declared:
            raise ValueError(f'Invalid target mapping: {path.name}')
        targeted |= targets
        ranges = []
        for hook in table['hooks']:
            original = bytes(hook['original_bytes'])
            if not original:
                raise ValueError('Missing original bytes')
            start = hook['address']
            ranges.append((start, start + len(original)))
            if hook['type'] == 'detour':
                required.add(hook['function'])
                if len(original) < 5:
                    raise ValueError('Detour is shorter than a jump')
            elif hook['type'] in ('simple', 'replace'):
                replacement = bytes(hook['replacement_bytes'])
                if hook['type'] == 'simple' and len(replacement) != len(original):
                    raise ValueError('Simple patch length mismatch')
            else:
                raise ValueError('Unknown hook type')
        ranges.sort()
        if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
            raise ValueError(f'Overlapping hooks: {path.name}')
    if targeted != declared:
        raise ValueError('A supported target has no hooks')
    dll = source / 'dll' / 'windows_x86.dll'
    if dll_exports(dll.read_bytes()) != required:
        raise ValueError('DLL exports do not match hook functions')
    result = source.parent / 'HighFpsFixes.kpatch'
    members = [(source / 'manifest.toml', 'manifest.toml')]
    members += [(path, path.name) for path in hooks_files]
    members += [(dll, 'binaries/windows_x86.dll')]
    with zipfile.ZipFile(result, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path, name in members:
            entry = zipfile.ZipInfo(name, (2026, 9, 27, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, path.read_bytes(), compresslevel=9)
    with zipfile.ZipFile(result) as archive:
        if archive.testzip() is not None:
            raise ValueError('Archive integrity check failed')
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--package-only', action='store_true')
    parser.add_argument('--clang', default=os.environ.get('CLANG', 'clang++'))
    parser.add_argument('--linker', default=os.environ.get('LLD_LINK', 'lld-link'))
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    if not args.package_only:
        clang = shutil.which(args.clang)
        linker = shutil.which(args.linker)
        if not clang or not linker:
            parser.error('Clang and lld-link must be on PATH or supplied explicitly')
        with tempfile.TemporaryDirectory() as directory:
            obj = Path(directory) / 'HighFpsFixes.obj'
            subprocess.run([
                clang, '--target=i686-w64-windows-gnu', '-std=c++17', '-O2',
                '-ffreestanding', '-fno-builtin', '-fno-exceptions', '-fno-rtti',
                '-fno-stack-protector', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
                '-mno-stack-arg-probe', '-msse2', '-c',
                str(source / 'dll' / 'src' / 'HighFpsFixes.cpp'), '-o', str(obj),
            ], check=True)
            subprocess.run([
                linker, '/lldmingw', '/dll', '/noentry', '/nodefaultlib', '/machine:x86',
                '/safeseh:no', '/timestamp:0', '/dynamicbase', '/nxcompat',
                '/opt:ref', '/opt:icf', '/out:' + str(source / 'dll' / 'windows_x86.dll'),
                '/implib:' + str(Path(directory) / 'HighFpsFixes.lib'), str(obj),
            ], check=True)
    print(package(source))


if __name__ == '__main__':
    main()
