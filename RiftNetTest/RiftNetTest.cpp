// File: RiftNetTest_MultiClient.cpp

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <sstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>

#include "include/KeyExchange.hpp"
#include "include/SecureChannel.hpp"
#include "include/ReliableTypes.hpp"
#include "include/ReliableConnectionState.hpp"
#include "include/UDPReliabilityProtocol.hpp"
#include "riftcompress.hpp"
#include "riftencrypt.hpp"

#pragma comment(lib, "ws2_32.lib")

using namespace RiftForged::Networking;

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

struct PeerConnectionState {
    SecureChannel secureChannel;
    Compressor compressor = Compressor(std::make_unique<LZ4Algorithm>());
    UDPReliabilityProtocol reliability;
    ReliableConnectionState connectionState;
    uint64_t txNonce = 1;
    uint64_t lastRxNonce = 0;
};

void RunClient(const std::string& peerName, const std::string& ip, uint16_t port) {
    WSADATA wsaData;
    SOCKET sock = INVALID_SOCKET;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[" << peerName << "] WSAStartup failed.\n";
        return;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[" << peerName << "] Socket creation failed.\n";
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);

    // Key exchange
    KeyExchange ke;
    const auto& pubKey = ke.GetLocalPublicKey();

    sendto(sock, reinterpret_cast<const char*>(pubKey.data()), pubKey.size(), 0,
        reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    std::cout << "[" << peerName << "] Sent public key.\n";

    std::vector<uint8_t> recvBuf(4096);
    sockaddr_in from{};
    int fromLen = sizeof(from);
    int received = recvfrom(sock, reinterpret_cast<char*>(recvBuf.data()), 32, 0,
        reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (received != 32) {
        std::cerr << "[" << peerName << "] Failed to receive server key.\n";
        closesocket(sock);
        WSACleanup();
        return;
    }

    KeyExchange::KeyBuffer serverKey;
    std::memcpy(serverKey.data(), recvBuf.data(), 32);
    ke.SetRemotePublicKey(serverKey);

    KeyExchange::KeyBuffer rxKey, txKey;
    if (!ke.DeriveSharedKey(false, rxKey, txKey)) {
        std::cerr << "[" << peerName << "] Shared key failure.\n";
        closesocket(sock);
        WSACleanup();
        return;
    }

    PeerConnectionState state;
    state.secureChannel.Initialize(rxKey, txKey);
    std::cout << "[" << peerName << "] Secure channel ready.\n";

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> sizeDist(1500, 3000);
    std::uniform_int_distribution<int> sleepDist(100, 300);

    while (true) {
        size_t size = sizeDist(rng);
        size_t msgCount = std::max<size_t>(1, size / 120);
        auto payload = generateSimulatedPayload(msgCount);
        auto compressed = state.compressor.compress(payload);

        auto packets = state.reliability.PrepareOutgoingPackets(
            state.connectionState,
            compressed.data(),
            static_cast<uint32_t>(compressed.size()),
            0x01  // Reliable flag
        );

        for (auto& pkt : packets) {
            auto encrypted = state.secureChannel.Encrypt(pkt, state.txNonce++);
            sendto(sock, reinterpret_cast<const char*>(encrypted.data()), encrypted.size(), 0,
                reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
            std::cout << "[" << peerName << "] Sent encrypted reliable packet.\n";
        }

        // Receive echo
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
        int got = recvfrom(sock, reinterpret_cast<char*>(recvBuf.data()), recvBuf.size(), 0,
            nullptr, nullptr);
        if (got > 0) {
            std::vector<uint8_t> decrypted;
            for (uint64_t n = state.lastRxNonce + 1; n <= state.lastRxNonce + 5; ++n) {
                if (state.secureChannel.Decrypt(std::vector<uint8_t>(recvBuf.begin(), recvBuf.begin() + got), decrypted, n)) {
                    state.lastRxNonce = n;
                    if (decrypted.size() < sizeof(ReliablePacketHeader)) continue;

                    ReliablePacketHeader recvHeader;
                    std::memcpy(&recvHeader, decrypted.data(), sizeof(ReliablePacketHeader));
                    std::vector<uint8_t> compressedData(decrypted.begin() + sizeof(ReliablePacketHeader), decrypted.end());

                    std::vector<uint8_t> outPayload;
                    if (state.reliability.ProcessIncomingHeader(state.connectionState, recvHeader, compressedData.data(), static_cast<uint16_t>(compressedData.size()), outPayload)) {
                        auto plain = state.compressor.decompress(outPayload);
                        std::string msg(plain.begin(), plain.end());
                        std::cout << "[" << peerName << "] Echo (type " << (int)recvHeader.packetType << ", nonce " << recvHeader.nonce << "):\n" << msg << "\n";
                    }
                    break;
                }
            }
        }

        // Retransmit
        state.reliability.ProcessRetransmissions(state.connectionState, [&](const std::vector<uint8_t>& pkt) {
            auto encrypted = state.secureChannel.Encrypt(pkt, state.txNonce++);
            sendto(sock, reinterpret_cast<const char*>(encrypted.data()), encrypted.size(), 0,
                reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
            std::cout << "[" << peerName << "] Retransmitted packet.\n";
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepDist(rng)));
    }

    closesocket(sock);
    WSACleanup();
}

int main() {
    const int numClients = 5000;
    const std::string ip = "127.0.0.1";
    const uint16_t port = 7777;

    std::vector<std::thread> clientThreads;

    for (int i = 0; i < numClients; ++i) {
        std::ostringstream oss;
        oss << "Client" << std::setw(2) << std::setfill('0') << i;
        std::string name = oss.str();  // extract to copyable std::string

        clientThreads.emplace_back([name, ip, port]() {
            RunClient(name, ip, port);
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(250)); // optional delay
    }


    // Optional: join all threads (if you ever plan on clean exit)
    for (auto& t : clientThreads) {
        if (t.joinable()) t.join();
    }

    return 0;
}