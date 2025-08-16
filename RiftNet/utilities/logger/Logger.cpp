// File: src/core/Logger.cpp
#include "pch.h"
#include "Logger.hpp"

#include <memory>
#include <vector>

namespace RiftNet {
    namespace Logging {

        std::shared_ptr<spdlog::logger> Logger::coreLogger;

        void Logger::Init() {
            // Create sinks
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

            // NOTE: You cannot declare two variables with the same name. Create *two* distinct sinks
            // and put BOTH into the sink list, or choose which one at runtime.
            // If you truly want two separate files (client + server), you typically select one
            // based on a role flag/environment var/compile definition.

            // Option A: pick the file name at runtime via env var or compile-time macro.
            // Here we pick based on a macro RIFTNET_ROLE set to "server" or "client".
#ifdef RIFTNET_ROLE_SERVER
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("RiftNetServer.log", true);
#elif defined(RIFTNET_ROLE_CLIENT)
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("RiftNetClient.log", true);
#else
    // Fallback: single generic log file if no role defined
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("RiftNet.log", true);
#endif

            console_sink->set_pattern("[%H:%M:%S] [%^%l%$] %v");
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");

            std::vector<spdlog::sink_ptr> sinks{ console_sink, file_sink };
            coreLogger = std::make_shared<spdlog::logger>("RiftNet", sinks.begin(), sinks.end());

            coreLogger->set_level(spdlog::level::debug); // Tweak per build type
            coreLogger->flush_on(spdlog::level::warn);
            spdlog::register_logger(coreLogger);
        }

    } // namespace Logging
} // namespace RiftNet
