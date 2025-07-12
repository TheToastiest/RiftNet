#include "../../include/core/Logger.hpp"

namespace RiftNet::Log
{
    static std::shared_ptr<spdlog::logger> s_NetworkLogger;

    void Init()
    {
        spdlog::set_pattern("[%T] [%^%l%$] %v");

        s_NetworkLogger = spdlog::stdout_color_mt("Network");
        s_NetworkLogger->set_level(spdlog::level::trace); // Change per config
    }

    std::shared_ptr<spdlog::logger>& GetNetworkLogger()
    {
        return s_NetworkLogger;
    }
}
