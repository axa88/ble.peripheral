#pragma once
#include <cstdint>

// Opaque boot nonce + monotonically increasing sequence; never a native handle.
struct ConnectionId
{
    uint64_t boot = 0;
    uint32_t sequence = 0;
    explicit operator bool() const { return boot != 0 && sequence != 0; }
};
inline bool operator==(ConnectionId a, ConnectionId b)
{
    return a.boot == b.boot && a.sequence == b.sequence;
}
inline bool operator!=(ConnectionId a, ConnectionId b) { return !(a == b); }
