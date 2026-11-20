#!/usr/bin/env python3
"""Verify a generated fake SELF's program ID and selected access profile.

Format: OpenOrbis create-fself/pkg/fself/FSELF.go, createSignature.
Profile: GoldHEN/GoldHEN_Cheat_Manager/Makefile and
Al-Azif/ps4-payload-guest/Makefile. The latter reads sandbox USBs at /usbN.
This checks bytes on the computer; it does not prove console access.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from preflight import ROOT

AUTHINFO_PATH = ROOT / 'sce_sys/authinfo.hex'
PROGRAM_AUTH_ID = 0x3800000000000011


def read_authinfo():
    raw = bytes.fromhex(AUTHINFO_PATH.read_text().strip())
    if len(raw) != 0x88:
        raise ValueError('Homebrew authinfo must contain exactly 136 bytes')
    return raw


def verify_self_authinfo(path, authinfo=None, profile='default'):
    if profile not in ('default', 'homebrew'):
        raise ValueError('Unknown SELF access profile')
    authinfo = (read_authinfo() if authinfo is None else authinfo) if profile == 'homebrew' else None
    data = Path(path).read_bytes()
    if (authinfo is not None and len(authinfo) != 0x88) or len(data) < 32 or data[:4] != b'O\x15=\x1d':
        raise ValueError('Invalid fake SELF or authentication profile')
    header_size, metadata_size = struct.unpack_from('<HH', data, 12)
    signature_offset = header_size + metadata_size - 0x100
    if header_size < 32 or metadata_size < 0x100 or signature_offset + 0x100 > len(data):
        raise ValueError('Invalid SELF metadata bounds')
    # The extended header follows the embedded ELF header and program headers.
    # Check the paid here as well, including when the authinfo signature is zero.
    entry_count = struct.unpack_from('<H', data, 24)[0]
    elf_offset = 32 + entry_count * 32
    if elf_offset + 64 > header_size or data[elf_offset:elf_offset + 4] != b'\x7fELF':
        raise ValueError('Invalid SELF embedded ELF header')
    program_size, program_count = struct.unpack_from('<HH', data, elf_offset + 54)
    extended_offset = (elf_offset + 64 + program_size * program_count + 15) & ~15
    if program_size != 56 or extended_offset + 64 > header_size:
        raise ValueError('Invalid SELF program-header bounds')
    if struct.unpack_from('<Q', data, extended_offset)[0] != PROGRAM_AUTH_ID:
        raise ValueError('Unexpected SELF program authentication ID')
    expected = (struct.pack('<QQ', len(authinfo), PROGRAM_AUTH_ID) + authinfo[8:]).ljust(0x100, b'\0') if authinfo is not None else bytes(0x100)
    if data[signature_offset:signature_offset + 0x100] != expected:
        raise ValueError('SELF does not match the selected access profile: ' + profile)
    return {'passed': True, 'hardware_tested': False,
            'profile': profile, 'explicit_authinfo': authinfo is not None,
            'authinfo_file': AUTHINFO_PATH.relative_to(ROOT).as_posix() if authinfo is not None else None,
            'authinfo_sha256': hashlib.sha256(authinfo).hexdigest() if authinfo is not None else None,
            'program_auth_id': hex(PROGRAM_AUTH_ID), 'signature_offset': signature_offset}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self', required=True, type=Path)
    parser.add_argument('--profile', choices=['default', 'homebrew'], default='default')
    arguments = parser.parse_args()
    print(json.dumps(verify_self_authinfo(arguments.self, profile=arguments.profile), indent=2))
