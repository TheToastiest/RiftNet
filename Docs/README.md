# RiftNet — High-Performance Secure UDP Networking Library

> **“Sync is not interpolation. It is truth moved across time.”**  
> — *RiftForged Development Doctrine*

---

**RiftNet** is a modular, server-authoritative, C++ networking library designed for real-time multiplayer games, simulations, and virtual worlds.

It was built from the ground up as a **philosophical counterpunch** to deceptive networking systems. No fake sync. No client illusions. Just state, serialized, encrypted, and transmitted with intention.

---

## 🔧 Core Features

- 🔒 **Encryption First**: Ephemeral key exchange (X25519) and secure channels using ChaCha20-Poly1305 or AES256-GCM, powered by libsodium.
- 📦 **Optional Compression**: Pluggable compression (LZ4 or Zstd) per connection or message type.
- 🔁 **Reliable UDP**: Custom protocol with sequence tracking, RTT estimation (RFC 6298), and congestion awareness.
- 🧠 **Designed for Authority**: Built for authoritative servers that simulate truth, not interpolate fiction.
- 🧩 **Layered Architecture**:
  - `RiftNet` = Socket + Protocol
  - `RiftHandler` = Serialization + Dispatch
  - `RiftEventBus` = Message Routing (WIP)

---

## ⚙️ Technical Overview

- ✅ Windows IOCP-based async UDP sockets
- 🧩 Modular architecture — drop in/out encryption, compression, message layers
- 🗝 Automatic key exchange + nonce tracking
- 📬 ReliablePacketHeader structure for resend logic
- 📉 RTT/congestion tracking (inspired by RFC 6298 + Karn’s Algorithm)
- 🔐 Compression/encryption can be toggled per message or channel
- 🧵 Fully multithreaded — built for performance and truth at scale

---

## 🔗 Dependencies

RiftNet depends on the following libraries, which are also MIT-licensed and available publicly:

- [RiftEncrypt](https://github.com/TheToastiest/RiftEncrypt) – Secure channel, key exchange, encryption logic (libsodium-powered)
- [RiftCompress](https://github.com/TheToastiest/RiftCompress) – Optional compression via LZ4 or Zstd

These are standalone libraries but tightly integrated into RiftNet's pipeline.

---

## 🚀 Getting Started

### 1. Include the SDK

- Add `RiftNet/include/` to your project’s include path
- Link against `RiftNet.lib` (or build as DLL)

### 2. Use the Connection class

```cpp
Connection conn;
conn.initiateKeyExchange(); // Async X25519 key exchange

conn.sendReliable(jsonMessage, [](bool success) {
    if (success) std::cout << "Sent!" << std::endl;
});
