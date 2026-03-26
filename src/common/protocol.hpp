#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>

namespace chat {

// Message Types
enum class MessageType : uint8_t {
    LOGIN = 1,
    CREATE_ROOM = 2,
    JOIN_ROOM = 3,
    LEAVE_ROOM = 4,
    CHAT_MSG = 5,
    PRIVATE_MSG = 6,
    FILE_START = 7,
    FILE_DATA = 8,
    FILE_END = 9,
    ERROR_MSG = 10,
    INFO_MSG = 11,
    LIST_ROOMS = 12,
    LIST_USERS = 13
};

// Fixed Header: 1 byte for type, 4 bytes for body length
#pragma pack(push, 1)
struct MessageHeader {
    uint8_t type;
    uint32_t length;
};
#pragma pack(pop)

const size_t HEADER_SIZE = sizeof(MessageHeader);
const size_t MAX_BODY_SIZE = 1024 * 1024 * 10; // 10MB limit for safety

} // namespace chat

#endif // PROTOCOL_HPP
