#include "dynamicGatt.h"
#include "fixtureGatt.h"
#include "bluetoothManager.h"
#include "advertising.h"
#include "gattRebuild.h"
#include <NimBLEDevice.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include <array>
#include <algorithm>
#include <mutex>
#include <memory>
#include <cstring>
#include <utility>
#ifdef USING_NIMBLE_ARDUINO_HEADERS
#include "nimble/porting/nimble/include/nimble/nimble_port.h"
#else
#include "nimble/nimble_port.h"
#endif

namespace DynamicGatt
{
namespace
{
    std::mutex entryMutex;
    std::atomic<State> state{State::Ready};
    struct Managed
    {
        ConnectionId id{};
        std::string serviceUuid, characteristicUuid;
        NimBLEService* service = nullptr;
        NimBLECharacteristic* characteristic = nullptr;
    };
    std::array<Managed, ServiceLimit> managed{};
    uint64_t boot = 0;
    uint32_t sequence = 0;
    constexpr std::array<uint16_t, 6> descriptorUuids{{0x2900, 0x2901, 0x2904, 0x2905, 0x2906, 0}};
    NimBLEServer* server() { return BluetoothManager::Instance().Server(); }
    std::array<NimBLEService*, 2> protectedServices()
    {
        return {{server()->getServiceByUUID(FixtureGatt::ServiceUuid), server()->getServiceByUUID(FixtureGatt::DescriptorServiceUuid)}};
    }
    Result unavailable()
    {
        if (state.load() == State::RecoveryRequired)
            return {Outcome::NotReady, "Restart the device: GATT recovery is required.", "gatt_recovery_required"};
        return {Outcome::Busy, "GATT is unavailable or another BLE entry is active.", "gatt_busy"};
    }
    bool nativeIdle()
    {
        if (!server() || server()->getConnectedCount() || ble_gap_adv_active() || ble_gap_disc_active() || ble_gap_conn_active()) return false;
#if CONFIG_BT_NIMBLE_EXT_ADV
        for (uint8_t i = 0; i < CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES; ++i)
            if (ble_gap_ext_adv_active(i)) return false;
#endif
        return true;
    }
    bool readEquals(uint16_t handle, const uint8_t* bytes, size_t length)
    {
        os_mbuf* data = nullptr;
        const int rc = ble_att_svr_read_local(handle, &data);
        bool ok = rc == 0 && data && OS_MBUF_PKTLEN(data) == length;
        std::array<uint8_t, BLE_ATT_ATTR_MAX_LEN> copy{};
        if (ok) ok = length <= copy.size() && os_mbuf_copydata(data, 0, length, copy.data()) == 0 &&
            (!length || std::memcmp(copy.data(), bytes, length) == 0);
        if (data) os_mbuf_free_chain(data);
        return ok;
    }
    bool nativeCharacteristic(NimBLEService* svc, NimBLECharacteristic* chr)
    {
        uint16_t declaration = 0, value = 0;
        if (!svc || !chr || chr->getService() != svc || !chr->getHandle() ||
            ble_gatts_find_chr(svc->getUUID().getBase(), chr->getUUID().getBase(), &declaration, &value) || value != chr->getHandle()) return false;
        os_mbuf* data = nullptr;
        const uint16_t flags = chr->getProperties();
        const uint8_t declarationProperties = static_cast<uint8_t>((flags & 0x7F) | ((flags & (BLE_GATT_CHR_F_RELIABLE_WRITE | BLE_GATT_CHR_F_AUX_WRITE)) ? 0x80 : 0));
        const int rc = ble_att_svr_read_local(declaration, &data);
        uint8_t fields[3]{};
        const bool ok = !rc && data && OS_MBUF_PKTLEN(data) >= 5 && os_mbuf_copydata(data, 0, 3, fields) == 0 &&
            fields[0] == declarationProperties && (uint16_t(fields[1]) | uint16_t(fields[2]) << 8) == value;
        if (data) os_mbuf_free_chain(data);
        if (!ok) return false;
        const auto current = chr->getValue();
        if ((chr->getProperties() & NIMBLE_PROPERTY::READ) && !readEquals(value, current.data(), current.size())) return false;
        return true;
    }
    struct Preserved
    {
        NimBLECharacteristic* characteristic;
        uint16_t handle, properties;
        std::vector<uint8_t> value;
    };
    struct DescriptorIdentity { NimBLEDescriptor* descriptor; NimBLECharacteristic* characteristic; NimBLEService* service; NimBLEUUID uuid; uint16_t handle, permissions; std::vector<uint8_t> value; };
    struct FixtureIdentity
    {
        std::vector<Preserved> values;
        std::vector<DescriptorIdentity> descriptors;
        std::vector<std::pair<NimBLECharacteristic*, uint16_t>> cccds;
        uint16_t gapHandle = 0, nameHandle = 0, appearanceHandle = 0;
        std::array<uint16_t, 2> serviceHandles{};
        uint16_t gattHandle = 0, changedHandle = 0, cccdHandle = 0;
        uint16_t serverFeaturesHandle = 0, clientFeaturesHandle = 0;
        bool capture()
        {
            const auto services = protectedServices();
            for (size_t i = 0; i < services.size(); ++i)
            {
                auto* svc = services[i];
                if (!svc || !svc->getHandle()) return false;
                serviceHandles[i] = svc->getHandle();
                for (auto* chr : svc->getCharacteristics())
                {
                    const auto v = chr->getValue();
                    values.push_back({chr, chr->getHandle(), chr->getProperties(), {v.begin(), v.end()}});
                    if (chr->getProperties() & (NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE))
                    {
                        uint16_t cccdHandle = 0;
                        const NimBLEUUID cccd(uint16_t{0x2902});
                        if (ble_gatts_find_dsc(svc->getUUID().getBase(), chr->getUUID().getBase(), cccd.getBase(), &cccdHandle) || !cccdHandle) return false;
                        cccds.emplace_back(chr, cccdHandle);
                    }
                    // Metadata belongs to the fixture; discover descriptors from its actual objects.
                    for (auto uuid : descriptorUuids)
                    {
                        const NimBLEUUID id(uuid ? uuid : uint16_t{0xFFFF});
                        auto* d = uuid ? chr->getDescriptorByUUID(id) : chr->getDescriptorByUUID(NimBLEUUID(FixtureGatt::CustomDescriptorUuid));
                        if (d) { const auto v = d->getValue(); descriptors.push_back({d, chr, svc, d->getUUID(), d->getHandle(), d->getProperties(), {v.begin(), v.end()}}); }
                    }
                }
            }
            const NimBLEUUID gap(uint16_t{0x1800}), name(uint16_t{0x2A00}), appearance(uint16_t{0x2A01});
            if (ble_gatts_find_svc(gap.getBase(), &gapHandle) || !gapHandle ||
                ble_gatts_find_chr(gap.getBase(), name.getBase(), nullptr, &nameHandle) || !nameHandle ||
                ble_gatts_find_chr(gap.getBase(), appearance.getBase(), nullptr, &appearanceHandle) || !appearanceHandle) return false;
            const NimBLEUUID gatt(uint16_t{0x1801}), changed(uint16_t{0x2A05}), cccd(uint16_t{0x2902});
            return !ble_gatts_find_svc(gatt.getBase(), &gattHandle) &&
                !ble_gatts_find_chr(gatt.getBase(), changed.getBase(), nullptr, &changedHandle) &&
                !ble_gatts_find_dsc(gatt.getBase(), changed.getBase(), cccd.getBase(), &cccdHandle) &&
                gattHandle && changedHandle && cccdHandle &&
                !ble_gatts_find_chr(gatt.getBase(), NimBLEUUID(uint16_t{0x2B3A}).getBase(), nullptr, &serverFeaturesHandle) && serverFeaturesHandle &&
                !ble_gatts_find_chr(gatt.getBase(), NimBLEUUID(uint16_t{0x2B29}).getBase(), nullptr, &clientFeaturesHandle) && clientFeaturesHandle;
        }
        bool verify()
        {
            // Preserve static handles as well as values: bonded CCCDs are keyed by ATT handle.
            const auto services = protectedServices();
            for (size_t i = 0; i < services.size(); ++i)
            {
                uint16_t handle = 0;
                if (!services[i] || ble_gatts_find_svc(services[i]->getUUID().getBase(), &handle) ||
                    handle != serviceHandles[i] || handle != services[i]->getHandle()) return false;
            }
            for (const auto& p : values)
            {
                const auto v = p.characteristic->getValue();
                if (p.characteristic->getHandle() != p.handle || p.characteristic->getProperties() != p.properties ||
                    v.size() != p.value.size() || !std::equal(v.begin(), v.end(), p.value.begin()) ||
                    !nativeCharacteristic(p.characteristic->getService(), p.characteristic)) return false;
            }
            for (const auto& p : descriptors)
            {
                auto* d = p.descriptor;
                auto* c = d->getCharacteristic();
                uint16_t handle = 0;
                const auto v = d->getValue();
                if (!c || c != p.characteristic || !p.characteristic || p.characteristic->getDescriptorByUUID(p.uuid) != d || c->getService() != p.service || !(d->getUUID() == p.uuid) || !d->getHandle() || d->getHandle() != p.handle || d->getProperties() != p.permissions || v.size() != p.value.size() || !std::equal(v.begin(), v.end(), p.value.begin()) ||
                    ble_gatts_find_dsc(c->getService()->getUUID().getBase(), c->getUUID().getBase(), d->getUUID().getBase(), &handle) ||
                    handle != d->getHandle() || !readEquals(handle, v.data(), v.size())) return false;
            }
            for (const auto& p : cccds)
            {
                uint16_t handle = 0;
                const NimBLEUUID cccd(uint16_t{0x2902});
                if (ble_gatts_find_dsc(p.first->getService()->getUUID().getBase(), p.first->getUUID().getBase(), cccd.getBase(), &handle) || handle != p.second) return false;
            }
            uint16_t handle = 0;
            const NimBLEUUID gap(uint16_t{0x1800}), name(uint16_t{0x2A00}), appearance(uint16_t{0x2A01});
            if (ble_gatts_find_svc(gap.getBase(), &handle) || handle != gapHandle ||
                ble_gatts_find_chr(gap.getBase(), name.getBase(), nullptr, &handle) || handle != nameHandle ||
                ble_gatts_find_chr(gap.getBase(), appearance.getBase(), nullptr, &handle) || handle != appearanceHandle) return false;
            const NimBLEUUID gatt(uint16_t{0x1801}), changed(uint16_t{0x2A05}), cccd(uint16_t{0x2902});
            return !ble_gatts_find_svc(gatt.getBase(), &handle) && handle == gattHandle &&
                !ble_gatts_find_chr(gatt.getBase(), changed.getBase(), nullptr, &handle) && handle == changedHandle &&
                !ble_gatts_find_dsc(gatt.getBase(), changed.getBase(), cccd.getBase(), &handle) && handle == cccdHandle &&
                !ble_gatts_find_chr(gatt.getBase(), NimBLEUUID(uint16_t{0x2B3A}).getBase(), nullptr, &handle) && handle == serverFeaturesHandle &&
                !ble_gatts_find_chr(gatt.getBase(), NimBLEUUID(uint16_t{0x2B29}).getBase(), nullptr, &handle) && handle == clientFeaturesHandle;
        }
    };
    bool protectedUuid(const NimBLEUUID& id)
    {
        for (auto* svc : protectedServices())
        {
            if (!svc) return true;
            if (svc->getUUID() == id) return true;
            for (auto* chr : svc->getCharacteristics()) if (chr->getUUID() == id) return true;
        }
        return false;
    }
    Result validateAdd(std::string_view serviceText, std::string_view characteristicText, size_t length, std::string& serviceUuid, std::string& characteristicUuid)
    {
        if (length > GattValue::MaxWebLength || !CanonicalUuid(serviceText, serviceUuid) || !CanonicalUuid(characteristicText, characteristicUuid))
            return {Outcome::InvalidArgument, "Use canonical 128-bit UUIDs and at most 256 value bytes.", "invalid_argument"};
        const NimBLEUUID svc(serviceUuid), chr(characteristicUuid);
        if (svc == chr || protectedUuid(svc) || protectedUuid(chr) || server()->getServiceByUUID(svc))
            return {Outcome::InvalidArgument, "UUID is protected, duplicated, or equal to its characteristic UUID.", "invalid_argument"};
        size_t count = 0;
        for (const auto& m : managed)
        {
            if (!m.service) continue;
            ++count;
            if (m.serviceUuid == serviceUuid || m.characteristicUuid == characteristicUuid || m.characteristicUuid == serviceUuid || m.serviceUuid == characteristicUuid)
                return {Outcome::InvalidArgument, "UUID is already used by a temporary service.", "invalid_argument"};
        }
        if (count == ServiceLimit) return {Outcome::InvalidArgument, "Four temporary services are already registered.", "service_limit"};
        if (sequence == UINT32_MAX) return {Outcome::Failed, "Restart the device: temporary service identifiers are exhausted.", "service_id_exhausted"};
        return {Outcome::Applied, {}, {}};
    }
    // Jobs use the existing NimBLE parent event queue, never a new worker/task.
    // Structural callers retain the entry lease until the event completes. There is
    // deliberately no timeout that can report failure while queued mutation survives.
    template<class Function> auto onHost(Function function) -> decltype(function())
    {
        using Return = decltype(function());
        struct Job
        {
            ble_npl_event event{};
            SemaphoreHandle_t done = xSemaphoreCreateBinary();
            Function function;
            Return result{};
            explicit Job(Function f) : function(std::move(f)) {}
            ~Job() { if (done) vSemaphoreDelete(done); }
        } job(std::move(function));
        if (!job.done) return Return{};
        ble_npl_event_init(&job.event, [](ble_npl_event* ev) {
            auto* j = static_cast<Job*>(ble_npl_event_get_arg(ev));
            j->result = j->function();
            xSemaphoreGive(j->done);
        }, &job);
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &job.event);
        xSemaphoreTake(job.done, portMAX_DELAY);
        return std::move(job.result);
    }
    struct NativeOperation
    {
        bool adding;
        Managed* target;
        std::string serviceUuid, characteristicUuid;
        const GattValue::Buffer& bytes;
        size_t length;
        FixtureIdentity fixture;
        bool mutated = false;
        bool uncertain = false;
        bool idle()
        {
            if (!nativeIdle()) return false;
            if (!fixture.capture()) { uncertain = true; return false; }
            for (const auto& m : managed)
            {
                if (!m.service) continue;
                uint16_t handle = 0;
                if (!m.characteristic || !m.service->getHandle() || m.service->getRemoved() ||
                    server()->getServiceByUUID(m.serviceUuid.c_str()) != m.service ||
                    ble_gatts_find_svc(m.service->getUUID().getBase(), &handle) || handle != m.service->getHandle() ||
                    !nativeCharacteristic(m.service, m.characteristic))
                { uncertain = true; return false; }
            }
            return true;
        }
        bool mutate()
        {
            if (adding)
            {
                // IDs are consumed even if a later allocation/registration fails.
                if (!boot) { boot = (uint64_t(esp_random()) << 32) | esp_random(); if (!boot) boot = 1; }
                target->id = {boot, ++sequence};
                target->serviceUuid = serviceUuid;
                target->characteristicUuid = characteristicUuid;
                target->service = server()->createService(serviceUuid.c_str());
                mutated = target->service != nullptr;
                if (!target->service) return false;
                target->characteristic = target->service->createCharacteristic(characteristicUuid.c_str(), NIMBLE_PROPERTY::READ, GattValue::MaxWebLength);
                if (!target->characteristic) return false;
                target->characteristic->setValue(bytes.data(), length);
                return true;
            }
            auto* svc = target->service;
            if (!svc || !svc->getHandle() || server()->getServiceByHandle(svc->getHandle()) != svc) return false;
            server()->removeService(svc, true);
            if (!svc->getRemoved()) return false; // wrapper removal is void and may silently do nothing
            mutated = true;
            // start/reset can delete svc; never retain/dereference this pointer afterward.
            target->service = nullptr;
            target->characteristic = nullptr;
            return true;
        }
        bool reset() { return ble_gatts_reset() == 0; }
        void stop() { ble_gatts_stop(); }
        bool start() { return server()->start(); }
        bool verify()
        {
            if (!FixtureGatt::FinalizeAfterRebuild() || !fixture.verify()) return false;
            if (adding)
            {
                if (!target->characteristic) return false;
                const auto initial = target->characteristic->getValue();
                if (initial.size() != length || (length && std::memcmp(initial.data(), bytes.data(), length))) return false;
            }
            for (const auto& m : managed)
            {
                if (!m.service) continue;
                uint16_t handle = 0;
                if (!m.characteristic || server()->getServiceByUUID(m.serviceUuid.c_str()) != m.service ||
                    m.service->getServer() != server() || m.characteristic->getProperties() != NIMBLE_PROPERTY::READ ||
                    m.characteristic->getValue().max_size() != GattValue::MaxWebLength ||
                    ble_gatts_find_svc(m.service->getUUID().getBase(), &handle) || !handle || handle != m.service->getHandle() ||
                    !nativeCharacteristic(m.service, m.characteristic)) return false;
                const NimBLEUUID cccd(uint16_t{0x2902});
                if (ble_gatts_find_dsc(m.service->getUUID().getBase(), m.characteristic->getUUID().getBase(), cccd.getBase(), nullptr) != BLE_HS_ENOENT) return false;
            }
            if (!adding)
            {
                const NimBLEUUID removed(serviceUuid);
                if (ble_gatts_find_svc(removed.getBase(), nullptr) != BLE_HS_ENOENT || server()->getServiceByUUID(removed)) return false;
            }
            return true;
        }
    };
    Result apply(bool adding, Managed* target, std::string serviceUuid, std::string characteristicUuid, const GattValue::Buffer& bytes, size_t length)
    {
        if (Advertising::IsDirectedActive()) return {Outcome::Busy, "Stop directed advertising before changing services.", "directed_advertising_active"};
        Advertising::RebuildSnapshot advertising;
        if (!Advertising::CaptureForRebuild(advertising)) return {Outcome::Busy, "Advertising snapshot is unavailable.", "advertising_busy"};
        // Entry is held: a completed host job cannot be replaced during this check.
        if (!FixtureGatt::BeginRebuild()) return {Outcome::Busy, "A GATT snapshot/publication is pending or fixtures are not ready.", "gatt_busy"};
        state.store(State::Applying);
        const auto stopped = Advertising::StopAll();
        GattRebuild::Outcome outcome = GattRebuild::Outcome::PreconditionsFailed;
        bool mutated = false;
        if (!stopped.failed)
            outcome = onHost([&]() {
                NativeOperation operation{adding, target, serviceUuid, characteristicUuid, bytes, length, {}, false};
                const auto result = GattRebuild::Run(operation);
                mutated = operation.mutated || operation.uncertain;
                return result;
            });
        if (outcome != GattRebuild::Outcome::Applied)
        {
            if (mutated)
            {
                // Finalization may have passed before another native verification failed.
                FixtureGatt::BeginRebuild();
                state.store(State::RecoveryRequired);
                return {Outcome::Failed, "Restart the device: native GATT state could not be verified. Advertising and GATT operations are disabled.", "gatt_recovery_required"};
            }
            const bool fixtureReady = FixtureGatt::FinalizeAfterRebuild();
            const bool restored = fixtureReady && Advertising::RestoreAfterRebuild(advertising);
            state.store(fixtureReady ? State::Ready : State::RecoveryRequired);
            return {Outcome::Failed, fixtureReady ? (restored ? "GATT preconditions failed; database was unchanged." : "Database unchanged; advertising restoration is incomplete.") : "Restart the device: fixture recovery failed.",
                fixtureReady ? (restored ? "gatt_precondition_failed" : "advertising_restore_incomplete") : "gatt_recovery_required"};
        }
        if (!adding) *target = Managed{};
        const bool restored = Advertising::RestoreAfterRebuild(advertising);
        state.store(State::Ready);
        return {Outcome::Applied, restored ? "Service database verified. Reconnect and explicitly rediscover services." : "Service database verified; advertising restoration is incomplete.", restored ? "" : "advertising_restore_incomplete"};
    }
}
Entry::Entry(bool requireReady)
{
    held_ = entryMutex.try_lock();
    if (held_ && requireReady && state.load() != State::Ready) { entryMutex.unlock(); held_ = false; }
}
Entry::~Entry()
{
    if (held_)
    {
        entryMutex.unlock();
        if (state.load() == State::Ready) Advertising::DrainDeferredCallbacks();
    }
}
State CurrentState() { return state.load(); }
const char* StateName()
{
    switch (state.load()) { case State::Ready: return "Ready"; case State::Applying: return "Applying"; default: return "RecoveryRequired"; }
}
bool Ready() { return state.load() == State::Ready && FixtureGatt::Ready(); }
bool CanonicalUuid(std::string_view input, std::string& normalized)
{
    if (input.size() != 36) return false;
    normalized.clear(); normalized.reserve(36);
    for (size_t i = 0; i < input.size(); ++i)
    {
        const char c = input[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; normalized += c; continue; }
        if (c >= '0' && c <= '9') normalized += c;
        else if (c >= 'a' && c <= 'f') normalized += c;
        else if (c >= 'A' && c <= 'F') normalized += char(c + 'a' - 'A');
        else return false;
    }
    // Reject every Bluetooth Base UUID alias, including 16- and 32-bit forms.
    return normalized.substr(8) != "-0000-1000-8000-00805f9b34fb";
}
Inventory GetInventory()
{
    Inventory result;
    Entry entry;
    result.state = state.load();
    if (!entry || !Ready()) { result.reason = state.load() == State::RecoveryRequired ? "Restart the device: GATT recovery is required." : "GATT inventory is busy or not ready."; return result; }
    result.services.push_back({{}, FixtureGatt::ServiceUuid, {}, true});
    result.services.push_back({{}, FixtureGatt::DescriptorServiceUuid, {}, true});
    for (const auto& m : managed) if (m.service) result.services.push_back({m.id, m.serviceUuid, m.characteristicUuid, false});
    result.available = true;
    return result;
}
Result AddService(std::string_view serviceText, std::string_view characteristicText, const GattValue::Buffer& bytes, size_t length)
{
    Entry entry;
    if (!entry || !Ready() || !server()) return unavailable();
    std::string svc, chr;
    const auto validation = validateAdd(serviceText, characteristicText, length, svc, chr);
    if (validation.outcome != Outcome::Applied) return validation;
    auto it = std::find_if(managed.begin(), managed.end(), [](const Managed& m) { return !m.service; });
    return apply(true, &*it, svc, chr, bytes, length);
}
Result RemoveService(ConnectionId id)
{
    Entry entry;
    if (!entry || !Ready() || !server()) return unavailable();
    auto it = std::find_if(managed.begin(), managed.end(), [&](const Managed& m) { return m.service && m.id == id; });
    if (it == managed.end()) return {Outcome::NotFound, "Temporary service identifier is unknown or belongs to another boot.", "service_not_found"};
    return apply(false, &*it, it->serviceUuid, it->characteristicUuid, {}, 0);
}
Result SendServiceChanged()
{
    Entry entry;
    if (!entry || !Ready() || !server()) return unavailable();
    return onHost([]() -> Result {
        if (!Ready() || !server()) return unavailable();
        if (!BluetoothManager::Instance().SendServiceChangedIndication()) return unavailable();
        return {Outcome::Dispatched, "Full-range Service Changed dispatched; delivery and acknowledgement are not established.", {}};
    });
}
}
