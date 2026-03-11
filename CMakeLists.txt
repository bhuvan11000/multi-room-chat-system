cmake_minimum_required(VERSION 3.15)
project(MultiRoomChatSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Boost ─────────────────────────────────────────────────────────────────────
find_package(Boost REQUIRED COMPONENTS system thread)
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
)

# ── SSL/TLS upgrade: uncomment when ready ────────────────────────────────────
# find_package(OpenSSL REQUIRED)
# target_link_libraries(chat_server PRIVATE OpenSSL::SSL OpenSSL::Crypto)
# target_link_libraries(chat_client PRIVATE OpenSSL::SSL OpenSSL::Crypto)
