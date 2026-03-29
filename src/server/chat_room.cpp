#include "chat_room.hpp"
#include "chat_session.hpp"

namespace chat {

void ChatRoom::join(std::shared_ptr<ChatSession> session) {
    participants_.insert(session);
    broadcast(MessageType::INFO_MSG, "[Server]: " + session->username() + " has joined the room " + name_);
}

void ChatRoom::leave(std::shared_ptr<ChatSession> session) {
    participants_.erase(session);
    broadcast(MessageType::INFO_MSG, "[Server]: " + session->username() + " has left the room " + name_);
}

void ChatRoom::broadcast(MessageType type, const std::string& body, std::shared_ptr<ChatSession> exclude) {
    for (auto& participant : participants_) {
        if (participant != exclude) {
            participant->deliver(type, body);
        }
    }
}

bool RoomManager::register_user(const std::string& username, std::shared_ptr<ChatSession> session) {
    if(users_.find(username) != users_.end()){
    	return false;
    }
    users_[username] = session;
    notify_user_list_change();
    return true;
}

void RoomManager::unregister_user(const std::string& username) {
    users_.erase(username);
    notify_user_list_change();
}

std::shared_ptr<ChatRoom> RoomManager::create_room(const std::string& room_name) {
    if (rooms_.find(room_name) != rooms_.end()) {
        return nullptr; // Room already exists
    }
    auto room = std::make_shared<ChatRoom>(room_name);
    rooms_[room_name] = room;
    notify_room_list_change();
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
        
        // Also send confirmation back to the sender
        auto sender_it = users_.find(sender);
        if (sender_it != users_.end()) {
            sender_it->second->deliver(MessageType::PRIVATE_MSG, "[Private to " + recipient + "]: " + body);
        }
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

std::string RoomManager::get_all_rooms() const {
    std::string list;
    for (auto const& [name, room] : rooms_) {
        if (!list.empty()) list += ",";
        list += name;
    }
    return list;
}

std::string RoomManager::get_all_users() const {
    std::string list;
    for (auto const& [name, session] : users_) {
        if (!list.empty()) list += ",";
        list += name;
    }
    return list;
}

void RoomManager::notify_room_list_change() {
    std::string list = get_all_rooms();
    for (auto const& [name, session] : users_) {
        session->deliver(MessageType::LIST_ROOMS, list);
    }
}

void RoomManager::notify_user_list_change() {
    std::string list = get_all_users();
    for (auto const& [name, session] : users_) {
        session->deliver(MessageType::LIST_USERS, list);
    }
}

} // namespace chat
