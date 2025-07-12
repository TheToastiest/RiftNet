#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "include/KeyExchange.hpp"
#include "include/SecureChannel.hpp"
#include "riftcompress.hpp"
#include "riftencrypt.hpp"

#pragma comment(lib, "ws2_32.lib")

using namespace RiftForged::Networking;

void print_key(const std::string& label, const KeyExchange::KeyBuffer& key) {
    std::cout << label << ": ";
    for (unsigned char byte : key) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(byte);
    }
    std::cout << std::dec << std::endl;
}

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

int main() {
    WSADATA wsaData;
    SOCKET sock = INVALID_SOCKET;

    const char* SERVER_IP = "127.0.0.1";
    const uint16_t SERVER_PORT = 7777;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[Client] WSAStartup failed.\n";
        return 1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[Client] Socket creation failed.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    // === Key Exchange ===
    KeyExchange clientKE;
    const auto& clientPubKey = clientKE.GetLocalPublicKey();

    sendto(sock, reinterpret_cast<const char*>(clientPubKey.data()), clientPubKey.size(), 0,
           reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    std::cout << "[Client] Sent public key to server.\n";

    std::vector<uint8_t> serverKeyBuf(32);
    sockaddr_in from{};
    int fromLen = sizeof(from);
    int bytesReceived = recvfrom(sock, reinterpret_cast<char*>(serverKeyBuf.data()), 32, 0,
                                 reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (bytesReceived != 32) {
        std::cerr << "[Client] Failed to receive server public key.\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    KeyExchange::KeyBuffer serverPubKey;
    std::memcpy(serverPubKey.data(), serverKeyBuf.data(), 32);
    clientKE.SetRemotePublicKey(serverPubKey);

    KeyExchange::KeyBuffer rxKey, txKey;
    if (!clientKE.DeriveSharedKey(false, rxKey, txKey)) {
        std::cerr << "[Client] Shared key derivation failed.\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    SecureChannel channel;
    channel.Initialize(rxKey, txKey);
    Compressor compressor(std::make_unique<LZ4Algorithm>());

    uint64_t nonce = 1;

    std::cout << "[Client] Secure channel ready. Starting packet loop...\n";

    // === Random Packet Loop ===
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> sizeDist(1500, 3000);
    std::uniform_int_distribution<int> sleepDist(100, 300); // milliseconds

    while (true) {
        size_t packetSize = sizeDist(rng);
        size_t messageCount = std::max<size_t>(1, packetSize / 120); // ~110–140 bytes per structured payload
        std::vector<uint8_t> payload = generateSimulatedPayload(messageCount);

        std::vector<uint8_t> compressed = compressor.compress(payload);
        if (compressed.empty()) {
            std::cerr << "[Client] Compression failed.\n";
            continue;
        }

        std::vector<uint8_t> encrypted = channel.Encrypt(compressed, nonce++);
        if (encrypted.empty()) {
            std::cerr << "[Client] Encryption failed.\n";
            continue;
        }

        int sent = sendto(sock, reinterpret_cast<const char*>(encrypted.data()), encrypted.size(), 0,
                          reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
        if (sent == SOCKET_ERROR) {
            std::cerr << "[Client] Send failed: " << WSAGetLastError() << "\n";
        } else {
            std::cout << "[Client] Sent packet (" << packetSize << " bytes raw, "
                      << encrypted.size() << " bytes encrypted).\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepDist(rng)));
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
