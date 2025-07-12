#pragma once

#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace RiftNet::Log
{
    // Optional init method
    void Init();

    // Global loggers by category
    std::shared_ptr<spdlog::logger>& GetNetworkLogger();
}

// --- Macros for use ---
//#define RF_NETWORK_TRACE(...)    ::RiftNet::Log::GetNetworkLogger()->trace(__VA_ARGS__)
//#define RF_NETWORK_DEBUG(...)    ::RiftNet::Log::GetNetworkLogger()->debug(__VA_ARGS__)
//#define RF_NETWORK_INFO(...)     ::RiftNet::Log::GetNetworkLogger()->info(__VA_ARGS__)
//#define RF_NETWORK_WARN(...)     ::RiftNet::Log::GetNetworkLogger()->warn(__VA_ARGS__)
//#define RF_NETWORK_ERROR(...)    ::RiftNet::Log::GetNetworkLogger()->error(__VA_ARGS__)
//#define RF_NETWORK_CRITICAL(...) ::RiftNet::Log::GetNetworkLogger()->critical(__VA_ARGS__)

#define RF_NETWORK_TRACE(...)   if (auto lg = spdlog::get("RiftNet")) lg->trace(__VA_ARGS__)
#define RF_NETWORK_INFO(...)  if (auto lg = spdlog::get("RiftNet")) lg->info(__VA_ARGS__)
#define RF_NETWORK_DEBUG(...) if (auto lg = spdlog::get("RiftNet")) lg->debug(__VA_ARGS__)
#define RF_NETWORK_ERROR(...) if (auto lg = spdlog::get("RiftNet")) lg->error(__VA_ARGS__)
#define RF_NETWORK_WARN(...)  if (auto lg = spdlog::get("RiftNet")) lg->warn(__VA_ARGS__)
#define RF_NETWORK_CRITICAL(...) if (auto lg = spdlog::get("RiftNet")) lg->critical(__VA_ARGS__)
