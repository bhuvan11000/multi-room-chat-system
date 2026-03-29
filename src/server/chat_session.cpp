#include "chat_session.hpp"
#include <iostream>

namespace chat {

void ChatSession::handle_message() {
    std::string body(read_body_.begin(), read_body_.end());
    MessageType type = static_cast<MessageType>(read_header_.type);

    switch (type) {
        case MessageType::LOGIN: {
     	    if(room_manager_.register_user(body, shared_from_this())){
		    username_ = body;
		    room_manager_.register_user(username_, shared_from_this());
		    deliver(MessageType::LIST_ROOMS, room_manager_.get_all_rooms());
		    deliver(MessageType::INFO_MSG, "Welcome " + username_ + "!");
		    std::cout << "\033[1;32m" << "User logged in: " << username_ << "\033[0m"<<std::endl;
            }else{
            	    deliver(MessageType::ERROR_MSG, "Username " + body + " is already taken. Please choose another one.");
            	    boost::system::error_code ec;
            	    ssl_socket_.lowest_layer().shutdown(tcp::socket::shutdown_both, ec);
            }
            break;
        }
        case MessageType::CREATE_ROOM: {
            auto room = room_manager_.create_room(body);
            if (room) {
                // REMOVED: current_room_->leave() logic to stay in previous rooms
                joined_rooms_.insert(room); // Add to the set of rooms
                room->join(shared_from_this());
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
                // REMOVED: current_room_->leave() logic
                joined_rooms_.insert(room); // Keep track of this new room
                room->join(shared_from_this());
                deliver(MessageType::JOIN_ROOM, body);
            } else {
                deliver(MessageType::ERROR_MSG, "Room '" + body + "' does not exist.");
            }
            break;
        }
        case MessageType::CHAT_MSG: {
            if (!joined_rooms_.empty()) {
                std::string full_body = "[" + username_ + "]: " + body;
                // Broadcast the message to ALL rooms the user is currently in
                for (auto& room : joined_rooms_) {
                    room->broadcast(MessageType::CHAT_MSG, full_body);
                }
            } else {
                deliver(MessageType::ERROR_MSG, "Please join a room first.");
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
            if (!joined_rooms_.empty()) {
                for (auto const& room : joined_rooms_) {
                   room -> broadcast(type, body, shared_from_this());
                }
            } else {
                if (type == MessageType::FILE_START) {
                    deliver(MessageType::ERROR_MSG, "Please join a room first.");
                }
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
	// Add a helper for trimming or do it inline
	case MessageType::LEAVE_ROOM: {
	    // 1. Trim leading/trailing whitespace from the room name
	    body.erase(0, body.find_first_not_of(" "));
	    body.erase(body.find_last_not_of(" ") + 1);

	    auto room = room_manager_.find_room(body);
	    
	    // 2. Add debug logging to see exactly what 'body' is
	    std::cout << "User '" << username_ << "' attempting to leave room: [" << body << "]" << std::endl;

	    if (room && joined_rooms_.count(room)) {
		joined_rooms_.erase(room);
		room->leave(shared_from_this());
		
		deliver(MessageType::INFO_MSG, "You Left room: " + body);
		deliver(MessageType::LEAVE_ROOM, body);
	    } else {
		deliver(MessageType::ERROR_MSG, "You are not a member of room: " + body);
	    }
	    break;
	}

        default:
            break;
    }
}

void ChatSession::handle_error(const boost::system::error_code& ec) {
    if (!username_.empty()) {
        std::cout << "User disconnected: " << username_ << " (" << ec.message() << ")" << std::endl;
        // Leave all joined rooms
        for (auto& room : joined_rooms_) {
            room->leave(shared_from_this());
        }
        joined_rooms_.clear();
        room_manager_.unregister_user(username_);
    }
}

} // namespace chat
