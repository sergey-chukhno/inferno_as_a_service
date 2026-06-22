#include "../common/include/Packet.hpp"
#include "../common/include/CryptoContext.hpp"
#include <iostream>
#include <cassert>
#include <string>
#include <cstring>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

using namespace inferno;

void test_packet_serialization() {
    CryptoContext::instance().initDefault();

    Packet packet(static_cast<uint16_t>(Opcode::CMD_EXEC), "whoami");
    const std::string expected_payload = "whoami";
    std::vector<uint8_t> serialized_packet = packet.serialize();
    // Wire size = header + IV + ciphertext + GCM tag
    size_t expected_wire_size = sizeof(PacketHeader) + expected_payload.size() + CryptoContext::OVERHEAD;
    assert(serialized_packet.size() == expected_wire_size &&
           "Packet serialization size mismatch (encryption overhead)");
    assert(packet.getOpcode() == static_cast<uint16_t>(Opcode::CMD_EXEC) &&
           "Opcode corrupted over the wire!");
    assert(packet.getPayload() == std::vector<uint8_t>(expected_payload.begin(), expected_payload.end()) &&
           "Payload data corrupted during packet unpackaging!");
    std::cout << "[PASS] test_packet_serialization" << std::endl;
}

void test_packet_deserialization() {
    CryptoContext::instance().initDefault();

    Packet packet(static_cast<uint16_t>(Opcode::CMD_EXEC), "whoami");
    const std::string expected_payload = "whoami";
    std::vector<uint8_t> serialized_packet = packet.serialize();
    std::optional<Packet> deserialized = Packet::deserialize(serialized_packet);
    assert(deserialized.has_value() && "Deserialization factory dropped valid packet!");
    assert(deserialized->getOpcode() == static_cast<uint16_t>(Opcode::CMD_EXEC) &&
           "Opcode corrupted over the wire!");
    assert(deserialized->getPayload() == std::vector<uint8_t>(expected_payload.begin(), expected_payload.end()) &&
           "Payload data corrupted during packet unpackaging!");
    std::string payload_str(deserialized->getPayload().begin(), deserialized->getPayload().end());
    assert(payload_str == expected_payload &&
           "Payload data corrupted during packet unpackaging!");
    std::cout << "[PASS] test_packet_deserialization" << std::endl;
}

void test_packet_endianness() {
    CryptoContext::instance().initDefault();

    Packet packet(0x1234, "test");
    std::vector<uint8_t> serialized = packet.serialize();

    // Magic 0xDEADBEEF -> DE AD BE EF
    assert(serialized[0] == 0xDE);
    assert(serialized[1] == 0xAD);
    assert(serialized[2] == 0xBE);
    assert(serialized[3] == 0xEF);

    // Opcode 0x1234 -> 12 34
    assert(serialized[4] == 0x12);
    assert(serialized[5] == 0x34);

    std::cout << "[PASS] test_packet_endianness" << std::endl;
}

void test_packet_size_limit() {
    CryptoContext::instance().initDefault();

    // Construct a malicious header claiming a giant payload
    PacketHeader header;
    header.magic = htonl(0xDEADBEEF);
    header.opcode = htons(0x0001);
    header.payload_size = htonl(1024 * 1024 * 1024); // 1GB — exceeds MAX_WIRE_SIZE

    std::vector<uint8_t> raw_data(sizeof(PacketHeader));
    std::memcpy(raw_data.data(), &header, sizeof(PacketHeader));

    auto deserialized = Packet::deserialize(raw_data);
    assert(!deserialized.has_value() && "Deserializer failed to block suspiciously large payload!");
    std::cout << "[PASS] test_packet_size_limit" << std::endl;
}

void test_fragmented_deserialization() {
    CryptoContext::instance().initDefault();

    Packet packet(0x0001, "FragmentTest");
    std::vector<uint8_t> serialized = packet.serialize();

    // Split into 3 parts: first 5 bytes, next 10 bytes, rest
    std::vector<uint8_t> part1(serialized.begin(), serialized.begin() + 5);
    std::vector<uint8_t> part2(serialized.begin() + 5, serialized.begin() + 15);
    std::vector<uint8_t> part3(serialized.begin() + 15, serialized.end());

    std::vector<uint8_t> accumulator;

    // Part 1
    accumulator.insert(accumulator.end(), part1.begin(), part1.end());
    assert(!Packet::deserialize(accumulator).has_value());

    // Part 2
    accumulator.insert(accumulator.end(), part2.begin(), part2.end());
    assert(!Packet::deserialize(accumulator).has_value());

    // Part 3
    accumulator.insert(accumulator.end(), part3.begin(), part3.end());
    auto final_packet = Packet::deserialize(accumulator);
    assert(final_packet.has_value() && "Failed to reconstruct fragmented packet!");
    assert(final_packet->getOpcode() == 0x0001);

    std::cout << "[PASS] test_fragmented_deserialization" << std::endl;
}

void test_empty_payload_encrypt_decrypt() {
    CryptoContext::instance().initDefault();

    Packet send(static_cast<uint16_t>(Opcode::SYS_REQ_INFO), "");
    std::vector<uint8_t> wire = send.serialize();

    fprintf(stderr, "[TEST] wire size=%zu\n", wire.size());

    auto recv = Packet::deserialize(wire);
    if (!recv.has_value()) {
        fprintf(stderr, "[TEST] FAILED to deserialize\n");
        assert(false && "Empty payload roundtrip failed");
    }
    assert(recv->getOpcode() == static_cast<uint16_t>(Opcode::SYS_REQ_INFO));
    assert(recv->getPayload().empty());
    std::cout << "[PASS] test_empty_payload_encrypt_decrypt\n";
}

void test_inject_packet_roundtrip() {
    CryptoContext::instance().initDefault();

    std::string test_path = "/Applications/Slack.app/Contents/MacOS/Slack";
    Packet send(static_cast<uint16_t>(Opcode::INJECT), test_path);
    std::vector<uint8_t> wire = send.serialize();

    auto recv = Packet::deserialize(wire);
    assert(recv.has_value() && "INJECT packet roundtrip failed");
    assert(recv->getOpcode() == static_cast<uint16_t>(Opcode::INJECT));
    std::string payload(recv->getPayload().begin(), recv->getPayload().end());
    assert(payload == test_path && "INJECT payload corrupted");
    std::cout << "[PASS] test_inject_packet_roundtrip\n";
}

void test_inject_res_packet_roundtrip() {
    CryptoContext::instance().initDefault();

    std::string result = "/Applications/Slack.app||1|1";
    Packet send(static_cast<uint16_t>(Opcode::INJECT_RES), result);
    std::vector<uint8_t> wire = send.serialize();

    auto recv = Packet::deserialize(wire);
    assert(recv.has_value() && "INJECT_RES packet roundtrip failed");
    assert(recv->getOpcode() == static_cast<uint16_t>(Opcode::INJECT_RES));
    std::string payload(recv->getPayload().begin(), recv->getPayload().end());
    assert(payload == result && "INJECT_RES payload corrupted");
    std::cout << "[PASS] test_inject_res_packet_roundtrip\n";
}
