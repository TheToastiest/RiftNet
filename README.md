# RiftNet — High-Performance Secure UDP Networking Library

**RiftNet** is a fully modular, C++-based networking library built for real-time, high-performance multiplayer games and simulations. It includes:

- 🔒 **Built-in Encryption** with libsodium (X25519 + ChaCha20-Poly1305 or AES256-GCM)
- 📦 **Optional Compression** via LZ4 or Zstd
- 🔁 **Reliable UDP** with retransmission, RTT estimation, and congestion control
- 🧩 Clean separation of `RiftNet` (Network), `RiftHandler` (Message Parsing), and other layers
- 🧠 Built for games, simulations, and real-time systems needing secure sync at scale

---

## ⚙️ Features

- UDP-based socket engine using Windows IOCP
- Automatic key exchange and secure channel setup
- Optional compression layer pluggable at runtime
- Custom packet header format supporting multiple message types
- Reliable packet queue and resend logic with sequence tracking
- Future support for EventBus-based routing and encryption rules per channel/message

---

## 🚀 Getting Started

### 1. Include the SDK

- Add `RiftNet/include/` to your project include path
- Link against `RiftNet.lib` (or use DLL build)

### 2. Create a `Connection` and start sending encrypted, compressed messages:

```cpp
Connection conn;
conn.initiateKeyExchange();
conn.sendReliable(jsonMessage, [](bool success) {
    if (success) std::cout << "Sent!" << std::endl;
});
