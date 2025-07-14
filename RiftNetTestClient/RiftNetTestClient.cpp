// File: RiftNetTest_MultiClient.cpp

#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "../RiftNet/include/core/Connection.hpp"
#include "../RiftNet/include/core/NetworkTypes.hpp"
#include "../RiftNet/include/core//Logger.hpp"

#pragma comment(lib, "ws2_32.lib")

using namespace RiftForged::Networking;
using RiftForged::Logging::Logger;

std::vector<uint8_t> generateSimulatedPayload(size_t count) {
    std::ostringstream oss;
    for (size_t i = 0; i < count; ++i) {
        oss << R"({"type":"PlayerAction","playerId":)" << (i % 32)
            << R"(,"action":"move","x":)" << (100 + (i % 10))
            << R"(,"y":)" << (200 + (i % 10))
            << R"(,"z":)" << (300 + (i % 10))
            << R"(,"timestamp":)" << (i * 100)
            << "}\n";
    }
    std::string result = oss.str();
    return std::vector<uint8_t>(result.begin(), result.end());
}

void RunClient(const std::string& name, const std::string& ip, uint16_t port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[" << name << "] WSAStartup failed.\n";
        return;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[" << name << "] Socket creation failed.\n";
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);

    NetworkEndpoint serverEndpoint(ip, port);
    Connection client(serverEndpoint);

    client.SetSendCallback([&](const NetworkEndpoint&, const std::vector<uint8_t>& data) {
        sendto(sock, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0,
            reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
        });

    // Key exchange
    const auto& pubKey = client.GetLocalPublicKey();
    sendto(sock, reinterpret_cast<const char*>(pubKey.data()), static_cast<int>(pubKey.size()), 0,
        reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));

    std::vector<uint8_t> recvBuf(4096);
    sockaddr_in from{};
    int fromLen = sizeof(from);
    int received = recvfrom(sock, reinterpret_cast<char*>(recvBuf.data()), 32, 0,
        reinterpret_cast<sockaddr*>(&from), &fromLen);

    if (received == 32) {
        KeyExchange::KeyBuffer serverKey;
        std::memcpy(serverKey.data(), recvBuf.data(), 32);
        client.PerformKeyExchange(serverKey, false);
    }
    else {
        std::cerr << "[" << name << "] Failed key exchange.\n";
        closesocket(sock);
        WSACleanup();
        return;
    }

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> sizeDist(1500, 3000);
    std::uniform_int_distribution<int> sleepDist(100, 300);

    while (true) {
        size_t size = sizeDist(rng);
        size_t msgCount = std::max<size_t>(1, size / 120);
        auto payload = generateSimulatedPayload(msgCount);

        client.SendReliable(payload, 0x01);

        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
        int got = recvfrom(sock, reinterpret_cast<char*>(recvBuf.data()), static_cast<int>(recvBuf.size()), 0, nullptr, nullptr);
        if (got > 0) {
            client.HandleRawPacket(std::vector<uint8_t>(recvBuf.begin(), recvBuf.begin() + got));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepDist(rng)));
    }

    closesocket(sock);
    WSACleanup();
}

int main() {
    RiftForged::Logging::Logger::Init();  // ✅ Logger must be initialized first

    const int numClients = 50;
    const std::string ip = "127.0.0.1";
    const uint16_t port = 7777;

    std::vector<std::thread> clientThreads;

    for (int i = 0; i < numClients; ++i) {
        std::ostringstream oss;
        oss << "Client" << std::setw(2) << std::setfill('0') << i;
        std::string clientName = oss.str();

        clientThreads.emplace_back([clientName, ip, port]() {
            RunClient(clientName, ip, port);
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto& t : clientThreads)
        if (t.joinable()) t.join();

    return 0;
}
