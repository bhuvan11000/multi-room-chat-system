cmake_minimum_required(VERSION 3.15)
project(MultiRoomChatSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Boost ─────────────────────────────────────────────────────────────────────
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(OpenSSL REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
include_directories(src/common)

# ── FTXUI (for TUI client) ────────────────────────────────────────────────────
# Prefer a system-installed package; auto-fetch from GitHub if not found.
find_package(ftxui QUIET)
if(NOT ftxui_FOUND)
    message(STATUS "FTXUI not found locally — fetching from GitHub...")
    include(FetchContent)
    FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
        GIT_TAG        v5.0.0
        GIT_SHALLOW    TRUE
    )
    set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ftxui)
endif()

# ── Server ────────────────────────────────────────────────────────────────────
add_executable(chat_server
    src/server/main.cpp
    src/server/chat_session.cpp
    src/server/chat_room.cpp
)
target_link_libraries(chat_server
    Boost::system
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
)

# ── TUI Client ────────────────────────────────────────────────────────────────
add_executable(chat_client
    src/client/chat_client.cpp
)
target_link_libraries(chat_client
    Boost::system
    Boost::thread
    ftxui::component
    ftxui::dom
    ftxui::screen
    OpenSSL::SSL
    OpenSSL::Crypto
)

# ── SSL/TLS upgrade: uncomment when ready ────────────────────────────────────
# find_package(OpenSSL REQUIRED)
# target_link_libraries(chat_server PRIVATE OpenSSL::SSL OpenSSL::Crypto)
# target_link_libraries(chat_client PRIVATE OpenSSL::SSL OpenSSL::Crypto)


# This version connects to an already-running chat_server externally.
# Run: ./chat_server <port>  first, then: ./perf_test <host> <port>
#
# Only needs protocol.hpp from src/common — no server sources compiled in.
add_executable(perf_test
    src/perf/perf_test.cpp
)
target_link_libraries(perf_test
    Boost::system
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
 
