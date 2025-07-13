// File: /Logger.hpp
#pragma once
#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace RiftForged::Logging {

    class Logger {
    public:
        static void Init();

        static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return coreLogger; }

    private:
        static std::shared_ptr<spdlog::logger> coreLogger;
    };

    // Convenience macros
#define RF_NETWORK_TRACE(...)    ::RiftForged::Logging::Logger::GetCoreLogger()->trace(__VA_ARGS__)
#define RF_NETWORK_DEBUG(...)    ::RiftForged::Logging::Logger::GetCoreLogger()->debug(__VA_ARGS__)
#define RF_NETWORK_INFO(...)     ::RiftForged::Logging::Logger::GetCoreLogger()->info(__VA_ARGS__)
#define RF_NETWORK_WARN(...)     ::RiftForged::Logging::Logger::GetCoreLogger()->warn(__VA_ARGS__)
#define RF_NETWORK_ERROR(...)    ::RiftForged::Logging::Logger::GetCoreLogger()->error(__VA_ARGS__)
#define RF_NETWORK_CRITICAL(...) ::RiftForged::Logging::Logger::GetCoreLogger()->critical(__VA_ARGS__)

}
