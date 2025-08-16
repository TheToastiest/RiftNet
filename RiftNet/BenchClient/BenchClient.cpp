#include "../include/RiftNet/RiftClient.hpp" // The only RiftNet header the user needs for the server.
#include "../utilities/logger/Logger.hpp"     // Assuming the logger is available for the test app.
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <numeric> 

// =====================================================================================
// Shared State for the Client
// =====================================================================================

struct ClientState {
    RiftClientHandle handle = nullptr;
    std::chrono::steady_clock::time_point last_ping_sent_time;
    bool connected = false;
    std::vector<double> rtt_samples_us; // Store RTT samples in microseconds
};

// =====================================================================================
// Event Callback Implementation
// =====================================================================================

void ClientEventCallback(const RiftEvent* event, void* user_data)
{
    ClientState* state = static_cast<ClientState*>(user_data);

    switch (event->type)
    {
    case RIFT_EVENT_CLIENT_CONNECTED:
        RF_NETWORK_INFO("Client: Successfully connected to server.");
        state->connected = true;
        break;

    case RIFT_EVENT_CLIENT_DISCONNECTED:
        RF_NETWORK_INFO("Client: Disconnected from server.");
        state->connected = false;
        break;

    case RIFT_EVENT_PACKET_RECEIVED:
    {
        auto now = std::chrono::steady_clock::now();
        auto rtt = std::chrono::duration_cast<std::chrono::microseconds>(now - state->last_ping_sent_time);
        state->rtt_samples_us.push_back(static_cast<double>(rtt.count()));
        break;
    }

    default:
        break;
    }
}

// =====================================================================================
// Main Application Entry Point
// =====================================================================================

int main()
{
    // 1. Initialize Logger
    RiftNet::Logging::Logger::Init();
    RF_NETWORK_INFO("--- RiftNet Latency Benchmark Client ---");

    // 2. Setup Client State and Configuration
    ClientState state;

    RiftClientConfig config = {};
    config.event_callback = ClientEventCallback;
    config.user_data = &state;

    // 3. Create and Connect the Client
    state.handle = rift_client_create(&config);
    if (!state.handle) {
        RF_NETWORK_CRITICAL("Failed to create RiftNet client.");
        return 1;
    }

    RiftResult result = rift_client_connect(state.handle, "127.0.0.1", 8888);
    if (result != RIFT_SUCCESS) {
        RF_NETWORK_CRITICAL("Failed to connect to server. Error code: {}", static_cast<int>(result));
        rift_client_destroy(state.handle);
        return 1;
    }

    // Wait for connection to be established
    RF_NETWORK_INFO("Connecting...");
    while (!state.connected) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 4. Run the Latency Test Loop
    RF_NETWORK_INFO("Connection established. Starting latency test...");
    const int pings_to_send = 1000;
    state.rtt_samples_us.reserve(pings_to_send);

    for (int i = 0; i < pings_to_send; ++i) {
        if (!state.connected) {
            RF_NETWORK_WARN("Disconnected during test.");
            break;
        }

        uint8_t payload[8]; // Payload will contain the send time
        auto now = std::chrono::steady_clock::now();
        state.last_ping_sent_time = now;

        // We don't need to send the actual time for an echo test, just trigger a response.
        rift_client_send(state.handle, payload, sizeof(payload));

        // Send pings at a regular interval
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Give last packets time to arrive
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 5. Stop and Destroy the Client
    RF_NETWORK_INFO("Latency test finished. Disconnecting...");
    rift_client_disconnect(state.handle);
    rift_client_destroy(state.handle);

    // 6. Analyze and Save Results to CSV
    if (state.rtt_samples_us.empty()) {
        RF_NETWORK_ERROR("No RTT samples were collected. Cannot generate report.");
        return 1;
    }

    std::ofstream csv_file("latency_results.csv");
    if (!csv_file.is_open()) {
        RF_NETWORK_ERROR("Failed to open latency_results.csv for writing.");
        return 1;
    }

    // Write header
    csv_file << "PingNumber,RTT_us\n";
    // Write data
    for (size_t i = 0; i < state.rtt_samples_us.size(); ++i) {
        csv_file << i + 1 << "," << state.rtt_samples_us[i] << "\n";
    }
    csv_file.close();

    double sum = std::accumulate(state.rtt_samples_us.begin(), state.rtt_samples_us.end(), 0.0);
    double avg_rtt_us = sum / state.rtt_samples_us.size();

    RF_NETWORK_INFO("-----------------------------------------");
    RF_NETWORK_INFO("Test Complete. Results saved to latency_results.csv");
    RF_NETWORK_INFO("Samples Collected: {}", state.rtt_samples_us.size());
    RF_NETWORK_INFO("Average Latency: {:.3f} ms", avg_rtt_us / 1000.0);
    RF_NETWORK_INFO("-----------------------------------------");

    return 0;
}
