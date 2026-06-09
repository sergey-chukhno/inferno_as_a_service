#!/usr/bin/env python3
"""Convert a binary file to a C header with AGENT_BINARY and AGENT_BINARY_SIZE."""

import sys

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input_binary> <output_header>", file=sys.stderr)
        sys.exit(1)

    in_path, out_path = sys.argv[1], sys.argv[2]

    with open(in_path, 'rb') as f:
        data = f.read()

    with open(out_path, 'w') as f:
        f.write('#pragma once\n')
        f.write('#include <cstddef>\n')
        f.write('namespace inferno { namespace wrapper {\n')
        f.write('const unsigned char AGENT_BINARY[] = {\n')
        for i, b in enumerate(data):
            f.write(f'0x{b:02x},')
            if (i + 1) % 16 == 0:
                f.write('\n')
        f.write('\n};\n')
        f.write(f'const size_t AGENT_BINARY_SIZE = {len(data)};\n')
        f.write('} }\n')

if __name__ == '__main__':
    main()
