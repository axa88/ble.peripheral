#include "pairingState.h"
#include <limits>

namespace Pairing
{
std::optional<RequestId> State::create(ConnectionId connection, Type type, uint32_t value,
                                      uint64_t nowMs, uint64_t durationMs)
{
    if (!boot_ || sequence_ == std::numeric_limits<uint32_t>::max() || !durationMs ||
        durationMs > std::numeric_limits<uint64_t>::max() - nowMs || value > 999999 ||
        (type != Type::Display && (!connection || connection.boot != boot_)))
        return std::nullopt;
    // A second challenge cannot silently replace an unresolved ceremony.
    for (const auto& r : requests_)
        if (r.id && r.resolution == Resolution::Pending &&
            type != Type::Display && connection && r.connection == connection)
            return std::nullopt;
    for (auto& r : requests_)
    {
        if (!r.id || r.resolution != Resolution::Pending)
        {
            r = {{boot_, ++sequence_}, type == Type::Display ? ConnectionId{} : connection,
                 type, value, nowMs + durationMs, Resolution::Pending};
            ++changes_;
            return r.id;
        }
    }
    return std::nullopt;
}

ResponseResult State::respond(RequestId id, Type type, uint32_t value, uint64_t nowMs)
{
    for (auto& r : requests_)
    {
        if (!id || r.id != id) continue;
        if (r.resolution == Resolution::Expired) return ResponseResult::Expired;
        if (r.resolution == Resolution::Cancelled || r.resolution == Resolution::Completed)
            return ResponseResult::NotFound;
        if (r.resolution != Resolution::Pending) return ResponseResult::NotPending;
        // Leave overdue requests for the deadline executor so their BLE action is not lost.
        if (nowMs >= r.deadlineMs) return ResponseResult::Expired;
        if (r.type != type || type == Type::Display ||
            (type == Type::PasskeyEntry ? value > 999999 : value > 1))
            return ResponseResult::InvalidArgument;
        r.resolution = Resolution::Responded;
        ++changes_;
        return ResponseResult::Claimed;
    }
    return ResponseResult::NotFound;
}

std::optional<Request> State::expireOne(uint64_t nowMs)
{
    for (auto& r : requests_)
    {
        if (r.id && r.resolution == Resolution::Pending && nowMs >= r.deadlineMs)
        {
            r.resolution = Resolution::Expired;
            ++changes_;
            return r;
        }
    }
    return std::nullopt;
}

void State::invalidate(ConnectionId connection, bool completed)
{
    if (!connection) return;
    for (auto& r : requests_)
    {
        if (r.id && r.connection == connection &&
            (r.resolution == Resolution::Pending || r.resolution == Resolution::Responded))
        {
            r.resolution = completed ? Resolution::Completed : Resolution::Cancelled;
            ++changes_;
        }
    }
}

std::optional<uint64_t> State::nextDeadline() const
{
    std::optional<uint64_t> deadline;
    for (const auto& r : requests_)
        if (r.id && r.resolution == Resolution::Pending && (!deadline || r.deadlineMs < *deadline))
            deadline = r.deadlineMs;
    return deadline;
}
}
