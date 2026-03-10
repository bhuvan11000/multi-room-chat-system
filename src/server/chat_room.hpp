#ifndef CHAT_ROOM_HPP
#define CHAT_ROOM_HPP

#include <string>
#include <set>
#include <map>
#include <memory>
#include <vector>
#include <iostream>
#include "../common/protocol.hpp"

namespace chat {

// Forward declaration of ChatSession
class ChatSession;

// Represents a single chat room
class ChatRoom {
public:
    explicit ChatRoom(const std::string& name) : name_(name) {}

    void join(std::shared_ptr<ChatSession> session);
    void leave(std::shared_ptr<ChatSession> session);
    void broadcast(MessageType type, const std::string& body, std::shared_ptr<ChatSession> exclude = nullptr);

    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::set<std::shared_ptr<ChatSession>> participants_;
};

// Manages all rooms and the global user registry
class RoomManager {
public:
    void register_user(const std::string& username, std::shared_ptr<ChatSession> session);
    void unregister_user(const std::string& username);
    
    std::shared_ptr<ChatRoom> create_room(const std::string& room_name);
    std::shared_ptr<ChatRoom> find_room(const std::string& room_name);
    void send_private(const std::string& sender, const std::string& recipient, const std::string& body);

    std::shared_ptr<ChatSession> find_user(const std::string& username);

private:
    std::map<std::string, std::shared_ptr<ChatRoom>> rooms_;
    std::map<std::string, std::shared_ptr<ChatSession>> users_;
};

} // namespace chat

#endif // CHAT_ROOM_HPP
