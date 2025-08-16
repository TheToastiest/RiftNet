#ifndef RIFTNET_CLIENT_HPP
#define RIFTNET_CLIENT_HPP

#include "RiftCommon.hpp"

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Creates a new client instance.
	 * @param config The configuration for the client.
	 * @return A handle to the new client, or NULL on failure.
	 */
	RiftClientHandle rift_client_create(const RiftClientConfig* config);

	/**
	 * @brief Destroys a client instance and frees all associated resources.
	 * @param client The handle to the client to destroy.
	 */
	void rift_client_destroy(RiftClientHandle client);

	/**
	 * @brief Connects the client to a server.
	 * @param client The client handle.
	 * @param host_address The IP address or hostname of the server.
	 * @param port The port of the server.
	 * @return RIFT_SUCCESS if the connection process is initiated successfully.
	 */
	RiftResult rift_client_connect(RiftClientHandle client, const char* host_address, uint16_t port);

	/**
	 * @brief Disconnects the client from the server.
	 * @param client The client handle.
	 */
	void rift_client_disconnect(RiftClientHandle client);

	/**
	 * @brief Sends a packet to the server.
	 * @param client The client handle.
	 * @param data The buffer of data to send.
	 * @param size The size of the data buffer.
	 * @return RIFT_SUCCESS on success, or an error code on failure.
	 */
	RiftResult rift_client_send(RiftClientHandle client, const uint8_t* data, size_t size);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // RIFTNET_CLIENT_HPP
