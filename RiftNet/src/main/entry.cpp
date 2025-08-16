// RiftNet is a C++ library for a UDP-based networking solution designed for games and real-time applications. It provides a simple API for creating servers and clients, handling connections, and sending/receiving data efficiently.

#include "pch.h"
#include "../core/networkio/INetworkIO.hpp"
#include "../core/networkio/iocontext.hpp"
#include "../core/networkio/NetworkEndpoint.hpp"
#include "../core/riftnetio/RiftNetIO.hpp"
#include "../../utilities/logger/Logger.hpp"

using namespace RiftNet;
using namespace RiftNet::Logging;
//using namespace RiftNet::Networking;
//using namespace RiftNet::Protocol;
class TestEventHandler : public RiftNet::Networking::INetworkIOEvents {
public:
    virtual ~TestEventHandler() = default;

    void OnRawDataReceived(const RiftNet::Networking::NetworkEndpoint& sender,
        const uint8_t* data,
        uint32_t size,
        RiftNet::Networking::OverlappedIOContext* context) override
    {
        // For this test, we just log that we received something.
        // In the real server, this is where you would decrypt, decompress,
        // and process the packet.
        RF_NETWORK_INFO("Received {} bytes from {}", size, sender.ToString());
    }

    void OnSendCompleted(RiftNet::Networking::OverlappedIOContext* context,
        bool success,
        uint32_t bytesSent) override
    {
        if (success) {
            RF_NETWORK_INFO("Successfully sent {} bytes to {}", bytesSent, context->endpoint.ToString());
        }
        else {
            RF_NETWORK_WARN("Send operation to {} failed.", context->endpoint.ToString());
        }
    }

    void OnNetworkError(const std::string& errorMessage, int errorCode) override
    {
        // This callback is for higher-level errors. Internal errors are logged directly.
        RF_NETWORK_ERROR("A network event error occurred: {} (Code: {})", errorMessage, errorCode);
    }
};


int main() {
    // 1. Initialize the Logger
    // This assumes you have a Logger.cpp file with an Init implementation.
    RiftNet::Logging::Logger::Init();
    RF_NETWORK_INFO("RiftNet Server Test Starting...");

    // 2. Create the necessary components
    TestEventHandler eventHandler;
    auto networkIO = std::make_unique<RiftNet::Networking::WinSocketIO>();

    // 3. Configure and Initialize the Server's I/O Layer
    const std::string listenIp = "127.0.0.1";
    const uint16_t listenPort = 8888;

    if (!networkIO->Init(listenIp, listenPort, &eventHandler)) {
        RF_NETWORK_CRITICAL("Failed to initialize the network I/O layer. Shutting down.");
        return 1;
    }

    // 4. Start the network I/O threads
    if (!networkIO->Start()) {
        RF_NETWORK_CRITICAL("Failed to start the network I/O layer. Shutting down.");
        return 1;
    }

    RF_NETWORK_INFO("Server is now running and listening on {}:{}", listenIp, listenPort);
    std::cout << "\nPress ENTER to stop the server...\n" << std::endl;

    // 5. Keep the main thread alive
    // In a real application, this would be your main game loop or application loop.
    std::cin.get();

    // 6. Stop the network I/O layer and clean up
    RF_NETWORK_INFO("Shutdown signal received. Stopping server...");
    networkIO->Stop();

    RF_NETWORK_INFO("Server has stopped. Exiting.");

    return 0;
}
