#include "../common/include/Packet.hpp"
#include <iostream>
#include <cassert>
#include <string>

using namespace inferno;

void test_packet_serialization() {
    Packet packet(static_cast<uint16_t>(Opcode::CMD_EXEC), "whoami");
    const std::string expected_payload = "whoami";
    std::vector<uint8_t> serialized_packet = packet.serialize();
    assert(serialized_packet.size() == sizeof(PacketHeader) + expected_payload.size() && "Packet serialization total size mismatch!");
    assert(packet.getOpcode() == static_cast<uint16_t>(Opcode::CMD_EXEC) && "Opcode corrupted over the wire!");
    assert(packet.getPayload() == std::vector<uint8_t>(expected_payload.begin(), expected_payload.end()) && "Payload data dynamically corrupted during packet unpackaging!");
    std::cout << "[PASS] test_packet_serialization" << std::endl;
}

void test_packet_deserialization() {
    Packet packet(static_cast<uint16_t>(Opcode::CMD_EXEC), "whoami");
    const std::string expected_payload = "whoami";
    std::vector<uint8_t> serialized_packet = packet.serialize();
    std::optional<Packet> deserialized = Packet::deserialize(serialized_packet);
    assert(deserialized.has_value() && "Deserialization factory maliciously dropped valid packet!");
    assert(deserialized->getOpcode() == static_cast<uint16_t>(Opcode::CMD_EXEC) && "Opcode corrupted over the wire!");
    assert(deserialized->getPayload() == std::vector<uint8_t>(expected_payload.begin(), expected_payload.end()) && "Payload data dynamically corrupted during packet unpackaging!");
    std::string payload_str(deserialized->getPayload().begin(), deserialized->getPayload().end());
    assert(payload_str == expected_payload && "Payload data dynamically corrupted during packet unpackaging!");
    std::cout << "[PASS] test_packet_deserialization" << std::endl;
}

void test_packet_endianness() {
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
    // Construct a malicious header claiming a giant payload (1GB)
    PacketHeader header;
    header.magic = htonl(0xDEADBEEF);
    header.opcode = htons(0x0001);
    header.payload_size = htonl(1024 * 1024 * 1024); // 1GB
    header.checksum = htonl(0);

    std::vector<uint8_t> raw_data(sizeof(PacketHeader));
    std::memcpy(raw_data.data(), &header, sizeof(PacketHeader));

    auto deserialized = Packet::deserialize(raw_data);
    assert(!deserialized.has_value() && "Deserializer failed to block suspiciously large payload!");
    std::cout << "[PASS] test_packet_size_limit" << std::endl;
}

void test_fragmented_deserialization() {
    Packet packet(0x0001, "FragmentTest");
    std::vector<uint8_t> serialized = packet.serialize();

    // Split into 3 parts: Header part 1, Header part 2 + Payload part 1, Payload part 2
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
