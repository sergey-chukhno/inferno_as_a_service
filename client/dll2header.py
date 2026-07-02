#!/usr/bin/env python3
"""XOR-encrypt a DLL and emit a C header with the ciphertext + key.

Usage: dll2header.py --key <hex_key> <dll_path> <output_header>
"""
import sys
import argparse


def _xor_encrypt(data: bytes, key: bytes) -> bytes:
    if not key:
        return data
    return bytes(data[i] ^ key[i % len(key)] for i in range(len(data)))


def _write_array(f, name, data):
    f.write(f'const unsigned char {name}[] = {{\n')
    for i, b in enumerate(data):
        f.write(f'0x{b:02x},')
        if (i + 1) % 16 == 0:
            f.write('\n')
    f.write('\n};\n')
    f.write(f'const size_t {name}_SIZE = {len(data)};\n')


def main():
    parser = argparse.ArgumentParser(
        description="Generate C header with XOR-encrypted DLL bytes"
    )
    parser.add_argument("--key", type=str, required=True,
                        help="XOR encryption key as hex (e.g. 2B7E151628AED2A6)")
    parser.add_argument("dll_path", help="Path to the compiled DLL")
    parser.add_argument("output_header", help="Path to the output C header")
    args = parser.parse_args()

    try:
        key = bytes.fromhex(args.key)
    except ValueError as e:
        print(f"Error: invalid hex key '{args.key}': {e}", file=sys.stderr)
        sys.exit(1)

    if not key:
        print("Error: key must be non-empty", file=sys.stderr)
        sys.exit(1)

    try:
        with open(args.dll_path, 'rb') as f:
            dll_data = f.read()
    except (IOError, OSError) as e:
        print(f"Error reading DLL '{args.dll_path}': {e}", file=sys.stderr)
        sys.exit(1)

    encrypted = _xor_encrypt(dll_data, key)

    with open(args.output_header, 'w') as f:
        f.write('#pragma once\n')
        f.write('#include <cstddef>\n')
        f.write('namespace inferno { namespace embedded {\n')
        _write_array(f, 'ENCRYPTED_DLL', encrypted)
        f.write('\n')
        f.write('const unsigned char XOR_KEY[] = {\n')
        for i, b in enumerate(key):
            f.write(f'0x{b:02x},')
            if (i + 1) % 16 == 0:
                f.write('\n')
        f.write('\n};\n')
        f.write(f'const size_t XOR_KEY_LEN = {len(key)};\n')
        f.write('} }\n')

    print(f"[dll2header] Wrote {args.output_header} "
          f"({len(dll_data)} bytes encrypted with {len(key)}-byte key)")


if __name__ == '__main__':
    main()
