// File: src/core/Logger.cpp
#include "../../include/core/Logger.hpp"

namespace RiftNet::Logging {

    std::shared_ptr<spdlog::logger> Logger::coreLogger;

    void Logger::Init() {
        // Create sinks
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("RiftNet.log", true);

        console_sink->set_pattern("[%H:%M:%S] [%^%l%$] %v");
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");

        std::vector<spdlog::sink_ptr> sinks{ console_sink, file_sink };
        coreLogger = std::make_shared<spdlog::logger>("RiftNet", sinks.begin(), sinks.end());

        coreLogger->set_level(spdlog::level::debug); // Adjust as needed
        coreLogger->flush_on(spdlog::level::warn);
        spdlog::register_logger(coreLogger);
    }

}
