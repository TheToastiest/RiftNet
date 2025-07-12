RiftNet - Core Networking System

Primary responsibility: asynchronous UDP networking with reliability, encryption, compression, and system-level packet routing.

🔧 System Scope

Handle raw UDP I/O with support for async send/recv

Provide reliable message delivery over UDP

Support encryption protocols (AES256-GCM / ChaCha20-Poly1305)

Compress packets to reduce bandwidth usage (LZ4 / Zstd)

Interface with RiftEventBus to dispatch messages internally

✅ Objectives

1. Socket Layer (Platform-Agnostic)



2. Reliability Layer



3. Encryption Layer



4. Compression Layer



5. Application Layer Integration



6. Diagnostics & Debugging



🔐 Dependencies

Uses FlatBuffers via RiftSerializer (custom implementation to come later)

Should emit to RiftEventBus

Will interact with RiftCombat, RiftShardEngine, RiftAuth, etc.

Let me know when to proceed with the next system (RiftEventBus, RiftPhys, etc).

