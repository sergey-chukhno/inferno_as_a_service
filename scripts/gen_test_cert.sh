#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════
# Inferno — Generate Self-Signed Code Signing Certificate (dev only)
# ═══════════════════════════════════════════════════════════════════════
#
# Generates a self-signed PFX certificate at secrets/cert.pfx for local
# development of the code signing pipeline.
#
# IMPORTANT: Self-signed certificates do NOT bypass Windows SmartScreen.
# They are only useful for testing the build pipeline locally. Production
# builds require an EV code signing certificate from a trusted CA.
#
# Prerequisites:
#   - openssl (macOS: built-in, Linux: apt install openssl)
#
# Usage:
#   ./scripts/gen_test_cert.sh
#   export INFERNO_CERT_PASSWORD="inferno_dev"
#   cmake -B build -DINFERNO_CODE_SIGN=ON
#   cmake --build build --target inferno_wrapper
# ═══════════════════════════════════════════════════════════════════════

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$PROJECT_DIR/secrets"
OUT_PFX="$OUT_DIR/cert.pfx"
TMP_KEY="$(mktemp)"
TMP_CERT="$(mktemp)"
PASSWORD="${1:-inferno_dev}"
SUBJ="${2:-/CN=Inferno Development Self-Signed}"

cleanup() {
    rm -f "$TMP_KEY" "$TMP_CERT"
}
trap cleanup EXIT

if ! command -v openssl &>/dev/null; then
    echo "[gen_test_cert] Error: openssl not found."
    echo "  Install it:  brew install openssl  (macOS)"
    echo "               apt install openssl   (Linux)"
    exit 1
fi

mkdir -p "$OUT_DIR"

echo "[gen_test_cert] Generating self-signed certificate..."
echo "  Subject: $SUBJ"
echo "  Output:  $OUT_PFX"
echo "  Warning: Self-signed certs do NOT bypass SmartScreen (dev only)"

# Step 1: Generate a 4096-bit RSA private key
openssl genrsa -out "$TMP_KEY" 4096

# Step 2: Generate a self-signed certificate with code signing EKU
openssl req -x509 -new \
    -key "$TMP_KEY" \
    -sha256 -days 365 \
    -subj "$SUBJ" \
    -addext "keyUsage=digitalSignature" \
    -addext "extendedKeyUsage=codeSigning" \
    -out "$TMP_CERT"

# Step 3: Bundle private key + certificate into a PKCS#12/PFX file
openssl pkcs12 -export \
    -in "$TMP_CERT" \
    -inkey "$TMP_KEY" \
    -out "$OUT_PFX" \
    -passout "pass:$PASSWORD" \
    -name "Inferno Dev Cert"

echo "[gen_test_cert] Certificate created: $OUT_PFX ($(wc -c < "$OUT_PFX") bytes)"
echo ""
echo "  Export the password and build:"
echo "    export INFERNO_CERT_PASSWORD=\"$PASSWORD\""
echo "    cmake -B build -DINFERNO_CODE_SIGN=ON"
echo "    cmake --build build --target inferno_wrapper"
echo ""
echo "  Note: the password is used ONCE to import the PFX into the"
echo "  Windows certificate store. The signtool sign command itself"
echo "  does NOT receive the password (selects cert by subject name)."
echo ""
echo "  Verify signature (requires signtool on Windows):"
echo "    signtool verify /v /pa build/invoice.pdf.exe"
