// perf_test.cpp — place at src/perf/perf_test.cpp
// Usage: start chat_server manually first, then: ./perf_test <host> <port>

#include <iostream>
#include <chrono>
#include <cstring>
#include <vector>
#include <string>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "../common/protocol.hpp"

using boost::asio::ip::tcp;
using namespace chat;
using namespace std::chrono;

// ---------------------------------------------------------------------------
// Low-level send / recv
// ---------------------------------------------------------------------------

static boost::asio::ssl::context make_ctx() {
    boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12_client);
    ctx.set_verify_mode(boost::asio::ssl::verify_none); // test env only
    return ctx;
}

static void send_msg(boost::asio::ssl::stream<tcp::socket>& s,
                     MessageType t, const std::string& body) {
    MessageHeader h{ static_cast<uint8_t>(t), static_cast<uint32_t>(body.size()) };
    std::vector<uint8_t> buf(HEADER_SIZE + body.size());
    std::memcpy(buf.data(),               &h,          HEADER_SIZE);
    std::memcpy(buf.data() + HEADER_SIZE, body.data(), body.size());
    boost::asio::write(s, boost::asio::buffer(buf));
}

// Returns {type, body} of one message
static std::pair<MessageType, std::string>
recv_one(boost::asio::ssl::stream<tcp::socket>& s) {
    MessageHeader h{};
    boost::asio::read(s, boost::asio::buffer(&h, HEADER_SIZE));
    std::vector<uint8_t> body(h.length);
    if (h.length)
        boost::asio::read(s, boost::asio::buffer(body.data(), body.size()));
    return { static_cast<MessageType>(h.type),
             std::string(body.begin(), body.end()) };
}

// ---------------------------------------------------------------------------
// Timed drain
// ---------------------------------------------------------------------------
// Reads and discards messages until the socket produces nothing for
// `quiet_ms` milliseconds. Solves the hardcoded-count hangon problem:
// we don't care how many broadcast messages the server sent during setup,
// we just wait until it goes quiet before starting measurements.

static int timed_drain(boost::asio::ssl::stream<tcp::socket>& s,
                       boost::asio::io_context& ioc,
                       int quiet_ms = 80) {
    int count = 0;
    boost::system::error_code ec;

    while (true) {
        // Set a short async timeout using a deadline timer + async_read race
        bool got_data    = false;
        bool timed_out   = false;

        boost::asio::steady_timer timer(ioc, std::chrono::milliseconds(quiet_ms));

        MessageHeader h{};
        // Async read header
        boost::asio::async_read(s, boost::asio::buffer(&h, HEADER_SIZE),
            [&](boost::system::error_code e, std::size_t) {
                if (!e) { got_data = true; timer.cancel(); }
            });

        // Async timer
        timer.async_wait([&](boost::system::error_code e) {
            if (!e) { timed_out = true; s.lowest_layer().cancel(); }
        });

        ioc.restart();
        ioc.run();

        if (timed_out) break; // socket quiet — we're done draining

        // Got a header — read the body and discard
        if (got_data && h.length > 0) {
            std::vector<uint8_t> body(h.length);
            boost::asio::read(s, boost::asio::buffer(body.data(), body.size()), ec);
        }
        ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void separator() { std::cout << std::string(55, '-') << "\n"; }

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: perf_test <host> <port>\n";
        return 1;
    }

    const int N_THROUGHPUT = 1000;
    const int N_PINGS      = 100;

    try {
        boost::asio::io_context ioc;
        auto ctx = make_ctx();
        boost::asio::ssl::stream<tcp::socket> sock(ioc, ctx);

        // ── Connect ──────────────────────────────────────────────────────────
        tcp::resolver resolver(ioc);
        boost::asio::connect(sock.lowest_layer(),
                             resolver.resolve(argv[1], argv[2]));
        sock.handshake(boost::asio::ssl::stream_base::client);
        std::cout << "Connected to " << argv[1] << ":" << argv[2] << "\n\n";

        // ── Login ─────────────────────────────────────────────────────────────
        // Send login then drain until server goes quiet (handles variable
        // number of LIST_ROOMS / LIST_USERS broadcasts gracefully).
        send_msg(sock, MessageType::LOGIN, "perf_user");
        int drained = timed_drain(sock, ioc);
        std::cout << "[Setup] Login: drained " << drained << " server message(s)\n";

        // ── Create room ───────────────────────────────────────────────────────
        send_msg(sock, MessageType::CREATE_ROOM, "perf_room");
        drained = timed_drain(sock, ioc);
        std::cout << "[Setup] Create room: drained " << drained << " server message(s)\n";

        separator();

        // ── TEST 1: Throughput ────────────────────────────────────────────────
        // Send N messages back-to-back, then drain all echoes.
        // Measures end-to-end: send burst + receive all echoes.
        std::cout << "[Test 1] Throughput: sending " << N_THROUGHPUT << " messages...\n";

        auto t0 = steady_clock::now();
        for (int i = 0; i < N_THROUGHPUT; ++i)
            send_msg(sock, MessageType::CHAT_MSG, "hello " + std::to_string(i));
        for (int i = 0; i < N_THROUGHPUT; ++i)
            recv_one(sock); // drain echoes — count is exact here, no ambiguity
        auto t1 = steady_clock::now();

        double secs = duration_cast<microseconds>(t1 - t0).count() / 1e6;
        std::cout << "[Test 1] " << N_THROUGHPUT << " msgs in "
                  << std::fixed << std::setprecision(3) << secs << "s  →  "
                  << std::setprecision(0) << (N_THROUGHPUT / secs) << " msg/s\n";

        separator();

        // ── TEST 2: Round-trip latency ────────────────────────────────────────
        // One send → wait for echo → measure. Repeat N_PINGS times.
        // Reports avg / min / max.
        std::cout << "[Test 2] Latency: " << N_PINGS << " ping-pong round-trips...\n";

        std::vector<double> samples;
        samples.reserve(N_PINGS);

        for (int i = 0; i < N_PINGS; ++i) {
            auto p0 = steady_clock::now();
            send_msg(sock, MessageType::CHAT_MSG, "ping");
            recv_one(sock);
            double ms = duration_cast<microseconds>(steady_clock::now() - p0).count() / 1000.0;
            samples.push_back(ms);
        }

        double avg = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
        double mn  = *std::min_element(samples.begin(), samples.end());
        double mx  = *std::max_element(samples.begin(), samples.end());

        std::cout << std::fixed << std::setprecision(3)
                  << "[Test 2] avg=" << avg << "ms"
                  << "  min=" << mn  << "ms"
                  << "  max=" << mx  << "ms\n";

        separator();
        std::cout << "Done.\n";

        sock.lowest_layer().close();

    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
