#include "bleConfig.h"
#include "pairingCoordinator.h"
#if BLE_ENABLE_SERIAL_CONSOLE || BLE_ENABLE_WEB_INTERFACE
#include <mutex>
#include <optional>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>
#include <inttypes.h>

namespace PairingCoordinator
{
namespace
{
// Native injection can synchronously deliver authentication callbacks.
// Serialize claim + dispatch with lifecycle updates, while permitting that reentry.
std::recursive_mutex mutex;
std::optional<Pairing::State> state;
Actions actions{};
TaskHandle_t executor = nullptr;
uint64_t now() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
void wake() { if (executor) xTaskNotifyGive(executor); }
void run(void*)
{
    for (;;)
    {
        std::optional<Pairing::Request> expired;
        TickType_t wait = portMAX_DELAY;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            const auto time = now();
            expired = state->expireOne(time);
            if (expired)
            {
                Serial.printf("[PAIR] Deadline | request:%016" PRIx64 "-%08" PRIx32 " | nowMs:%" PRIu64 " | deadlineMs:%" PRIu64 "\n",
                    expired->id.boot, expired->id.sequence, time, expired->deadlineMs);
                bool nativeReturn = false;
                if (expired->type == Pairing::Type::NumericConfirmation)
                    nativeReturn = actions.injectConfirmation(expired->connection, false);
                else if (expired->type == Pairing::Type::PasskeyEntry)
                    nativeReturn = actions.disconnect(expired->connection);
                if (expired->type != Pairing::Type::Display)
                    Serial.printf("[PAIR] Deadline action | request:%016" PRIx64 "-%08" PRIx32 " | action:%s | nativeReturn:%u\n",
                        expired->id.boot, expired->id.sequence,
                        expired->type == Pairing::Type::PasskeyEntry ? "disconnect" : "reject", nativeReturn ? 1u : 0u);
            }
            if (auto deadline = state->nextDeadline())
                wait = *deadline <= time ? 1 : pdMS_TO_TICKS(*deadline - time) + 1;
        }
        if (expired) continue;
        ulTaskNotifyTake(pdTRUE, wait);
    }
}
}
bool begin(uint64_t boot, Actions registered)
{
    if (state) return executor != nullptr;
    if (!boot || !registered.injectPasskey || !registered.injectConfirmation || !registered.disconnect)
        return false;
    state.emplace(boot);
    actions = registered;
    if (xTaskCreate(run, "pairing-deadline", 3072, nullptr, 2, &executor) == pdPASS) return true;
    state.reset();
    return false;
}
bool publish(ConnectionId connection, Pairing::Type type, uint32_t value)
{
    bool created = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (state) created = state->create(connection, type, value, now(), 25000).has_value();
    }
    wake();
    return created;
}
void invalidate(ConnectionId connection, bool completed)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (state) state->invalidate(connection, completed);
    }
    wake();
}
Snapshot snapshot()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return state ? Snapshot{state->requests(), state->changes()} : Snapshot{};
}
uint64_t changes()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return state ? state->changes() : 0;
}
uint32_t stackHighWaterBytes()
{
    // ESP-IDF reports bytes here, unlike upstream FreeRTOS's word count.
    return executor ? static_cast<uint32_t>(uxTaskGetStackHighWaterMark(executor)) : 0;
}
Result respond(Pairing::RequestId id, Pairing::Type type, uint32_t value)
{
    // Hold through the bounded BLE call: an old response must not be dispatched
    // after completion and a new challenge on the same still-connected peer.
    std::lock_guard<std::recursive_mutex> lock(mutex);
    ConnectionId connection;
    {
        if (!state) return Result::NotFound;
        auto result = state->respond(id, type, value, now());
        switch (result)
        {
            case Pairing::ResponseResult::NotFound: return Result::NotFound;
            case Pairing::ResponseResult::NotPending: return Result::NotPending;
            case Pairing::ResponseResult::Expired: wake(); return Result::Expired;
            case Pairing::ResponseResult::InvalidArgument: return Result::InvalidArgument;
            case Pairing::ResponseResult::Claimed: break;
        }
        for (const auto& request : state->requests())
            if (request.id == id) connection = request.connection;
    }
    wake();
    // Native acceptance is not authentication success. Live identity is revalidated by each action.
    bool accepted = type == Pairing::Type::PasskeyEntry ? actions.injectPasskey(connection, value) :
                                                        actions.injectConfirmation(connection, value != 0);
    return accepted ? Result::Accepted : Result::Failed;
}
}
#else
namespace PairingCoordinator
{
bool begin(uint64_t, Actions) { return true; }
bool publish(ConnectionId, Pairing::Type, uint32_t) { return false; }
void invalidate(ConnectionId, bool) {}
Snapshot snapshot() { return {}; }
uint64_t changes() { return 0; }
uint32_t stackHighWaterBytes() { return 0; }
Result respond(Pairing::RequestId, Pairing::Type, uint32_t) { return Result::NotFound; }
}
#endif
