#include "pch.h"
#include "../../include/RiftNet/RiftServer.hpp"
#include "../core/riftnetio/RiftNetIO.hpp"
#include "../core/networkio/INetworkIOEvents.hpp"
#include "../core/connection/Connection.hpp"

#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>
#include <stop_token>
#include <cassert>

// Provide a hash for NetworkEndpoint so it can be used in std::unordered_map.
namespace std {
    template <>
    struct hash<RiftNet::Networking::NetworkEndpoint> {
        size_t operator()(const RiftNet::Networking::NetworkEndpoint& ep) const noexcept {
            return hash<string>()(ep.ipAddress) ^ (hash<uint16_t>()(ep.port) << 1);
        }
    };
}

// The internal C++ implementation of the server.
class RiftServer_Internal : public RiftNet::Networking::INetworkIOEvents {
private:
    using ConnectionPtr = std::shared_ptr<RiftNet::Protocol::Connection>;

public:
    explicit RiftServer_Internal(const RiftServerConfig* config)
        : m_config(*config)
        , m_networkIO(std::make_unique<RiftNet::Networking::WinSocketIO>())
        , m_isRunning(false)
        , m_nextClientId(1) {
    }

    ~RiftServer_Internal() {
        Stop();
#ifndef NDEBUG
        assert(!m_updateThread.joinable() && "update thread still joinable in destructor");
#endif
    }

    RiftResult Start() {
        if (m_isRunning.load(std::memory_order_acquire) || m_updateThread.joinable())
            return RIFT_ERROR_GENERIC;

        if (!m_networkIO->Init(m_config.host_address, m_config.port, this)) {
            return RIFT_ERROR_SOCKET_BIND_FAILED;
        }
        if (!m_networkIO->Start()) {
            return RIFT_ERROR_GENERIC;
        }

        m_isRunning.store(true, std::memory_order_release);
        m_updateThread = std::jthread([this](std::stop_token st) { Update(st); });
        return RIFT_SUCCESS;
    }

    void Stop() {
        // Always take the same path; no early return that could skip joining.
        m_isRunning.store(false, std::memory_order_release);

        if (m_updateThread.joinable()) {
            const bool self_call = (m_updateThread.get_id() == std::this_thread::get_id());
            m_updateThread.request_stop();
            if (m_networkIO) m_networkIO->Stop();
            if (!self_call) m_updateThread.join();
        }
        else {
            if (m_networkIO) m_networkIO->Stop();
        }

        // Drop connections after IO/threads are quiesced
        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            m_clientsByEndpoint.clear();
            m_clientsById.clear();
        }
    }

    RiftResult Send(RiftClientId client_id, const uint8_t* data, size_t size, bool reliable) {
        if (!data || size == 0) return RIFT_ERROR_INVALID_PARAMETER;
        std::lock_guard<std::mutex> lock(m_clientMutex);
        auto it = m_clientsById.find(client_id);
        if (it != m_clientsById.end()) {
            it->second->SendApplicationData(data, static_cast<uint32_t>(size), reliable);
            return RIFT_SUCCESS;
        }
        return RIFT_ERROR_INVALID_PARAMETER;
    }

    void Broadcast(const uint8_t* data, size_t size, bool reliable) {
        if (!data || size == 0) return;
        std::lock_guard<std::mutex> lock(m_clientMutex);
        for (const auto& pair : m_clientsById) {
            pair.second->SendApplicationData(data, static_cast<uint32_t>(size), reliable);
        }
    }

    // =========================
    // INetworkIOEvents
    // =========================
    void OnRawDataReceived(const RiftNet::Networking::NetworkEndpoint& sender,
        const uint8_t* data,
        uint32_t size,
        RiftNet::Networking::OverlappedIOContext* /*context*/) override
    {
        if (!m_isRunning.load(std::memory_order_acquire)) return;

        auto connection = FindOrCreateConnection(sender);
        if (connection) {
            connection->ProcessIncomingRawPacket(data, size);
        }
    }

    void OnSendCompleted(RiftNet::Networking::OverlappedIOContext* /*context*/,
        bool /*success*/,
        uint32_t /*bytesSent*/) override {
    }

    void OnNetworkError(const std::string& /*errorMessage*/, int /*errorCode*/) override {
        // Optional: surface a server error event here
    }

private:
    void Update(std::stop_token st) {
        using namespace std::chrono_literals;
        const auto kTick = 100ms;
        const auto kIdleTimeout = std::chrono::seconds(30); // give clients time; client sends keepalives

        while (!st.stop_requested()) {
            std::this_thread::sleep_for(kTick);
            if (!m_isRunning.load(std::memory_order_acquire)) break;

            const auto now = std::chrono::steady_clock::now();
            std::vector<RiftClientId> toDisconnect;

            {
                std::lock_guard<std::mutex> lock(m_clientMutex);
                for (auto& pair : m_clientsById) {
                    pair.second->Update(now);
                    if (pair.second->IsTimedOut(now, kIdleTimeout)) {
                        toDisconnect.push_back(pair.first);
                    }
                }
            }

            for (RiftClientId id : toDisconnect) {
                DisconnectClient(id);
            }
        }
    }

    ConnectionPtr FindOrCreateConnection(const RiftNet::Networking::NetworkEndpoint& endpoint) {
        std::lock_guard<std::mutex> lock(m_clientMutex);

        if (auto it = m_clientsByEndpoint.find(endpoint); it != m_clientsByEndpoint.end()) {
            return it->second;
        }

        RiftClientId newId = m_nextClientId.fetch_add(1, std::memory_order_relaxed);
        auto newConnection = std::make_shared<RiftNet::Protocol::Connection>(endpoint, /*isServer=*/true);

        newConnection->SetSendCallback([this](const RiftNet::Networking::NetworkEndpoint& ep,
            const std::vector<uint8_t>& data) {
                m_networkIO->SendData(ep, data.data(), static_cast<uint32_t>(data.size()));
            });

        newConnection->SetAppDataCallback([this, newId](const uint8_t* data, uint32_t size) {
            RiftEvent appEvent{};
            appEvent.type = RIFT_EVENT_PACKET_RECEIVED;
            appEvent.data.packet.sender_id = newId;
            appEvent.data.packet.data = data;
            appEvent.data.packet.size = size;
            m_config.event_callback(&appEvent, m_config.user_data);
            });

        m_clientsById[newId] = newConnection;
        m_clientsByEndpoint[endpoint] = newConnection;

        RiftEvent connectedEvent{};
        connectedEvent.type = RIFT_EVENT_CLIENT_CONNECTED;
        connectedEvent.data.client_id = newId;
        m_config.event_callback(&connectedEvent, m_config.user_data);

        return newConnection;
    }

    void DisconnectClient(RiftClientId id) {
        ConnectionPtr connection = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            auto it = m_clientsById.find(id);
            if (it != m_clientsById.end()) {
                connection = it->second;
                m_clientsByEndpoint.erase(connection->GetEndpoint());
                m_clientsById.erase(it);
            }
        }

        if (connection) {
            RiftEvent disconnectedEvent{};
            disconnectedEvent.type = RIFT_EVENT_CLIENT_DISCONNECTED;
            disconnectedEvent.data.client_id = id;
            m_config.event_callback(&disconnectedEvent, m_config.user_data);
        }
    }

private:
    RiftServerConfig m_config;
    std::unique_ptr<RiftNet::Networking::INetworkIO> m_networkIO;

    std::mutex m_clientMutex;
    std::unordered_map<RiftClientId, ConnectionPtr> m_clientsById;
    std::unordered_map<RiftNet::Networking::NetworkEndpoint, ConnectionPtr> m_clientsByEndpoint;
    std::atomic<RiftClientId> m_nextClientId;

    std::atomic<bool> m_isRunning;
    std::jthread m_updateThread; // auto-joins in dtor; keep last
};

// =====================================================================================
// C-Style API Implementation
// =====================================================================================
#ifdef __cplusplus
extern "C" {
#endif

    RiftServerHandle rift_server_create(const RiftServerConfig* config) {
        if (!config || !config->event_callback) return nullptr;
        try {
            return reinterpret_cast<RiftServerHandle>(new RiftServer_Internal(config));
        }
        catch (...) {
            return nullptr;
        }
    }

    void rift_server_destroy(RiftServerHandle server) {
        if (server) {
            delete reinterpret_cast<RiftServer_Internal*>(server);
        }
    }

    RiftResult rift_server_start(RiftServerHandle server) {
        if (!server) return RIFT_ERROR_INVALID_HANDLE;
        return reinterpret_cast<RiftServer_Internal*>(server)->Start();
    }

    void rift_server_stop(RiftServerHandle server) {
        if (server) {
            reinterpret_cast<RiftServer_Internal*>(server)->Stop();
        }
    }

    // Default to RELIABLE to feed RTT sampling / latency bench.
    RiftResult rift_server_send(RiftServerHandle server, RiftClientId client_id, const uint8_t* data, size_t size) {
        if (!server) return RIFT_ERROR_INVALID_HANDLE;
        return reinterpret_cast<RiftServer_Internal*>(server)->Send(client_id, data, size, /*reliable=*/true);
    }

    RiftResult rift_server_broadcast(RiftServerHandle server, const uint8_t* data, size_t size) {
        if (!server) return RIFT_ERROR_INVALID_HANDLE;
        reinterpret_cast<RiftServer_Internal*>(server)->Broadcast(data, size, /*reliable=*/true);
        return RIFT_SUCCESS;
    }

    // Explicit variants for benchmark/control:
    RiftResult rift_server_send_reliable(RiftServerHandle server, RiftClientId client_id, const uint8_t* data, size_t size) {
        if (!server) return RIFT_ERROR_INVALID_HANDLE;
        return reinterpret_cast<RiftServer_Internal*>(server)->Send(client_id, data, size, /*reliable=*/true);
    }

    RiftResult rift_server_send_unreliable(RiftServerHandle server, RiftClientId client_id, const uint8_t* data, size_t size) {
        if (!server) return RIFT_ERROR_INVALID_HANDLE;
        return reinterpret_cast<RiftServer_Internal*>(server)->Send(client_id, data, size, /*reliable=*/false);
    }

    RiftResult rift_server_broadcast_reliable(RiftServerHandle server, const uint8_t* data, size_t size) {
        if (!server) return RIFT_ERROR_INVALID_HANDLE;
        reinterpret_cast<RiftServer_Internal*>(server)->Broadcast(data, size, /*reliable=*/true);
        return RIFT_SUCCESS;
    }

    RiftResult rift_server_broadcast_unreliable(RiftServerHandle server, const uint8_t* data, size_t size) {
        if (!server) return RIFT_ERROR_INVALID_HANDLE;
        reinterpret_cast<RiftServer_Internal*>(server)->Broadcast(data, size, /*reliable=*/false);
        return RIFT_SUCCESS;
    }

#ifdef __cplusplus
} // extern "C"
#endif
