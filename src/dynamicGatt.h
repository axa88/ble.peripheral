#pragma once
#include "connectionId.h"
#include "gattValue.h"
#include <string>
#include <string_view>
#include <vector>

namespace DynamicGatt
{
    enum class State { Ready, Applying, RecoveryRequired };
    State CurrentState();
    const char* StateName();
    bool Ready();
    // Nonblocking entry lease. Host/timer callbacks must never wait for mutation.
    class Entry
    {
    public:
        explicit Entry(bool requireReady = true);
        ~Entry();
        explicit operator bool() const { return held_; }
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
    private:
        bool held_ = false;
    };
    inline constexpr size_t ServiceLimit = 4;
    struct ServiceSnapshot
    {
        ConnectionId id{};
        std::string serviceUuid, characteristicUuid;
        bool protectedService = false;
    };
    struct Inventory
    {
        State state = State::Ready;
        bool available = false;
        std::vector<ServiceSnapshot> services;
        std::string reason;
    };
    enum class Outcome { Applied, Dispatched, Busy, NotReady, InvalidArgument, NotFound, Failed };
    struct Result
    {
        Outcome outcome = Outcome::Failed;
        std::string detail, errorCode;
    };
    Inventory GetInventory();
    bool CanonicalUuid(std::string_view input, std::string& normalized);
    Result AddService(std::string_view serviceUuid, std::string_view characteristicUuid, const GattValue::Buffer& bytes, size_t length);
    Result RemoveService(ConnectionId id);
    Result SendServiceChanged();
}
