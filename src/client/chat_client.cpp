#include <iostream>
#include <thread>
#include <deque>
#include <cstring>
#include <mutex>
#include <functional>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include "../common/protocol.hpp"
#include "../common/ssl_manager.hpp"

using boost::asio::ip::tcp;
using namespace ftxui;
using namespace chat;

std::mutex log_mtx;
std::vector<std::string> log_msgs;

void push(const std::string& s) {
    std::lock_guard<std::mutex> lk(log_mtx);
    log_msgs.push_back(s);
}

void send_msg(boost::asio::ssl::stream<tcp::socket>& sock, boost::asio::io_context& ioc,
              std::deque<std::vector<uint8_t>>& q, MessageType t, const std::string& body) {
    boost::asio::post(ioc, [&sock, &q, t, body]() {
        bool busy = !q.empty();
        MessageHeader h{ static_cast<uint8_t>(t), static_cast<uint32_t>(body.size()) };
        std::vector<uint8_t> msg(HEADER_SIZE + body.size());
        std::memcpy(msg.data(), &h, HEADER_SIZE);
        std::memcpy(msg.data() + HEADER_SIZE, body.data(), body.size());
        q.push_back(std::move(msg));
        if (!busy) {
            auto do_write = [&sock, &q](auto self) -> void {
                boost::asio::async_write(sock, boost::asio::buffer(q.front()),
                    [&sock, &q, self](boost::system::error_code ec, std::size_t) {
                        if (!ec) {
                            q.pop_front();
                            if (!q.empty()) self(self);
                        }
                    });
            };
            do_write(do_write);
        }
    });
}

void send_file(boost::asio::ssl::stream<tcp::socket>& sock, boost::asio::io_context& ioc,
               std::deque<std::vector<uint8_t>>& q, const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        push("[Error]: Could not open file " + path);
        return;
    }

    std::string filename = path.substr(path.find_last_of("/\\") + 1);
    send_msg(sock, ioc, q, MessageType::FILE_START, filename);

    char buffer[4096];
    while (file.read(buffer, sizeof(buffer))) {
        send_msg(sock, ioc, q, MessageType::FILE_DATA, std::string(buffer, file.gcount()));
    }
    if (file.gcount() > 0) {
        send_msg(sock, ioc, q, MessageType::FILE_DATA, std::string(buffer, file.gcount()));
    }

    send_msg(sock, ioc, q, MessageType::FILE_END, filename);
    push("[Info]: File sent: " + filename);
}

int main(int argc, char* argv[]) {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    if (argc != 3) { std::cerr << "Usage: chat_client <host> <port>\n"; return 1; }

    boost::asio::io_context ioc;
    auto ssl_ctx = chat::SSLManager::create_boost_context(chat::SSLManager::CLIENT);
    boost::asio::ssl::stream<tcp::socket> sock(ioc, ssl_ctx);
    std::deque<std::vector<uint8_t>> wq;

    try {
        tcp::resolver resolver(ioc);
        boost::asio::connect(sock.lowest_layer(), resolver.resolve(argv[1], argv[2]));
        sock.handshake(boost::asio::ssl::stream_base::client);
    } catch (std::exception& e) {
        std::cerr << "Connection error: " << e.what() << "\n";
        return 1;
    }

    MessageHeader rh{};
    std::vector<uint8_t> rb;
    std::function<void()> read_hdr, read_body;
    std::ofstream receiving_file;
    std::string receiving_filename;

    read_hdr = [&]() {
        boost::asio::async_read(sock, boost::asio::buffer(&rh, HEADER_SIZE),
            [&](boost::system::error_code ec, std::size_t) {
                if (!ec) read_body(); else push("[disconnected]");
            });
    };

    read_body = [&]() {
        rb.resize(rh.length);
        boost::asio::async_read(sock, boost::asio::buffer(rb.data(), rb.size()),
            [&](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    MessageType type = static_cast<MessageType>(rh.type);
                    std::string body(rb.begin(), rb.end());

                    if (type == MessageType::FILE_START) {
                        receiving_filename = "received_" + body;
                        receiving_file.open(receiving_filename, std::ios::binary);
                        push("[Info]: Receiving file: " + body);
                    } else if (type == MessageType::FILE_DATA) {
                        if (receiving_file.is_open()) {
                            receiving_file.write(reinterpret_cast<const char*>(rb.data()), rb.size());
                        }
                    } else if (type == MessageType::FILE_END) {
                        if (receiving_file.is_open()) {
                            receiving_file.close();
                            push("[Info]: File saved as: " + receiving_filename);
                        }
                    } else {
                        // Normal chat or info messages
                        push(body);
                    }
                    read_hdr();
                }
            });
    };

    read_hdr();
    std::thread net([&]() { ioc.run(); });

    std::string username;
    std::cout << "Username: ";
    std::getline(std::cin, username);
    send_msg(sock, ioc, wq, MessageType::LOGIN, username);

    auto screen = ScreenInteractive::Fullscreen();
    std::string input_text, room = "(none)";

    auto input = Input(&input_text, "/create  /join  /private  /sendfile  or message");
    input |= CatchEvent([&](Event e) {
        if (e != Event::Return || input_text.empty()) return false;
        std::string line = input_text;
        input_text.clear();
        if      (line.substr(0,8)=="/create ") { room=line.substr(8); send_msg(sock,ioc,wq,MessageType::CREATE_ROOM,room); }
        else if (line.substr(0,6)=="/join ")   { room=line.substr(6); send_msg(sock,ioc,wq,MessageType::JOIN_ROOM,room); }
        else if (line.substr(0,9)=="/private ")  send_msg(sock,ioc,wq,MessageType::PRIVATE_MSG,line.substr(9));
        else if (line.substr(0,10)=="/sendfile ") send_file(sock,ioc,wq,line.substr(10));
        else if (line=="/quit")                  screen.Exit();
        else                                     send_msg(sock,ioc,wq,MessageType::CHAT_MSG,line);
        return true;
    });

    std::thread([&]() {
        while(true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            screen.PostEvent(Event::Custom);
        }
    }).detach();

    screen.Loop(Renderer(input, [&]() {
        std::vector<std::string> snap;
        {
            std::lock_guard<std::mutex> lk(log_mtx);
            snap = log_msgs;
        }
        Elements lines;
        for (auto& m : snap) lines.push_back(text(m));
        return vbox({
            hbox({ text(" "+username) | bold | color(Color::Green), text("  #"+room) | color(Color::Cyan) }) | border,
            vbox(std::move(lines)) | yframe | flex | border,
            hbox({ text(" > ") | bold, input->Render() }) | border,
        });
    }));

    sock.lowest_layer().close();
    ioc.stop();
    if (net.joinable()) net.join();
    return 0;
}
