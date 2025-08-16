#include "pch.h"
#include "../../include/RiftNet/RiftClient.hpp"
#include "../core/riftnetio/RiftNetIO.hpp"
#include "../core/networkio/INetworkIOEvents.hpp"
#include "../protocol/packet/Packet.hpp"
#include "../protocol/packetfactory/PacketFactory.hpp"
#include "../core/connection/Connection.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>
#include <cassert>

// The internal C++ implementation of the client.
class RiftClient_Internal : public RiftNet::Networking::INetworkIOEvents {
public:
    explicit RiftClient_Internal(const RiftClientConfig* config)
        : m_config(*config)
        , m_networkIO(std::make_unique<RiftNet::Networking::WinSocketIO>())
        , m_running(false) {
    }

    ~RiftClient_Internal() {
        Disconnect();
#ifndef NDEBUG
        assert(!m_updateThread.joinable() && "update thread still joinable in destructor");
#endif
    }

    RiftResult Connect(const char* host_address, uint16_t port) {
        if (!host_address) return RIFT_ERROR_INVALID_PARAMETER;

        if (m_running.load(std::memory_order_acquire) || m_updateThread.joinable())
            return RIFT_ERROR_GENERIC;

        if (!m_networkIO->Init("0.0.0.0", 0, this)) {
            return RIFT_ERROR_SOCKET_BIND_FAILED;
        }

        // Logical connection to server (client side)
        m_serverConnection = std::make_unique<RiftNet::Protocol::Connection>(
            RiftNet::Networking::NetworkEndpoint(host_address, port),
            /*isServer=*/false
        );

        // Wire sends through WinSocketIO
        m_serverConnection->SetSendCallback([this](const RiftNet::Networking::NetworkEndpoint& ep,
            const std::vector<uint8_t>& data) {
                m_networkIO->SendData(ep, data.data(), static_cast<uint32_t>(data.size()));
            });

        // Deliver application payloads to the user's callback
        m_serverConnection->SetAppDataCallback([this](const uint8_t* data, uint32_t size) {
            RiftEvent appEvent{};
            appEvent.type = RIFT_EVENT_PACKET_RECEIVED;
            appEvent.data.packet.sender_id = 0;
            appEvent.data.packet.data = data;
            appEvent.data.packet.size = size;
            m_config.event_callback(&appEvent, m_config.user_data);
            });

        if (!m_networkIO->Start()) {
            m_serverConnection.reset();
            return RIFT_ERROR_GENERIC;
        }

        m_running.store(true, std::memory_order_release);

        // Start the update worker (auto-joined by jthread in dtor)
        m_updateThread = std::jthread([this](std::stop_token st) { Update(st); });

        // Notify connected
        {
            RiftEvent connectedEvent{};
            connectedEvent.type = RIFT_EVENT_CLIENT_CONNECTED;
            m_config.event_callback(&connectedEvent, m_config.user_data);
        }

        // Prime the reliability path with a tiny reliable "HELLO".
        // Produces an early ACK pair so RTT sampling has data immediately.
        static const uint8_t hello[5] = { 'R','F','N','T', 0x01 }; // magic + version byte
        m_serverConnection->SendApplicationData(hello, static_cast<uint32_t>(sizeof(hello)), /*reliable=*/true);

        return RIFT_SUCCESS;
    }

    void Disconnect() {
        m_running.store(false, std::memory_order_release);

        if (m_updateThread.joinable()) {
            const bool self_call = (m_updateThread.get_id() == std::this_thread::get_id());
            m_updateThread.request_stop();
            if (m_networkIO) m_networkIO->Stop();
            if (!self_call) m_updateThread.join();
        }
        else {
            if (m_networkIO) m_networkIO->Stop();
        }

        m_serverConnection.reset();

        // Emit a disconnect event so callers can clean up UI/state.
        RiftEvent e{};
        e.type = RIFT_EVENT_CLIENT_DISCONNECTED;
        m_config.event_callback(&e, m_config.user_data);
    }

    RiftResult Send(const uint8_t* data, size_t size, bool reliable) {
        if (!data || size == 0) return RIFT_ERROR_INVALID_PARAMETER;
        if (!m_running.load(std::memory_order_acquire) || !m_serverConnection)
            return RIFT_ERROR_CONNECTION_FAILED;

        m_serverConnection->SendApplicationData(data, static_cast<uint32_t>(size), reliable);
        return RIFT_SUCCESS;
    }

    // =========================
    // INetworkIOEvents
    // =========================
    void OnRawDataReceived(const RiftNet::Networking::NetworkEndpoint& /*sender*/,
        const uint8_t* data,
        uint32_t size,
        RiftNet::Networking::OverlappedIOContext* /*context*/) override
    {
        if (!m_running.load(std::memory_order_acquire)) return;
        if (m_serverConnection) {
            m_serverConnection->ProcessIncomingRawPacket(data, size);
        }
    }

    void OnSendCompleted(RiftNet::Networking::OverlappedIOContext* /*context*/,
        bool /*success*/,
        uint32_t /*bytesSent*/) override {
    }

    void OnNetworkError(const std::string& /*errorMessage*/, int /*errorCode*/) override {
        // Optional: surface a network error event here if desired.
    }

private:
    void Update(std::stop_token st) {
        using namespace std::chrono_literals;

        const auto kIdleTimeout = std::chrono::seconds(30);  // was 10s; give RTT time to form
        const auto kTick = 100ms;
        const auto kKeepalive = 1000ms;                    // send a small reliable noop each second

        auto lastKeepalive = std::chrono::steady_clock::now();

        while (!st.stop_requested()) {
            std::this_thread::sleep_for(kTick);
            if (!m_running.load(std::memory_order_acquire)) break;

            if (m_serverConnection) {
                const auto now = std::chrono::steady_clock::now();
                m_serverConnection->Update(now);

                // Lightweight reliable keepalive so ACKs keep flowing (feeds RTT sampler).
                if (now - lastKeepalive >= kKeepalive) {
                    static const uint8_t noop[1] = { 0x00 };
                    m_serverConnection->SendApplicationData(noop, 1u, /*reliable=*/true);
                    lastKeepalive = now;
                }

                // Idle timeout based on lack of inbound/ACK activity.
                if (m_serverConnection->IsTimedOut(now, kIdleTimeout)) {
                    RiftEvent e{};
                    e.type = RIFT_EVENT_CLIENT_DISCONNECTED;
                    m_config.event_callback(&e, m_config.user_data);
                    m_running.store(false, std::memory_order_release);
                    break;
                }
            }
        }
    }

private:
    RiftClientConfig m_config;

    std::unique_ptr<RiftNet::Networking::INetworkIO> m_networkIO;
    std::unique_ptr<RiftNet::Protocol::Connection>  m_serverConnection;

    std::atomic<bool> m_running;
    std::jthread      m_updateThread; // keep last
};

// =====================================================================================
// C-Style API Implementation
// =====================================================================================
#ifdef __cplusplus
extern "C" {
#endif

    RiftClientHandle rift_client_create(const RiftClientConfig* config) {
        if (!config || !config->event_callback) {
            return nullptr;
        }
        try {
            return reinterpret_cast<RiftClientHandle>(new RiftClient_Internal(config));
        }
        catch (...) {
            return nullptr;
        }
    }

    void rift_client_destroy(RiftClientHandle client) {
        if (client) {
            delete reinterpret_cast<RiftClient_Internal*>(client);
        }
    }

    RiftResult rift_client_connect(RiftClientHandle client, const char* host_address, uint16_t port) {
        if (!client) return RIFT_ERROR_INVALID_HANDLE;
        return reinterpret_cast<RiftClient_Internal*>(client)->Connect(host_address, port);
    }

    void rift_client_disconnect(RiftClientHandle client) {
        if (client) {
            reinterpret_cast<RiftClient_Internal*>(client)->Disconnect();
        }
    }

    // Default to RELIABLE to feed RTT sampling / latency bench.
    RiftResult rift_client_send(RiftClientHandle client, const uint8_t* data, size_t size) {
        if (!client) return RIFT_ERROR_INVALID_HANDLE;
        return reinterpret_cast<RiftClient_Internal*>(client)->Send(data, size, /*reliable=*/true);
    }

    // Explicit variants for clarity/bench control.
    RiftResult rift_client_send_reliable(RiftClientHandle client, const uint8_t* data, size_t size) {
        if (!client) return RIFT_ERROR_INVALID_HANDLE;
        return reinterpret_cast<RiftClient_Internal*>(client)->Send(data, size, /*reliable=*/true);
    }

    RiftResult rift_client_send_unreliable(RiftClientHandle client, const uint8_t* data, size_t size) {
        if (!client) return RIFT_ERROR_INVALID_HANDLE;
        return reinterpret_cast<RiftClient_Internal*>(client)->Send(data, size, /*reliable=*/false);
    }

#ifdef __cplusplus
} // extern "C"
#endif
