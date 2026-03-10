#ifndef CHAT_SESSION_HPP
#define CHAT_SESSION_HPP

#include <boost/asio.hpp>
#include <memory>
#include <deque>
#include <vector>
#include <string>
#include "../common/protocol.hpp"
#include "chat_room.hpp"

namespace chat {

using boost::asio::ip::tcp;

class ChatSession : public std::enable_shared_from_this<ChatSession> {
public:
    ChatSession(tcp::socket socket, RoomManager& room_manager)
        : socket_(std::move(socket)), room_manager_(room_manager) {}

    void start() {
        do_read_header();
    }

    void deliver(MessageType type, const std::string& body) {
        bool write_in_progress = !write_msgs_.empty();
        
        // Prepare header
        MessageHeader header;
        header.type = static_cast<uint8_t>(type);
        header.length = static_cast<uint32_t>(body.size());

        std::vector<uint8_t> full_msg;
        full_msg.resize(HEADER_SIZE + body.size());
        std::memcpy(full_msg.data(), &header, HEADER_SIZE);
        std::memcpy(full_msg.data() + HEADER_SIZE, body.data(), body.size());

        write_msgs_.push_back(std::move(full_msg));
        
        if (!write_in_progress) {
            do_write();
        }
    }

    const std::string& username() const { return username_; }

private:
    void do_read_header() {
        auto self(shared_from_this());
        boost::asio::async_read(socket_,
            boost::asio::buffer(&read_header_, HEADER_SIZE),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec && read_header_.length <= MAX_BODY_SIZE) {
                    do_read_body();
                } else {
                    handle_error(ec);
                }
            });
    }

    void do_read_body() {
        auto self(shared_from_this());
        read_body_.resize(read_header_.length);
        boost::asio::async_read(socket_,
            boost::asio::buffer(read_body_.data(), read_body_.size()),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    handle_message();
                    do_read_header();
                } else {
                    handle_error(ec);
                }
            });
    }

    void do_write() {
        auto self(shared_from_this());
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().size()),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    write_msgs_.pop_front();
                    if (!write_msgs_.empty()) {
                        do_write();
                    }
                } else {
                    handle_error(ec);
                }
            });
    }

    void handle_message();
    void handle_error(const boost::system::error_code& ec);

    tcp::socket socket_;
    RoomManager& room_manager_;
    std::string username_;
    std::shared_ptr<ChatRoom> current_room_;

    MessageHeader read_header_;
    std::vector<uint8_t> read_body_;
    std::deque<std::vector<uint8_t>> write_msgs_;
};

} // namespace chat

#endif // CHAT_SESSION_HPP
