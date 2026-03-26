#include <iostream>
#include <thread>
#include <deque>
#include <cstring>
#include <mutex>
#include <functional>
#include <fstream>
#include <sstream>
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
std::mutex state_mtx;
std::vector<std::string> available_rooms;
std::vector<std::string> online_users;
std::string current_room = "(none)";

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
    auto screen = ScreenInteractive::Fullscreen();

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
                    } else if (type == MessageType::LIST_ROOMS) {
                        std::lock_guard<std::mutex> lk(state_mtx);
                        available_rooms.clear();
                        std::stringstream ss(body);
                        std::string item;
                        while(std::getline(ss, item, ',')) available_rooms.push_back(item);
                        screen.PostEvent(Event::Custom);
                    } else if (type == MessageType::LIST_USERS) {
                        std::lock_guard<std::mutex> lk(state_mtx);
                        online_users.clear();
                        std::stringstream ss(body);
                        std::string item;
                        while(std::getline(ss, item, ',')) online_users.push_back(item);
                        screen.PostEvent(Event::Custom);
                    } else if (type == MessageType::JOIN_ROOM) {
                        std::lock_guard<std::mutex> lk(state_mtx);
                        current_room = body;
                        screen.PostEvent(Event::Custom);
                    } else {
                        push(body);
                        screen.PostEvent(Event::Custom);
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

    std::string input_text;

    auto input = Input(&input_text, "Type /create <room>, /join <room>, /private <user> <msg>, /sendfile <path> or message...");
    input |= CatchEvent([&](Event e) {
        if (e != Event::Return || input_text.empty()) return false;
        std::string line = input_text;
        input_text.clear();
        if      (line.substr(0,8)=="/create ") { send_msg(sock,ioc,wq,MessageType::CREATE_ROOM,line.substr(8)); }
        else if (line.substr(0,6)=="/join ")   { send_msg(sock,ioc,wq,MessageType::JOIN_ROOM,line.substr(6)); }
        else if (line.substr(0,9)=="/private ")  send_msg(sock,ioc,wq,MessageType::PRIVATE_MSG,line.substr(9));
        else if (line.substr(0,10)=="/sendfile ") send_file(sock,ioc,wq,line.substr(10));
        else if (line=="/quit")                  screen.Exit();
        else                                     send_msg(sock,ioc,wq,MessageType::CHAT_MSG,line);
        return true;
    });

    auto renderer = Renderer(input, [&]() -> Element {
        std::vector<std::string> chat_snap, rooms_snap, users_snap;
        std::string room_name;
        {
            std::lock_guard<std::mutex> lk(log_mtx);
            chat_snap = log_msgs;
        }
        {
            std::lock_guard<std::mutex> lk(state_mtx);
            rooms_snap = available_rooms;
            users_snap = online_users;
            room_name = current_room;
        }

        auto make_list = [](const std::string& title, const std::vector<std::string>& items, Color c) {
            Elements e;
            e.push_back(text(title) | bold | underlined | color(c));
            for (const auto& i : items) e.push_back(text(" " + i));
            return vbox(std::move(e)) | flex;
        };

        Elements lines;
        for (auto& m : chat_snap) {
            if (m.find("[Error]") != std::string::npos) lines.push_back(text(m) | color(Color::Red));
            else if (m.find("[Info]") != std::string::npos) lines.push_back(text(m) | color(Color::Yellow));
            else if (m.find("[Server]") != std::string::npos) lines.push_back(text(m) | color(Color::BlueLight));
            else if (m.find("[Private") != std::string::npos) lines.push_back(text(m) | color(Color::Magenta));
            else lines.push_back(text(m));
        }

        auto sidebar = vbox({
            make_list("ROOMS", rooms_snap, Color::Cyan),
            separator(),
            make_list("USERS", users_snap, Color::Green),
        }) | size(WIDTH, GREATER_THAN, 20) | border;

        auto main_chat = vbox({
            hbox({ 
                text(" USER: ") | bold, text(username) | color(Color::Green),
                text("  ROOM: ") | bold, text("#" + room_name) | color(Color::Cyan),
                filler()
            }) | border,
            vbox(std::move(lines)) | yframe | flex | border,
            hbox({ text(" > ") | bold, input->Render() }) | border,
        }) | flex;

        return hbox({
            main_chat,
            sidebar
        });
    });

    screen.Loop(renderer);

    sock.lowest_layer().close();
    ioc.stop();
    if (net.joinable()) net.join();
    return 0;
}
