// chat_session.cpp: Implementation of the session state machine and handlers.
#include "chat_session.hpp"
#include <iostream>

namespace chat {

// handle_message: Central switch logic for processing client commands.
void ChatSession::handle_message() {
    std::string body(read_body_.begin(), read_body_.end());
    MessageType type = static_cast<MessageType>(read_header_.type);

    switch (type) {
// Handle user login and ensure username uniqueness.
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
                joined_rooms_.insert(room); 
                room->join(shared_from_this());
                current_room_ = room;
                deliver(MessageType::INFO_MSG, "[Info]: Room '" + body + "' created and joined.");
                deliver(MessageType::JOIN_ROOM, body);
                deliver(MessageType::SWITCH_ROOM, body);
            } else {
                deliver(MessageType::ERROR_MSG, "[Server]: Room '" + body + "' already exists.");
            }
            break;
        }
        case MessageType::JOIN_ROOM: {
            auto room = room_manager_.find_room(body);
            if (room) {
                joined_rooms_.insert(room); 
                room->join(shared_from_this());
                current_room_ = room;
                deliver(MessageType::INFO_MSG, "[Info]: Joined room: " + body);
                deliver(MessageType::JOIN_ROOM, body);
                deliver(MessageType::SWITCH_ROOM, body);
            } else {
                deliver(MessageType::ERROR_MSG, "[Server]: Room '" + body + "' does not exist.");
            }
            break;
        }
// Send a text message to all users in the current room.
        case MessageType::CHAT_MSG: {
            if (current_room_) {
                std::string full_body = "[" + username_ + "]: " + body;
                current_room_->broadcast(MessageType::CHAT_MSG, full_body);
            } else {
                deliver(MessageType::ERROR_MSG, "[Server]: Please join a room first.");
            }
            break;
        }
// Logic for one-to-one communication by searching the user registry.
        case MessageType::PRIVATE_MSG: {
            size_t space_pos = body.find(' ');
            if (space_pos != std::string::npos) {
                std::string recipient = body.substr(0, space_pos);
                std::string msg = body.substr(space_pos + 1);
                room_manager_.send_private(username_, recipient, msg);
            }
            break;
        }
// Forward binary file chunks across the SSL connection.
        case MessageType::FILE_START:
// Forward binary file chunks across the SSL connection.
        case MessageType::FILE_DATA:
// Forward binary file chunks across the SSL connection.
        case MessageType::FILE_END: {
            if (current_room_) {
                current_room_ -> broadcast(type, body, shared_from_this());
            } else {
                if (type == MessageType::FILE_START) {
                    deliver(MessageType::ERROR_MSG, "[Server]: Please join a room first.");
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
// Update which room messages are broadcast to.
        case MessageType::SWITCH_ROOM: {
            auto room = room_manager_.find_room(body);
            if (!room) {
                deliver(MessageType::ERROR_MSG, "[Server]: Room '" + body + "' does not exist.");
            } else if (room == current_room_) {
                deliver(MessageType::INFO_MSG, "[Info]: You are already in room: " + body);
            } else if (joined_rooms_.count(room)) {
                current_room_ = room;
                deliver(MessageType::INFO_MSG, "[Info]: Switched to room: " + body);
                deliver(MessageType::SWITCH_ROOM, body);
            } else {
                // Exit current room and join the new one
                if (current_room_) {
                    std::string old_room_name = current_room_->name();
                    joined_rooms_.erase(current_room_);
                    current_room_->leave(shared_from_this());
                    deliver(MessageType::LEAVE_ROOM, old_room_name);
                }
                
                joined_rooms_.insert(room);
                room->join(shared_from_this());
                current_room_ = room;
                deliver(MessageType::INFO_MSG, "[Info]: Joined and switched to room: " + body);
                deliver(MessageType::JOIN_ROOM, body);
                deliver(MessageType::SWITCH_ROOM, body);
            }
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
		
                if (current_room_ == room) {
                    current_room_ = nullptr;
                    deliver(MessageType::SWITCH_ROOM, "(none)");
                }

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

// handle_error: Cleanup when a client unexpectedly disconnects.
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
