#include "chat_session.hpp"
#include <iostream>

namespace chat {

void ChatSession::handle_message() {
    std::string body(read_body_.begin(), read_body_.end());
    MessageType type = static_cast<MessageType>(read_header_.type);

    switch (type) {
        case MessageType::LOGIN: {
            username_ = body;
            room_manager_.register_user(username_, shared_from_this());
            deliver(MessageType::INFO_MSG, "Welcome " + username_ + "!");
            std::cout << "User logged in: " << username_ << std::endl;
            break;
        }
        case MessageType::CREATE_ROOM: {
            auto room = room_manager_.create_room(body);
            if (room) {
                if (current_room_) current_room_->leave(shared_from_this());
                current_room_ = room;
                current_room_->join(shared_from_this());
                deliver(MessageType::INFO_MSG, "Room '" + body + "' created and joined.");
                deliver(MessageType::JOIN_ROOM, body);
            } else {
                deliver(MessageType::ERROR_MSG, "Room '" + body + "' already exists.");
            }
            break;
        }
        case MessageType::JOIN_ROOM: {
            auto room = room_manager_.find_room(body);
            if (room) {
                if (current_room_) current_room_->leave(shared_from_this());
                current_room_ = room;
                current_room_->join(shared_from_this());
                deliver(MessageType::JOIN_ROOM, body);
            } else {
                deliver(MessageType::ERROR_MSG, "Room '" + body + "' does not exist.");
            }
            break;
        }
        case MessageType::CHAT_MSG: {
            if (current_room_) {
                std::string full_body = "[" + username_ + "]: " + body;
                current_room_->broadcast(MessageType::CHAT_MSG, full_body);
            } else {
                deliver(MessageType::ERROR_MSG, "You are not in a room.");
            }
            break;
        }
        case MessageType::PRIVATE_MSG: {
            size_t space_pos = body.find(' ');
            if (space_pos != std::string::npos) {
                std::string recipient = body.substr(0, space_pos);
                std::string msg = body.substr(space_pos + 1);
                room_manager_.send_private(username_, recipient, msg);
            }
            break;
        }
        case MessageType::FILE_START:
        case MessageType::FILE_DATA:
        case MessageType::FILE_END: {
            if (current_room_) {
                current_room_->broadcast(type, body, shared_from_this());
            }
            break;
        }
        case MessageType::LIST_ROOMS: {
            deliver(MessageType::LIST_ROOMS, room_manager_.get_all_rooms());
            break;
        }
        case MessageType::LIST_USERS: {
            deliver(MessageType::LIST_USERS, room_manager_.get_all_users());
            break;
        }
        default:
            break;
    }
}

void ChatSession::handle_error(const boost::system::error_code& ec) {
    if (!username_.empty()) {
        std::cout << "User disconnected: " << username_ << " (" << ec.message() << ")" << std::endl;
        if (current_room_) {
            current_room_->leave(shared_from_this());
        }
        room_manager_.unregister_user(username_);
    }
}

} // namespace chat
