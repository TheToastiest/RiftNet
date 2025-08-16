# RiftNet
RiftNet is a high-performance, secure, UDP-based networking library designed for real-time applications like multiplayer games. It provides a simple C-style API for creating clients and servers, focusing on low latency, security, and ease of use.

At its core, RiftNet is built to be a "lethal weapon" for game developers, offering sub-3ms localhost round-trip times with built-in encryption and compression, solving common networking challenges right out of the box.

# Features
High Performance: Optimized for low-latency communication, making it ideal for fast-paced games (FPS, RTS, etc.).

Secure by Default: All communications are encrypted using a modern cryptographic handshake, protecting against common network attacks like packet sniffing.

Built-in Compression: Payloads are compressed using LZ4 to reduce bandwidth usage.

Simple C API: A clean, straightforward extern "C" API ensures maximum compatibility and ease of integration with various languages and engines.

Event-Driven: A single callback function is used to handle all network events (connections, disconnections, packet arrivals) in a non-blocking manner.

# Core Concepts
Opaque Handles
RiftNet operates on opaque handles (RiftServerHandle, RiftClientHandle). You never need to know the internal details of these structures. You create them, use them with API functions, and then destroy them when you're done.

Event Callback
All network activity is reported through a single callback function that you provide during configuration. This function receives a RiftEvent struct which describes the event type and contains relevant data, such as the client ID or the received packet.
```
void MyEventCallback(const RiftEvent* event, void* user_data)
{
    switch (event->type)
    {
        case RIFT_EVENT_CLIENT_CONNECTED:
            // Handle new connection...
            break;
        // ... other cases
    }
}
```
# API Overview
Common Types & Structs
These types are defined in RiftCommon.hpp and are used by both the client and server.

RiftServerHandle / RiftClientHandle: Opaque pointers to server and client instances.

RiftClientId: A uint64_t that uniquely identifies a client on the server.

RiftResult: An enum for function return codes (e.g., RIFT_SUCCESS).

RiftEventType: An enum for all possible event types (e.g., RIFT_EVENT_CLIENT_CONNECTED).

RiftEvent
The main struct passed to your callback.
```
typedef struct RiftEvent {
    RiftEventType type;
    union {
        RiftPacket   packet;    // For RIFT_EVENT_PACKET_RECEIVED
        RiftClientId client_id; // For connect/disconnect events
    } data;
} RiftEvent;
```
RiftPacket
Contains the data from a received packet.
```
typedef struct RiftPacket {
    const uint8_t* data;
    size_t         size;
    RiftClientId   sender_id; // ID of the client who sent it
} RiftPacket;
```
Server API (RiftServer.hpp)
Configuration
```
typedef struct RiftServerConfig {
    const char* host_address;
    uint16_t          port;
    RiftEventCallback event_callback;
    void* user_data; // Optional pointer passed to your callback
} RiftServerConfig;
```
# Functions

```
RiftServerHandle rift_server_create(const RiftServerConfig* config)
```
Creates a new server instance. Returns a handle or NULL on failure.

```
void rift_server_destroy(RiftServerHandle server)
```
Frees all resources associated with a server instance.

```
RiftResult rift_server_start(RiftServerHandle server)
```
Starts the server, which begins listening for connections.

```
void rift_server_stop(RiftServerHandle server)
```
Stops the server.

```
RiftResult rift_server_send(RiftServerHandle server, RiftClientId client_id, const uint8_t* data, size_t size)
```
Sends a packet to a single, specific client.

RiftResult rift_server_broadcast(RiftServerHandle server, const uint8_t* data, size_t size)

Sends a packet to all currently connected clients.

# Client API (RiftClient.hpp)
Configuration
```
typedef struct RiftClientConfig {
    RiftEventCallback event_callback;
    void* user_data;
} RiftClientConfig;
```
#Functions

```
RiftClientHandle rift_client_create(const RiftClientConfig* config)
```
Creates a new client instance. Returns a handle or NULL on failure.

```
void rift_client_destroy(RiftClientHandle client)
```
Frees all resources associated with a client instance.

```
RiftResult rift_client_connect(RiftClientHandle client, const char* host_address, uint16_t port)
```
Initiates a connection to a server.

```
void rift_client_disconnect(RiftClientHandle client)
```
Disconnects from the server.

```
RiftResult rift_client_send(RiftClientHandle client, const uint8_t* data, size_t size)
```
Sends a packet to the server.

# Quick Start
Server Example
```
#include <RiftNet/RiftNet.hpp>
#include <iostream>
#include <thread>

// Event handler for the server
void ServerEventCallback(const RiftEvent* event, void* user_data)
{
    RiftServerHandle server = *static_cast<RiftServerHandle*>(user_data);

    if (event->type == RIFT_EVENT_PACKET_RECEIVED) {
        std::cout << "Server received a packet, echoing it back." << std::endl;
        // Echo the packet back to the sender
        rift_server_send(server, event->data.packet.sender_id, 
                         event->data.packet.data, event->data.packet.size);
    } else if (event->type == RIFT_EVENT_CLIENT_CONNECTED) {
        std::cout << "Client connected with ID: " << event->data.client_id << std::endl;
    }
}

int main()
{
    RiftServerHandle server_handle = nullptr;

    RiftServerConfig config{};
    config.host_address = "127.0.0.1";
    config.port = 8888;
    config.event_callback = ServerEventCallback;
    config.user_data = &server_handle; // Allow callback to access the handle

    server_handle = rift_server_create(&config);
    if (!server_handle) return 1;

    rift_server_start(server_handle);

    std::cout << "Server running. Press ENTER to stop." << std::endl;
    std::cin.get();

    rift_server_stop(server_handle);
    rift_server_destroy(server_handle);

    return 0;
}
```

# Client Example
```
#include <RiftNet/RiftNet.hpp>
#include <iostream>
#include <thread>
#include <string>

struct ClientState {
    bool connected = false;
};

// Event handler for the client
void ClientEventCallback(const RiftEvent* event, void* user_data)
{
    ClientState* state = static_cast<ClientState*>(user_data);
    if (event->type == RIFT_EVENT_CLIENT_CONNECTED) {
        std::cout << "Client connected to server." << std::endl;
        state->connected = true;
    } else if (event->type == RIFT_EVENT_PACKET_RECEIVED) {
        std::cout << "Client received echo from server." << std::endl;
    }
}

int main()
{
    using namespace std::chrono_literals;

    ClientState state;
    RiftClientConfig config{};
    config.event_callback = ClientEventCallback;
    config.user_data = &state;

    RiftClientHandle client_handle = rift_client_create(&config);
    if (!client_handle) return 1;

    rift_client_connect(client_handle, "127.0.0.1", 8888);

    // Wait for connection
    while (!state.connected) {
        std::this_thread::sleep_for(10ms);
    }
    
    // Send a message
    std::string message = "Hello, RiftNet!";
    rift_client_send(client_handle, (const uint8_t*)message.c_str(), message.length());

    // Wait a moment to receive the echo
    std::this_thread::sleep_for(1s);

    rift_client_disconnect(client_handle);
    rift_client_destroy(client_handle);

    return 0;
}
```
