#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "chat_session.hpp"
#include "../common/ssl_manager.hpp"

using boost::asio::ip::tcp;

namespace chat {

class ChatServer {
public:
    ChatServer(boost::asio::io_context& io_context, boost::asio::ssl::context& context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), context_(context) {
        std::cout << "Server started on port " << port << "..." << std::endl;
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<ChatSession>(std::move(socket), context_, room_manager_)->start();
                }
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
    boost::asio::ssl::context& context_;
    RoomManager room_manager_;
};

} 

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: chat_server <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;
        auto ssl_ctx = chat::SSLManager::create_boost_context(chat::SSLManager::SERVER, "server.crt", "server.key");
        chat::ChatServer server(io_context, ssl_ctx, std::atoi(argv[1]));
        io_context.run();

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
