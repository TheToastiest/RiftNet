// File: /Logger.hpp
#pragma once
#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace RiftNet::Logging {

    class Logger {
    public:
        static void Init();

        static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return coreLogger; }

    private:
        static std::shared_ptr<spdlog::logger> coreLogger;
    };

    // Convenience macros
#define RF_NETWORK_TRACE(...)    ::RiftNet::Logging::Logger::GetCoreLogger()->trace(__VA_ARGS__)
#define RF_NETWORK_DEBUG(...)    ::RiftNet::Logging::Logger::GetCoreLogger()->debug(__VA_ARGS__)
#define RF_NETWORK_INFO(...)     ::RiftNet::Logging::Logger::GetCoreLogger()->info(__VA_ARGS__)
#define RF_NETWORK_WARN(...)     ::RiftNet::Logging::Logger::GetCoreLogger()->warn(__VA_ARGS__)
#define RF_NETWORK_ERROR(...)    ::RiftNet::Logging::Logger::GetCoreLogger()->error(__VA_ARGS__)
#define RF_NETWORK_CRITICAL(...) ::RiftNet::Logging::Logger::GetCoreLogger()->critical(__VA_ARGS__)

}
