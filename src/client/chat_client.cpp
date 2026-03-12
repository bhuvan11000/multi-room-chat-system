#include <iostream>
#include <thread>
#include <deque>
#include <cstring>
#include <mutex>
#include <boost/asio.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include "../common/protocol.hpp"

using boost::asio::ip::tcp;
using namespace ftxui;
using namespace chat;

std::mutex               log_mtx;
std::vector<std::string> log_msgs;
void push(const std::string& s) {
    std::lock_guard<std::mutex> lk(log_mtx);
    log_msgs.push_back(s);
}

// ── send a framed message ─────────────────────────────────────────────────────
void send_msg(tcp::socket& sock, boost::asio::io_context& ioc,
              std::deque<std::vector<uint8_t>>& q, MessageType t, const std::string& body) {
    boost::asio::post(ioc, [&, t, body]() {
        bool busy = !q.empty();
        MessageHeader h{ static_cast<uint8_t>(t), static_cast<uint32_t>(body.size()) };
        std::vector<uint8_t> msg(HEADER_SIZE + body.size());
        std::memcpy(msg.data(), &h, HEADER_SIZE);
        std::memcpy(msg.data() + HEADER_SIZE, body.data(), body.size());
        q.push_back(std::move(msg));
        if (!busy) {
            std::function<void()> do_write = [&]() {
                boost::asio::async_write(sock, boost::asio::buffer(q.front()),
                    [&, do_write](boost::system::error_code ec, std::size_t) {
                        if (!ec) { q.pop_front(); if (!q.empty()) do_write(); }
                    });
            };
            do_write();
        }
    });
}

int main(int argc, char* argv[]) {
    if (argc != 3) { std::cerr << "Usage: chat_client <host> <port>\n"; return 1; }

    boost::asio::io_context ioc;
    tcp::socket sock(ioc);
    std::deque<std::vector<uint8_t>> wq;
    boost::asio::connect(sock, tcp::resolver(ioc).resolve(argv[1], argv[2]));

    // read loop runs on net thread
    MessageHeader rh{};
    std::vector<uint8_t> rb;
    std::function<void()> read_hdr, read_body;
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
                if (!ec) { push(std::string(rb.begin(), rb.end())); read_hdr(); }
            });
    };
    read_hdr();
    std::thread net([&]() { ioc.run(); });

    std::string username;
    std::cout << "Username: ";
    std::getline(std::cin, username);
    send_msg(sock, ioc, wq, MessageType::LOGIN, username);

    // ── TUI ──────────────────────────────────────────────────────────────────
    auto screen = ScreenInteractive::Fullscreen();
    std::string input_text, room = "(none)";

    auto input = Input(&input_text, "/create  /join  /private  or message");
    input |= CatchEvent([&](Event e) {
        if (e != Event::Return || input_text.empty()) return false;
        std::string line = input_text; input_text.clear();
        if      (line.substr(0,8)=="/create ") { room=line.substr(8); send_msg(sock,ioc,wq,MessageType::CREATE_ROOM,room); }
        else if (line.substr(0,6)=="/join ")   { room=line.substr(6); send_msg(sock,ioc,wq,MessageType::JOIN_ROOM,room); }
        else if (line.substr(0,9)=="/private ")  send_msg(sock,ioc,wq,MessageType::PRIVATE_MSG,line.substr(9));
        else if (line=="/quit")                  screen.Exit();
        else                                     send_msg(sock,ioc,wq,MessageType::CHAT_MSG,line);
        return true;
    });

    std::thread([&]() {
        while(true) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); screen.PostEvent(Event::Custom); }
    }).detach();

    screen.Loop(Renderer(input, [&]() {
        std::vector<std::string> snap; { std::lock_guard<std::mutex> lk(log_mtx); snap=log_msgs; }
        Elements lines; for (auto& m : snap) lines.push_back(text(m));
        return vbox({
            hbox({ text(" "+username) | bold | color(Color::Green), text("  #"+room) | color(Color::Cyan) }) | border,
            vbox(std::move(lines)) | yframe | flex | border,
            hbox({ text(" > ") | bold, input->Render() }) | border,
        });
    }));

    sock.close(); ioc.stop(); net.join();
}
