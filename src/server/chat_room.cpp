#include "chat_room.hpp"
#include "chat_session.hpp"

namespace chat {

void ChatRoom::join(std::shared_ptr<ChatSession> session) {
    participants_.insert(session);
    broadcast(MessageType::INFO_MSG, "[Server]: " + session->username() + " has joined the room.");
}

void ChatRoom::leave(std::shared_ptr<ChatSession> session) {
    participants_.erase(session);
    broadcast(MessageType::INFO_MSG, "[Server]: " + session->username() + " has left the room.");
}

void ChatRoom::broadcast(MessageType type, const std::string& body, std::shared_ptr<ChatSession> exclude) {
    for (auto& participant : participants_) {
        if (participant != exclude) {
            participant->deliver(type, body);
        }
    }
}

void RoomManager::register_user(const std::string& username, std::shared_ptr<ChatSession> session) {
    users_[username] = session;
}

void RoomManager::unregister_user(const std::string& username) {
    users_.erase(username);
}

std::shared_ptr<ChatRoom> RoomManager::create_room(const std::string& room_name) {
    if (rooms_.find(room_name) != rooms_.end()) {
        return nullptr; // Room already exists
    }
    auto room = std::make_shared<ChatRoom>(room_name);
    rooms_[room_name] = room;
    return room;
}

std::shared_ptr<ChatRoom> RoomManager::find_room(const std::string& room_name) {
    auto it = rooms_.find(room_name);
    if (it != rooms_.end()) {
        return it->second;
    }
    return nullptr;
}

void RoomManager::send_private(const std::string& sender, const std::string& recipient, const std::string& body) {
    auto it = users_.find(recipient);
    if (it != users_.end()) {
        it->second->deliver(MessageType::PRIVATE_MSG, "[Private from " + sender + "]: " + body);
    } else {
        auto sender_it = users_.find(sender);
        if (sender_it != users_.end()) {
            sender_it->second->deliver(MessageType::ERROR_MSG, "User " + recipient + " not found.");
        }
    }
}

std::shared_ptr<ChatSession> RoomManager::find_user(const std::string& username) {
    auto it = users_.find(username);
    if (it != users_.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace chat
