#ifndef RIFTNET_COMMON_HPP
#define RIFTNET_COMMON_HPP

#include "../../pch.h"
#include <stdint.h> // For fixed-width integers like uint16_t, uint64_t
#include <stddef.h> // For size_t

#ifdef __cplusplus
extern "C" {
#endif

    // Opaque handles to the internal C++ objects. The user only ever sees these pointers.
    typedef struct RiftServer* RiftServerHandle;
    typedef struct RiftClient* RiftClientHandle;

    // A handle to identify a specific client connected to the server.
    typedef uint64_t RiftClientId;

    // --- Enums for Configuration and Events ---

    typedef enum RiftResult {
        RIFT_SUCCESS = 0,
        RIFT_ERROR_GENERIC = -1,
        RIFT_ERROR_INVALID_HANDLE = -2,
        RIFT_ERROR_INVALID_PARAMETER = -3,
        RIFT_ERROR_SOCKET_CREATION_FAILED = -4,
        RIFT_ERROR_SOCKET_BIND_FAILED = -5,
        RIFT_ERROR_CONNECTION_FAILED = -6,
        RIFT_ERROR_SEND_FAILED = -7,
    } RiftResult;

    typedef enum RiftEventType {
        // Server Events
        RIFT_EVENT_SERVER_START,
        RIFT_EVENT_SERVER_STOP,
        RIFT_EVENT_CLIENT_CONNECTED,
        RIFT_EVENT_CLIENT_DISCONNECTED,

        // Shared Events
        RIFT_EVENT_PACKET_RECEIVED,
    } RiftEventType;

    // --- Event Structures ---

    // Structure to hold data for a received packet
    typedef struct RiftPacket {
        const uint8_t* data;
        size_t         size;
        RiftClientId   sender_id; // On server, identifies the client. On client, is 0.
    } RiftPacket;

    // The main event structure passed to the user's callback
    typedef struct RiftEvent {
        RiftEventType type;
        union {
            RiftPacket   packet;
            RiftClientId client_id; // Used for connect/disconnect events
        } data;
    } RiftEvent;


    // --- Callbacks ---

    // The user must provide a function with this signature to handle all network events.
    typedef void (*RiftEventCallback)(const RiftEvent* event, void* user_data);


    // --- Configuration ---

    typedef struct RiftServerConfig {
        const char* host_address;
        uint16_t          port;
        RiftEventCallback event_callback;
        void* user_data; // Optional pointer passed back in every callback
    } RiftServerConfig;

    typedef struct RiftClientConfig {
        RiftEventCallback event_callback;
        void* user_data;
    } RiftClientConfig;


#ifdef __cplusplus
} // extern "C"
#endif

#endif // RIFTNET_COMMON_HPP
