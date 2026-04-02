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
