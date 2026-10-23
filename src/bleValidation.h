#pragma once
#include <cstdint>
#include <optional>

namespace BleValidation
{
struct ConnectionParameters
{
    uint16_t minIntervalUnits;
    uint16_t maxIntervalUnits;
    uint16_t latency;
    uint16_t supervisionTimeoutUnits;
};

// Validate both the operator's millisecond values and the exact native units.
inline std::optional<ConnectionParameters> connectionParameters(
    uint32_t minMs, uint32_t maxMs, uint16_t latency, uint32_t timeoutMs)
{
    if (minMs < 8 || maxMs < minMs || maxMs > 4000 || latency > 499 ||
        timeoutMs < 100 || timeoutMs > 32000 ||
        timeoutMs <= 2ULL * (static_cast<uint64_t>(latency) + 1) * maxMs)
        return std::nullopt;
    const auto minUnits = static_cast<uint16_t>((static_cast<uint64_t>(minMs) * 4 + 2) / 5);
    const auto maxUnits = static_cast<uint16_t>((static_cast<uint64_t>(maxMs) * 4 + 2) / 5);
    const auto timeoutUnits = static_cast<uint16_t>((static_cast<uint64_t>(timeoutMs) + 5) / 10);
    // 10 ms timeout units versus 1.25 ms interval units, strict > as in BLE.
    if (static_cast<uint64_t>(timeoutUnits) * 4 <= (static_cast<uint64_t>(latency) + 1) * maxUnits)
        return std::nullopt;
    return ConnectionParameters{minUnits, maxUnits, latency, timeoutUnits};
}
}
