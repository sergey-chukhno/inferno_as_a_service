#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace inferno {

class Propagator {
public:
    Propagator() = default;
    ~Propagator() = default;
    Propagator(const Propagator&) = delete;
    Propagator& operator=(const Propagator&) = delete;

    struct Result {
        bool success;
        std::string output;
    };

    enum class Command : uint8_t {
        SCAN  = 0,
        BRUTE = 1,
        DEPLOY = 2
    };

    Result execute(Command cmd, const std::string& target);

private:
    Result scan();
    Result brute(const std::string& target);
    Result deploy(const std::string& target);

    // Only target Docker bridge subnet by default for safety
    static constexpr const char* DEFAULT_SUBNET = "172.17.0.0/16";
};

} // namespace inferno
