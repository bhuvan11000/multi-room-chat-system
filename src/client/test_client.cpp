#include <iostream>
#include <thread>
#include <deque>
#include <fstream>
#include <cstring>
#include <boost/asio.hpp>
#include "../common/protocol.hpp"

using boost::asio::ip::tcp;

namespace chat {

class ChatClient {
public:
    ChatClient(boost::asio::io_context& io_context, const tcp::resolver::results_type& endpoints)
        : io_context_(io_context), socket_(io_context) {
        do_connect(endpoints);
    }

    void write(MessageType type, const std::string& body) {
        boost::asio::post(io_context_, [this, type, body]() {
            bool write_in_progress = !write_msgs_.empty();
            
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
        });
    }

    void send_file(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            std::cerr << "Could not open file: " << filepath << std::endl;
            return;
        }

        std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);
        write(MessageType::FILE_START, filename);

        char buffer[4096];
        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
            write(MessageType::FILE_DATA, std::string(buffer, file.gcount()));
        }

        write(MessageType::FILE_END, filename);
        std::cout << "File sent: " << filename << std::endl;
    }

    void close() {
        boost::asio::post(io_context_, [this]() { socket_.close(); });
    }

private:
    void do_connect(const tcp::resolver::results_type& endpoints) {
        boost::asio::async_connect(socket_, endpoints,
            [this](boost::system::error_code ec, tcp::endpoint) {
                if (!ec) {
                    do_read_header();
                }
            });
    }

    void do_read_header() {
        boost::asio::async_read(socket_,
            boost::asio::buffer(&read_header_, HEADER_SIZE),
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    do_read_body();
                } else {
                    socket_.close();
                }
            });
    }

    void do_read_body() {
        read_body_.resize(read_header_.length);
        boost::asio::async_read(socket_,
            boost::asio::buffer(read_body_.data(), read_body_.size()),
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    handle_incoming_message();
                    do_read_header();
                } else {
                    socket_.close();
                }
            });
    }

    void handle_incoming_message() {
        MessageType type = static_cast<MessageType>(read_header_.type);
        std::string body(read_body_.begin(), read_body_.end());

        if (type == MessageType::FILE_START) {
            std::cout << "\nReceiving file: " << body << "..." << std::endl;
            current_incoming_file_.open("received_" + body, std::ios::binary);
        } else if (type == MessageType::FILE_DATA) {
            if (current_incoming_file_.is_open()) {
                current_incoming_file_.write(body.data(), body.size());
            }
        } else if (type == MessageType::FILE_END) {
            if (current_incoming_file_.is_open()) {
                current_incoming_file_.close();
                std::cout << "File transfer complete: " << body << std::endl;
            }
        } else {
            std::cout << "\n" << body << "\n> " << std::flush;
        }
    }

    void do_write() {
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().size()),
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    write_msgs_.pop_front();
                    if (!write_msgs_.empty()) {
                        do_write();
                    }
                } else {
                    socket_.close();
                }
            });
    }

    boost::asio::io_context& io_context_;
    tcp::socket socket_;
    MessageHeader read_header_;
    std::vector<uint8_t> read_body_;
    std::deque<std::vector<uint8_t>> write_msgs_;
    std::ofstream current_incoming_file_;
};

} // namespace chat

int main(int argc, char* argv[]) {
    try {
        if (argc != 3) {
            std::cerr << "Usage: chat_client <host> <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;
        tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(argv[1], argv[2]);

        chat::ChatClient client(io_context, endpoints);

        std::thread t([&io_context]() { io_context.run(); });

        std::string username;
        std::cout << "Enter username: ";
        std::getline(std::cin, username);
        client.write(chat::MessageType::LOGIN, username);

        std::string line;
        std::cout << "Commands: /create <room>, /join <room>, /private <user> <msg>, /sendfile <path>, or just type message\n> ";
        while (std::getline(std::cin, line)) {
            if (line.substr(0, 8) == "/create ") {
                client.write(chat::MessageType::CREATE_ROOM, line.substr(8));
            } else if (line.substr(0, 6) == "/join ") {
                client.write(chat::MessageType::JOIN_ROOM, line.substr(6));
            } else if (line.substr(0, 9) == "/private ") {
                client.write(chat::MessageType::PRIVATE_MSG, line.substr(9));
            } else if (line.substr(0, 10) == "/sendfile ") {
                client.send_file(line.substr(10));
            } else if (line == "/quit") {
                break;
            } else {
                client.write(chat::MessageType::CHAT_MSG, line);
            }
            std::cout << "> ";
        }

        client.close();
        t.join();

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
