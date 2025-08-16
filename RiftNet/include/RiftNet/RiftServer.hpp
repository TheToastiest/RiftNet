#ifndef RIFTNET_SERVER_HPP
#define RIFTNET_SERVER_HPP

#include "RiftCommon.hpp"

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Creates a new server instance.
	 * @param config The configuration for the server.
	 * @return A handle to the new server, or NULL on failure.
	 */
	RiftServerHandle rift_server_create(const RiftServerConfig* config);

	/**
	 * @brief Destroys a server instance and frees all associated resources.
	 * @param server The handle to the server to destroy.
	 */
	void rift_server_destroy(RiftServerHandle server);

	/**
	 * @brief Starts the server and begins listening for incoming connections and data.
	 * @param server The server handle.
	 * @return RIFT_SUCCESS on success, or an error code on failure.
	 */
	RiftResult rift_server_start(RiftServerHandle server);

	/**
	 * @brief Stops the server.
	 * @param server The server handle.
	 */
	void rift_server_stop(RiftServerHandle server);

	/**
	 * @brief Sends a packet to a specific client.
	 * @param server The server handle.
	 * @param client_id The ID of the client to send the data to.
	 * @param data The buffer of data to send.
	 * @param size The size of the data buffer.
	 * @return RIFT_SUCCESS on success, or an error code on failure.
	 */
	RiftResult rift_server_send(RiftServerHandle server, RiftClientId client_id, const uint8_t* data, size_t size);

	/**
	 * @brief Sends a packet to all connected clients.
	 * @param server The server handle.
	 * @param data The buffer of data to send.
	 * @param size The size of the data buffer.
	 * @return RIFT_SUCCESS on success, or an error code on failure.
	 */
	RiftResult rift_server_broadcast(RiftServerHandle server, const uint8_t* data, size_t size);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // RIFTNET_SERVER_HPP
