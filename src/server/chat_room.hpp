// chat_room.hpp: Defines room management and broadcasting logic.
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
// ChatRoom: Manages a collection of participants and broadcasts to them.
class ChatRoom {
public:
    explicit ChatRoom(const std::string& name) : name_(name) {}

    void join(std::shared_ptr<ChatSession> session);
    void leave(std::shared_ptr<ChatSession> session);
// broadcast: Send a message to everyone in the room except (optionally) one user.
    void broadcast(MessageType type, const std::string& body, std::shared_ptr<ChatSession> exclude = nullptr);

    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::set<std::shared_ptr<ChatSession>> participants_;
};

// Manages all rooms and the global user registry
// RoomManager: Global registry for all rooms and online users.
class RoomManager {
public:
    bool register_user(const std::string& username, std::shared_ptr<ChatSession> session);
    void unregister_user(const std::string& username);
    
    std::shared_ptr<ChatRoom> create_room(const std::string& room_name);
    std::shared_ptr<ChatRoom> find_room(const std::string& room_name);
// send_private: Directly send a message between two users.
    void send_private(const std::string& sender, const std::string& recipient, const std::string& body);

    std::shared_ptr<ChatSession> find_user(const std::string& username);
    
    std::string get_all_rooms() const;
    std::string get_all_users() const;

    void notify_room_list_change();
    void notify_user_list_change();

private:
    std::map<std::string, std::shared_ptr<ChatRoom>> rooms_;
    std::map<std::string, std::shared_ptr<ChatSession>> users_;
};

} // namespace chat

#endif // CHAT_ROOM_HPP
