#include <iostream>
#include <boost/asio.hpp>
#include "chat_session.hpp"

using boost::asio::ip::tcp;

namespace chat {

class ChatServer {
public:
    ChatServer(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        std::cout << "Server started on port " << port << "..." << std::endl;
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<ChatSession>(std::move(socket), room_manager_)->start();
                }
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
    RoomManager room_manager_;
};

} // namespace chat

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: chat_server <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;
        chat::ChatServer server(io_context, std::atoi(argv[1]));
        io_context.run();

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
