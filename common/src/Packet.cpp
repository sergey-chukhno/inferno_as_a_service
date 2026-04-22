#include "../include/Packet.hpp"
#include <cstring> // For std::memcpy
#include <optional>
#include <vector>
#include <arpa/inet.h> // For htonl, htons, ntohl, ntohs

namespace inferno {

// calculateChecksum() helper function 

// cryptographic hash function to sign payload logic 
static uint32_t compute_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return crc ^ 0xFFFFFFFF;
}

void Packet::calculateChecksum() {
    if (m_payload.empty()) {
        m_header.checksum = 0;
        return;
    }   
    m_header.checksum = compute_crc32(m_payload.data(), m_payload.size());
}

// Packet constructors

Packet::Packet() {
    m_header.magic = 0xDEADBEEF;
    m_header.opcode = 0;
    m_header.payload_size = 0;
    m_header.checksum = 0;
}

// Constructor for creating a NEW packet to send
Packet::Packet(uint16_t opcode, const std::vector<uint8_t>& payload) : m_payload(payload) {
    m_header.magic = 0xDEADBEEF;
    m_header.opcode = opcode;
    m_header.payload_size = static_cast<uint32_t>(m_payload.size());
    calculateChecksum();
}

// Constructor for creating a NEW packet to send
Packet::Packet(uint16_t opcode, const std::string& string_payload) : m_payload(string_payload.begin(), string_payload.end()) {
    m_header.magic = 0xDEADBEEF;
    m_header.opcode = opcode;
    m_header.payload_size = static_cast<uint32_t>(m_payload.size());
    calculateChecksum();
}

// Private constructor used by deserialize()
Packet::Packet(const PacketHeader& header, std::vector<uint8_t> payload) 
    : m_header(header), m_payload(std::move(payload)) {}

Packet::~Packet() = default;
    

// serialize()  

std::vector<uint8_t> Packet::serialize() const {
    std::vector<uint8_t> buffer;
    buffer.reserve(sizeof(PacketHeader) + m_payload.size());
    
    PacketHeader network_header = m_header;
    network_header.magic        = htonl(m_header.magic);
    network_header.opcode       = htons(m_header.opcode);
    network_header.payload_size = htonl(m_header.payload_size);
    network_header.checksum     = htonl(m_header.checksum);

    const uint8_t* header_ptr = reinterpret_cast<const uint8_t*>(&network_header);
    buffer.insert(buffer.end(), header_ptr, header_ptr + sizeof(PacketHeader));
    buffer.insert(buffer.end(), m_payload.begin(), m_payload.end());
    return buffer;
}


// deserialize()

std::optional<Packet> Packet::deserialize(const std::vector<uint8_t>& raw_data) {
    if (raw_data.size() < sizeof(PacketHeader)) {
        return std::nullopt;            
    }

    PacketHeader header;
    std::memcpy(&header, raw_data.data(), sizeof(PacketHeader));

    // Convert from Network Byte Order to Host Byte Order
    header.magic        = ntohl(header.magic);
    header.opcode       = ntohs(header.opcode);
    header.payload_size = ntohl(header.payload_size);
    header.checksum     = ntohl(header.checksum);

    if (header.magic != 0xDEADBEEF) {
        return std::nullopt;
    }

    if (header.payload_size > MAX_PAYLOAD_SIZE) {
        return std::nullopt; // Protect against OOM attacks
    }

    if (raw_data.size() < sizeof(PacketHeader) + header.payload_size) {
        return std::nullopt;
    }

    std::vector<uint8_t> payload(raw_data.begin() + sizeof(PacketHeader), 
                                 raw_data.begin() + sizeof(PacketHeader) + header.payload_size);

    if (header.checksum != compute_crc32(payload.data(), payload.size())) {
        return std::nullopt;
    }

    return Packet(header, payload);
}


// Getters
uint16_t Packet::getOpcode() const {
    return m_header.opcode;
}

const std::vector<uint8_t>& Packet::getPayload() const {
    return m_payload;
}   

} // namespace inferno