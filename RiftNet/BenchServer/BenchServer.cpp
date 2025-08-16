#include "../include/RiftNet/RiftServer.hpp"    // RiftNet server C API
#include "../utilities/logger/Logger.hpp"        // Your logger
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

// ===============================
// Connected client tracking
// ===============================
namespace {
    std::mutex g_clients_mtx;
    std::unordered_set<RiftClientId> g_clients;
}

// ===============================
// Event Callback
// ===============================
void ServerEventCallback(const RiftEvent* event, void* user_data)
{
    RiftServerHandle serverHandle = *static_cast<RiftServerHandle*>(user_data);

    switch (event->type) {
    case RIFT_EVENT_CLIENT_CONNECTED: {
        RF_NETWORK_INFO("Server: Client connected with ID {}", event->data.client_id);
        std::lock_guard<std::mutex> lock(g_clients_mtx);
        g_clients.insert(event->data.client_id);
        break;
    }
    case RIFT_EVENT_CLIENT_DISCONNECTED: {
        RF_NETWORK_INFO("Server: Client with ID {} disconnected.", event->data.client_id);
        std::lock_guard<std::mutex> lock(g_clients_mtx);
        g_clients.erase(event->data.client_id);
        break;
    }
    case RIFT_EVENT_PACKET_RECEIVED: {
        // Echo back immediately — **reliable** to ensure ACK piggyback and RTT samples.
        const auto size = static_cast<size_t>(event->data.packet.size);
        RF_NETWORK_TRACE("Server: Echoing {} bytes back to client ID {}.",
            size, event->data.packet.sender_id);

        // If your library exposes rift_server_send_reliable, prefer that here.
        // Otherwise, rift_server_send defaults to reliable in the refactor.
        RiftResult rc = rift_server_send(
            serverHandle,
            event->data.packet.sender_id,
            event->data.packet.data,
            size
        );
        if (rc != RIFT_SUCCESS) {
            RF_NETWORK_ERROR("Server: echo send failed for client {} (rc={})",
                event->data.packet.sender_id, static_cast<int>(rc));
        }
        break;
    }
    case RIFT_EVENT_SERVER_START:
        RF_NETWORK_INFO("Server: Successfully started and listening.");
        break;
    case RIFT_EVENT_SERVER_STOP:
        RF_NETWORK_INFO("Server: Stopped.");
        break;
    default:
        break;
    }
}

// ===============================
// Main
// ===============================
int main()
{
    using namespace std::chrono_literals;

    // 1) Logger
    RiftNet::Logging::Logger::Init();
    RF_NETWORK_INFO("--- RiftNet Latency Benchmark Server ---");

    // 2) Config
    RiftServerHandle serverHandle = nullptr;

    RiftServerConfig config{};
    config.host_address = "127.0.0.1";
    config.port = 8888;
    config.event_callback = ServerEventCallback;
    config.user_data = &serverHandle; // callback can call API via this

    // 3) Create + start
    serverHandle = rift_server_create(&config);
    if (!serverHandle) {
        RF_NETWORK_CRITICAL("Failed to create RiftNet server.");
        return 1;
    }
    RF_NETWORK_INFO("RiftNet server handle created.");

    const RiftResult start_rc = rift_server_start(serverHandle);
    if (start_rc != RIFT_SUCCESS) {
        RF_NETWORK_CRITICAL("Failed to start RiftNet server. Error code: {}", static_cast<int>(start_rc));
        rift_server_destroy(serverHandle);
        return 1;
    }

    // 4) Heartbeat thread: force periodic server->client reliable packets
    //    so any pending ACKs get piggybacked (feeds client RTT sampler).
    std::jthread heartbeat([&](std::stop_token st) {
        static const uint8_t hb[1] = { 0xF0 };
        while (!st.stop_requested()) {
            std::this_thread::sleep_for(500ms);

            std::vector<RiftClientId> snapshot;
            {
                std::lock_guard<std::mutex> lock(g_clients_mtx);
                snapshot.reserve(g_clients.size());
                for (auto id : g_clients) snapshot.push_back(id);
            }

            if (snapshot.empty()) continue;

            for (auto id : snapshot) {
                const RiftResult rc = rift_server_send(serverHandle, id, hb, sizeof(hb)); // reliable
                if (rc != RIFT_SUCCESS) {
                    RF_NETWORK_WARN("Heartbeat send failed to client {} (rc={})",
                        id, static_cast<int>(rc));
                }
            }
            RF_NETWORK_TRACE("Heartbeat sent to {} clients", snapshot.size());
        }
        });

    // 5) Run until ENTER
    std::cout << "\nEcho server running on 127.0.0.1:8888. Press ENTER to stop.\n" << std::endl;
    std::cin.get();

    // 6) Shutdown
    RF_NETWORK_INFO("Shutdown signal received. Stopping server...");
    heartbeat.request_stop();
    rift_server_stop(serverHandle);
    rift_server_destroy(serverHandle);
    RF_NETWORK_INFO("Server shut down cleanly.");

    return 0;
}
