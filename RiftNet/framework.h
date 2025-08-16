#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <map>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <memory>
#include <fstream>
#include <sstream>
#include <deque>
#include <condition_variable>
#include <functional>
#include <future>
#include <utility>      // For std::move and std::forward
#include <type_traits>  // For std::invoke_result
#include <stdexcept>  