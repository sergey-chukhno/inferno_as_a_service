#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>
#include <string>

#include "common/include/Transport.hpp"
#include "common/include/TlsTransport.hpp"
#include "common/include/Socket.hpp"
#include "common/include/Packet.hpp"
#include "client/include/Http2Client.hpp"

#define TEST(name) std::printf("[TEST] %s...\n", name)
#define PASS() std::printf("[PASS] %s\n", __func__)

// ═══════════════════════════════════════════════════════════════════
// Test 1: ITransport type enum
// ═══════════════════════════════════════════════════════════════════
void test_transport_type_enum() {
    TEST("TransportType enum values");
    int tcp_val = static_cast<int>(inferno::TransportType::TCP);
    int h2_val  = static_cast<int>(inferno::TransportType::HTTP2);
    assert(tcp_val == 0);
    assert(h2_val == 1);
    PASS();
}

// ═══════════════════════════════════════════════════════════════════
// Test 2: Socket implements ITransport
// ═══════════════════════════════════════════════════════════════════
void test_tcp_transport_interface() {
    TEST("Socket implements ITransport interface");
    inferno::Socket sock;
    assert(!sock.isConnected());
    assert(static_cast<int>(sock.type()) == static_cast<int>(inferno::TransportType::TCP));
    PASS();
}

// ═══════════════════════════════════════════════════════════════════
// Test 3: TlsTransport default state
// ═══════════════════════════════════════════════════════════════════
void test_tls_transport_default_state() {
    TEST("TlsTransport default state");
    inferno::TlsTransport tls;
    assert(!tls.isConnected());
    assert(!tls.isAlpnH2());
    assert(tls.type() == inferno::TransportType::HTTP2);
    PASS();
}

// ═══════════════════════════════════════════════════════════════════
// Test 4: Http2Client default state
// ═══════════════════════════════════════════════════════════════════
void test_http2_client_default_state() {
    TEST("Http2Client default state");
    inferno::Http2Client client;
    assert(!client.isConnected());
    assert(client.transport().type() == inferno::TransportType::HTTP2);
    PASS();
}

// ═══════════════════════════════════════════════════════════════════
// Test 5: Packet serialization still works (regression)
// ═══════════════════════════════════════════════════════════════════
void test_packet_regression() {
    TEST("Packet serialize/deserialize (regression)");
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    inferno::Packet pkt(0x0001, payload);
    auto data = pkt.serialize();
    assert(!data.empty());
    auto deserialized = inferno::Packet::deserialize(data);
    assert(deserialized.has_value());
    assert(deserialized->getOpcode() == 0x0001);
    auto p = deserialized->getPayload();
    assert(p.size() == 4);
    assert(p[0] == 0xDE && p[1] == 0xAD && p[2] == 0xBE && p[3] == 0xEF);
    PASS();
}

// ═══════════════════════════════════════════════════════════════════
// Test 6: TlsTransport move semantics
// ═══════════════════════════════════════════════════════════════════
void test_tls_transport_move() {
    TEST("TlsTransport move semantics");
    inferno::TlsTransport tls1;
    inferno::TlsTransport tls2 = std::move(tls1);
    assert(!tls2.isConnected());
    assert(tls2.type() == inferno::TransportType::HTTP2);
    PASS();
}

// ═══════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════

int main() {
    std::printf("\n=== HTTP/2 Transport Tests ===\n\n");

    test_transport_type_enum();
    test_tcp_transport_interface();
    test_tls_transport_default_state();
    test_http2_client_default_state();
    test_packet_regression();
    test_tls_transport_move();

    std::printf("\n=== ALL HTTP/2 TRANSPORT TESTS PASSED ===\n");
    return 0;
}
