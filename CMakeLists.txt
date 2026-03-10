cmake_minimum_required(VERSION 3.10)
project(MultiRoomChatSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Boost
find_package(Boost REQUIRED COMPONENTS system thread)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    
    # Common headers
    include_directories(src/common)

    # Server executable
    add_executable(chat_server 
        src/server/main.cpp 
        src/server/chat_session.cpp 
        src/server/chat_room.cpp
    )
    target_link_libraries(chat_server Boost::system Boost::thread)

    # Client executable
    add_executable(chat_client 
        src/client/test_client.cpp
    )
    target_link_libraries(chat_client Boost::system Boost::thread)
else()
    message(FATAL_ERROR "Boost not found. Please install libboost-all-dev.")
endif()
