#pragma once
#include "connectionId.h"
#include <array>
#include <cstddef>
#include <optional>

namespace Pairing
{
using RequestId = ConnectionId;
enum class Type { PasskeyEntry, NumericConfirmation, Display };
enum class Resolution { Pending, Responded, Expired, Cancelled, Completed };
enum class ResponseResult { Claimed, NotFound, NotPending, Expired, InvalidArgument };
struct Request
{
    RequestId id;
    ConnectionId connection; // Empty for device-level display notifications.
    Type type = Type::Display;
    uint32_t value = 0;
    uint64_t deadlineMs = 0;
    Resolution resolution = Resolution::Cancelled;
};

// Caller serializes access. No runtime or interface dependencies.
class State
{
public:
    static constexpr size_t Capacity = 8;
    explicit State(uint64_t boot) : boot_(boot) {}
    std::optional<RequestId> create(ConnectionId connection, Type type, uint32_t value,
                                    uint64_t nowMs, uint64_t durationMs);
    ResponseResult respond(RequestId id, Type type, uint32_t value, uint64_t nowMs);
    std::optional<Request> expireOne(uint64_t nowMs);
    void invalidate(ConnectionId connection, bool completed);
    std::optional<uint64_t> nextDeadline() const;
    const std::array<Request, Capacity>& requests() const { return requests_; }
    uint64_t changes() const { return changes_; }
private:
    uint64_t boot_;
    uint32_t sequence_ = 0;
    uint64_t changes_ = 0;
    std::array<Request, Capacity> requests_{};
};
}
