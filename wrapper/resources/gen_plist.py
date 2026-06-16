#!/usr/bin/env python3
"""Generate Info.plist for macOS .app bundle."""

import plistlib
import sys

out_path = sys.argv[1]
plist = {
    'CFBundleExecutable': 'invoice_pdf',
    'CFBundleIdentifier': 'com.invoice.pdf',
    'CFBundleName': 'Invoice',
    'CFBundlePackageType': 'APPL',
}
with open(out_path, 'wb') as f:
    plistlib.dump(plist, f)
