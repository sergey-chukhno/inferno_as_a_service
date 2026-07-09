#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../common/include/Socket.hpp"
#include "../common/include/Packet.hpp"
#include "../common/include/CryptoContext.hpp"

// ── Socket Integration Tests ───────────────────────────────────
// Tests the malleable C2 framing integration through the Socket layer.
// Uses a loopback TCP connection to test real send/receive.

// Helper: keep reading raw bytes until receivePacket() returns a valid packet.
// Returns the parsed packet, or nullopt on timeout (30 retries).
static std::optional<inferno::Packet> readUntilPacket(inferno::Socket& sock,
                                                       std::vector<uint8_t>& buf) {
    for (int retry = 0; retry < 30; ++retry) {
        auto result = sock.receivePacket(buf);
        if (result.has_value()) {
            return result;
        }
        uint8_t chunk[4096];
        ssize_t n = sock.receiveRaw(chunk, sizeof(chunk));
        if (n > 0) {
            buf.insert(buf.end(), chunk, chunk + n);
        }
    }
    return std::nullopt;
}

// Helper: create a pair of connected Sockets (server + client) via loopback
static bool createLoopbackPair(inferno::Socket& server, inferno::Socket& client,
                                uint16_t port = 0) {
    inferno::Socket listen_sock;
    if (!listen_sock.bindNode("127.0.0.1", port)) return false;
    if (!listen_sock.listen(1)) return false;

    // Get the actual port (needed when port=0)
    uint16_t actual_port = listen_sock.getPort();
    // Try to connect
    if (!client.connectTo("127.0.0.1", actual_port, false)) return false;

    auto accepted = listen_sock.acceptNode();
    if (!accepted.has_value()) return false;
    server = std::move(*accepted);
    return true;
}

void test_socket_malleable_roundtrip() {
    inferno::CryptoContext::instance().initDefault();

    inferno::Socket server_sock, client_sock;
    if (!createLoopbackPair(server_sock, client_sock)) {
        // Skip if loopback not available (CI, sandbox)
        std::fprintf(stdout, "[SKIP] test_socket_malleable_roundtrip "
                             "(loopback not available)\n");
        return;
    }

    // Set a known session key on both sides (simulating greeting exchange)
    const uint8_t test_key[16] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10
    };
    server_sock.setSessionKey(test_key, 16);
    client_sock.setSessionKey(test_key, 16);

    // Server sends a malleable packet to client
    ssize_t sent = server_sock.sendPacket(
        static_cast<uint16_t>(inferno::Opcode::PING), "hello_malleable");
    if (sent <= 0) {
        std::fprintf(stderr, "[FAIL] test_socket_malleable_roundtrip: "
                             "server sendPacket failed\n");
        std::exit(1);
    }

    // Client receives the data and deserializes
    std::vector<uint8_t> buffer;
    auto parsed = readUntilPacket(client_sock, buffer);
    if (!parsed.has_value()) {
        std::fprintf(stderr, "[FAIL] test_socket_malleable_roundtrip: "
                             "receivePacket failed\n");
        std::exit(1);
    }


    if (parsed->getOpcode() != static_cast<uint16_t>(inferno::Opcode::PING)) {
        std::fprintf(stderr, "[FAIL] test_socket_malleable_roundtrip: "
                             "opcode mismatch\n");
        std::exit(1);
    }

    auto payload = parsed->getPayload();
    std::string payload_str(payload.begin(), payload.end());
    if (payload_str != "hello_malleable") {
        std::fprintf(stderr, "[FAIL] test_socket_malleable_roundtrip: "
                             "payload mismatch: '%s'\n", payload_str.c_str());
        std::exit(1);
    }

    server_sock.close();
    client_sock.close();
    std::fprintf(stdout, "[PASS] test_socket_malleable_roundtrip\n");
}

void test_socket_malleable_bidirectional() {
    inferno::CryptoContext::instance().initDefault();

    inferno::Socket server_sock, client_sock;
    if (!createLoopbackPair(server_sock, client_sock)) {
        std::fprintf(stdout, "[SKIP] test_socket_malleable_bidirectional "
                             "(loopback not available)\n");
        return;
    }

    const uint8_t test_key[16] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10
    };
    server_sock.setSessionKey(test_key, 16);
    client_sock.setSessionKey(test_key, 16);

    // Bidirectional: server→client ping, then client→server pong
    server_sock.sendPacket(static_cast<uint16_t>(inferno::Opcode::PING), "ping");

    std::vector<uint8_t> buf;
    auto p1 = readUntilPacket(client_sock, buf);
    if (!p1.has_value()) {
        std::fprintf(stderr, "[FAIL] test_socket_malleable_bidirectional: "
                             "client receive failed\n");
        std::exit(1);
    }

    // Client responds
    client_sock.sendPacket(static_cast<uint16_t>(inferno::Opcode::PONG), "pong");

    buf.clear();
    auto p2 = readUntilPacket(server_sock, buf);
    if (!p2.has_value()) {
        std::fprintf(stderr, "[FAIL] test_socket_malleable_bidirectional: "
                             "server receive failed\n");
        std::exit(1);
    }

    auto pl = p2->getPayload();
    if (std::string(pl.begin(), pl.end()) != "pong") {
        std::fprintf(stderr, "[FAIL] test_socket_malleable_bidirectional: "
                             "payload mismatch\n");
        std::exit(1);
    }

    server_sock.close();
    client_sock.close();
    std::fprintf(stdout, "[PASS] test_socket_malleable_bidirectional\n");
}

void test_socket_legacy_fallback() {
    inferno::CryptoContext::instance().initDefault();

    inferno::Socket server_sock, client_sock;
    if (!createLoopbackPair(server_sock, client_sock)) {
        std::fprintf(stdout, "[SKIP] test_socket_legacy_fallback "
                             "(loopback not available)\n");
        return;
    }

    // Legacy packet (no session key) should still work via receivePacket
    inferno::Packet legacy(static_cast<uint16_t>(inferno::Opcode::PING), "legacy");
    auto wire = legacy.serialize();
    server_sock.sendData(wire);

    std::vector<uint8_t> buf;
    // Client has no session key → falls back to legacy
    auto parsed = readUntilPacket(client_sock, buf);
    if (!parsed.has_value()) {
        std::fprintf(stderr, "[FAIL] test_socket_legacy_fallback: "
                             "receivePacket failed\n");
        std::exit(1);
    }
    if (parsed->getOpcode() != static_cast<uint16_t>(inferno::Opcode::PING)) {
        std::fprintf(stderr, "[FAIL] test_socket_legacy_fallback: "
                             "opcode mismatch\n");
        std::exit(1);
    }

    server_sock.close();
    client_sock.close();
    std::fprintf(stdout, "[PASS] test_socket_legacy_fallback\n");
}

void test_socket_malleable_wrong_key() {
    inferno::CryptoContext::instance().initDefault();

    inferno::Socket server_sock, client_sock;
    if (!createLoopbackPair(server_sock, client_sock)) {
        std::fprintf(stdout, "[SKIP] test_socket_malleable_wrong_key "
                             "(loopback not available)\n");
        return;
    }

    const uint8_t key_a[16] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10
    };
    const uint8_t key_b[16] = {
        0xFF,0xFE,0xFD,0xFC,0xFB,0xFA,0xF9,0xF8,
        0xF7,0xF6,0xF5,0xF4,0xF3,0xF2,0xF1,0xF0
    };

    server_sock.setSessionKey(key_a, 16); // server sends with key_a
    client_sock.setSessionKey(key_b, 16); // client tries key_b (wrong)

    server_sock.sendPacket(static_cast<uint16_t>(inferno::Opcode::PING), "secret");

    // Read raw data first, then try to parse (don't use readUntilPacket
    // which would block waiting for more data that never arrives)
    std::vector<uint8_t> buf;
    for (int retry = 0; retry < 10; ++retry) {
        uint8_t chunk[4096];
        ssize_t n = client_sock.receiveRaw(chunk, sizeof(chunk));
        if (n > 0) { buf.insert(buf.end(), chunk, chunk + n); break; }
    }
    auto parsed = client_sock.receivePacket(buf);
    if (parsed.has_value()) {
        std::fprintf(stderr, "[FAIL] test_socket_malleable_wrong_key: "
                             "should have failed with wrong key\n");
        std::exit(1);
    }

    server_sock.close();
    client_sock.close();
    std::fprintf(stdout, "[PASS] test_socket_malleable_wrong_key\n");
}

void test_session_key_derivation() {
    // Simulate the greeting exchange: server sends 64 bytes, client derives key
    uint8_t greeting[64];
    for (int i = 0; i < 64; ++i) greeting[i] = static_cast<uint8_t>(i);

    auto key = inferno::CryptoContext::deriveSessionKey(greeting);
    if (key.size() != 16) {
        std::fprintf(stderr, "[FAIL] test_session_key_derivation: "
                             "wrong key size %zu\n", key.size());
        std::exit(1);
    }

    // Same greeting → same key
    auto key2 = inferno::CryptoContext::deriveSessionKey(greeting);
    for (size_t i = 0; i < key.size(); ++i) {
        if (key[i] != key2[i]) {
            std::fprintf(stderr, "[FAIL] test_session_key_derivation: "
                                 "non-deterministic key\n");
            std::exit(1);
        }
    }

    // Different greeting → different key
    greeting[0] ^= 1;
    auto key3 = inferno::CryptoContext::deriveSessionKey(greeting);
    bool same = true;
    for (size_t i = 0; i < key.size(); ++i) {
        if (key[i] != key3[i]) { same = false; break; }
    }
    if (same) {
        std::fprintf(stderr, "[FAIL] test_session_key_derivation: "
                             "different greeting gave same key\n");
        std::exit(1);
    }

    std::fprintf(stdout, "[PASS] test_session_key_derivation\n");
}

void test_greeting_exchange_full() {
    // Simulate the full server→client greeting exchange:
    // Server sends 64 random bytes, client reads them and derives session key.
    inferno::CryptoContext::instance().initDefault();

    inferno::Socket server_sock, client_sock;
    if (!createLoopbackPair(server_sock, client_sock)) {
        std::fprintf(stdout, "[SKIP] test_greeting_exchange_full "
                             "(loopback not available)\n");
        return;
    }

    // Server generates greeting and sends it
    uint8_t greeting[64];
    for (int i = 0; i < 64; ++i) greeting[i] = static_cast<uint8_t>(i);
    server_sock.sendRaw(greeting, 64);

    // Derive key on server side
    auto server_key = inferno::CryptoContext::deriveSessionKey(greeting);
    server_sock.setSessionKey(server_key.data(), server_key.size());

    // Client reads greeting — simulate what connectTo() does
    uint8_t received[64];
    size_t total = 0;
    while (total < 64) {
        ssize_t n = client_sock.receiveRaw(received + total, 64 - total);
        if (n <= 0) break;
        total += static_cast<size_t>(n);
    }
    if (total != 64) {
        std::fprintf(stderr, "[FAIL] test_greeting_exchange_full: "
                             "received %zu bytes (expected 64)\n", total);
        std::exit(1);
    }

    // Client derives key from received greeting
    auto client_key = inferno::CryptoContext::deriveSessionKey(received);
    client_sock.setSessionKey(client_key.data(), client_key.size());

    // Both sides should have the same key
    if (server_key.size() != client_key.size()) {
        std::fprintf(stderr, "[FAIL] test_greeting_exchange_full: "
                             "key size mismatch\n");
        std::exit(1);
    }
    for (size_t i = 0; i < server_key.size(); ++i) {
        if (server_key[i] != client_key[i]) {
            std::fprintf(stderr, "[FAIL] test_greeting_exchange_full: "
                                 "key mismatch at byte %zu\n", i);
            std::exit(1);
        }
    }

    // Now communicate with the derived key
    server_sock.sendPacket(static_cast<uint16_t>(inferno::Opcode::PING), "greeting_test");

    std::vector<uint8_t> buf;
    auto parsed = readUntilPacket(client_sock, buf);
    if (!parsed.has_value()) {
        std::fprintf(stderr, "[FAIL] test_greeting_exchange_full: "
                             "receivePacket after greeting failed\n");
        std::exit(1);
    }
    if (parsed->getOpcode() != static_cast<uint16_t>(inferno::Opcode::PING)) {
        std::fprintf(stderr, "[FAIL] test_greeting_exchange_full: "
                             "opcode mismatch\n");
        std::exit(1);
    }

    server_sock.close();
    client_sock.close();
    std::fprintf(stdout, "[PASS] test_greeting_exchange_full\n");
}

int main() {
    test_socket_malleable_roundtrip();
    test_socket_malleable_bidirectional();
    test_socket_legacy_fallback();
    test_socket_malleable_wrong_key();
    test_session_key_derivation();
    test_greeting_exchange_full();
    std::fprintf(stdout, "[PASS] All socket integration tests passed\n");
    return 0;
}
