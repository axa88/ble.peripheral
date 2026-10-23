#pragma once
#include "pairingState.h"

namespace PairingCoordinator
{
struct Actions
{
    bool (*injectPasskey)(ConnectionId, uint32_t);
    bool (*injectConfirmation)(ConnectionId, bool);
    bool (*disconnect)(ConnectionId);
};
struct Snapshot
{
    std::array<Pairing::Request, Pairing::State::Capacity> requests;
    uint64_t changes;
};
enum class Result { Accepted, Failed, NotFound, NotPending, Expired, InvalidArgument };
bool begin(uint64_t boot, Actions actions);
bool publish(ConnectionId connection, Pairing::Type type, uint32_t value = 0);
void invalidate(ConnectionId connection, bool completed = false);
Snapshot snapshot();
uint64_t changes();
uint32_t stackHighWaterBytes();
Result respond(Pairing::RequestId id, Pairing::Type type, uint32_t value);
}
