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
#include <thread>
#include <mutex>
#include <atomic>
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
        bool got_data    = false;
        bool timed_out   = false;

        boost::asio::steady_timer timer(ioc, std::chrono::milliseconds(quiet_ms));

        MessageHeader h{};
        boost::asio::async_read(s, boost::asio::buffer(&h, HEADER_SIZE),
            [&](boost::system::error_code e, std::size_t) {
                if (!e) { got_data = true; timer.cancel(); }
            });

        timer.async_wait([&](boost::system::error_code e) {
            if (!e) { timed_out = true; s.lowest_layer().cancel(); }
        });

        ioc.restart();
        ioc.run();

        if (timed_out) break;

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
// Test 3: Concurrent clients
// ---------------------------------------------------------------------------
// Each thread independently connects, logs in, creates its own room, sends
// N messages, and records its own throughput. The caller aggregates results.
//
// NOTE: The server's RoomManager must be mutex-protected for this test to
// be meaningful (concurrent map writes are UB without synchronisation).

struct ClientResult {
    int    client_id   = 0;
    int    msgs_sent   = 0;
    double secs        = 0.0;
    double throughput  = 0.0;   // msg/s
    bool   ok          = false;
    std::string error;
};

static ClientResult run_concurrent_client(const std::string& host,
                                          const std::string& port,
                                          int client_id,
                                          int n_msgs) {
    ClientResult r;
    r.client_id = client_id;
    r.msgs_sent = n_msgs;
    try {
        boost::asio::io_context ioc;
        auto ctx = make_ctx();
        boost::asio::ssl::stream<tcp::socket> sock(ioc, ctx);

        tcp::resolver resolver(ioc);
        boost::asio::connect(sock.lowest_layer(),
                             resolver.resolve(host, port));
        sock.handshake(boost::asio::ssl::stream_base::client);

        std::string uname = "perf_user_" + std::to_string(client_id);
        std::string rname = "perf_room_" + std::to_string(client_id);

        send_msg(sock, MessageType::LOGIN, uname);
        timed_drain(sock, ioc);
        send_msg(sock, MessageType::CREATE_ROOM, rname);
        timed_drain(sock, ioc);

        auto t0 = steady_clock::now();
        for (int i = 0; i < n_msgs; ++i)
            send_msg(sock, MessageType::CHAT_MSG, "hello " + std::to_string(i));
        for (int i = 0; i < n_msgs; ++i)
            recv_one(sock);
        r.secs       = duration_cast<microseconds>(steady_clock::now() - t0).count() / 1e6;
        r.throughput = n_msgs / r.secs;
        r.ok         = true;
        sock.lowest_layer().close();
    } catch (std::exception& e) {
        r.error = e.what();
    }
    return r;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: perf_test <host> <port>\n";
        return 1;
    }

    const std::string host = argv[1];
    const std::string port = argv[2];

    const int N_THROUGHPUT   = 1000;
    const int N_PINGS        = 100;
    const int N_CONCURRENT   = 5;    // Test 3: simultaneous clients
    const int N_CONCURRENT_MSGS = 200; // msgs per concurrent client
    const int LARGE_PAYLOAD_SIZE = 4096; // Test 4: bytes per message
    const int N_LARGE        = 200;   // Test 4: message count

    try {
        boost::asio::io_context ioc;
        auto ctx = make_ctx();
        boost::asio::ssl::stream<tcp::socket> sock(ioc, ctx);

        // ── Connect ──────────────────────────────────────────────────────────
        tcp::resolver resolver(ioc);
        boost::asio::connect(sock.lowest_layer(),
                             resolver.resolve(host, port));
        sock.handshake(boost::asio::ssl::stream_base::client);
        std::cout << "Connected to " << host << ":" << port << "\n\n";

        // ── Login ─────────────────────────────────────────────────────────────
        send_msg(sock, MessageType::LOGIN, "perf_user");
        int drained = timed_drain(sock, ioc);
        std::cout << "[Setup] Login: drained " << drained << " server message(s)\n";

        // ── Create room ───────────────────────────────────────────────────────
        send_msg(sock, MessageType::CREATE_ROOM, "perf_room");
        drained = timed_drain(sock, ioc);
        std::cout << "[Setup] Create room: drained " << drained << " server message(s)\n";

        separator();

        // ── TEST 1: Throughput ────────────────────────────────────────────────
        std::cout << "[Test 1] Throughput: sending " << N_THROUGHPUT << " messages...\n";

        auto t0 = steady_clock::now();
        for (int i = 0; i < N_THROUGHPUT; ++i)
            send_msg(sock, MessageType::CHAT_MSG, "hello " + std::to_string(i));
        for (int i = 0; i < N_THROUGHPUT; ++i)
            recv_one(sock);
        auto t1 = steady_clock::now();

        double secs = duration_cast<microseconds>(t1 - t0).count() / 1e6;
        std::cout << "[Test 1] " << N_THROUGHPUT << " msgs in "
                  << std::fixed << std::setprecision(3) << secs << "s  →  "
                  << std::setprecision(0) << (N_THROUGHPUT / secs) << " msg/s\n";

        separator();

        // ── TEST 2: Round-trip latency ────────────────────────────────────────
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

        // ── TEST 3: Concurrent clients ────────────────────────────────────────
        // Spawns N_CONCURRENT threads, each independently connecting and
        // sending N_CONCURRENT_MSGS messages. Measures per-client throughput
        // and total aggregate throughput to assess scalability under load.
        std::cout << "[Test 3] Concurrent clients: " << N_CONCURRENT
                  << " clients × " << N_CONCURRENT_MSGS << " msgs each...\n";

        std::vector<ClientResult> results(N_CONCURRENT);
        std::vector<std::thread> threads;
        threads.reserve(N_CONCURRENT);

        auto tc0 = steady_clock::now();
        for (int i = 0; i < N_CONCURRENT; ++i) {
            threads.emplace_back([&, i]() {
                results[i] = run_concurrent_client(host, port, i, N_CONCURRENT_MSGS);
            });
        }
        for (auto& t : threads) t.join();
        double wall = duration_cast<microseconds>(steady_clock::now() - tc0).count() / 1e6;

        double total_msgs = 0, total_tp = 0;
        for (auto& r : results) {
            if (r.ok) {
                std::cout << "  client " << r.client_id << ": "
                          << std::fixed << std::setprecision(0) << r.throughput << " msg/s"
                          << " (" << std::setprecision(3) << r.secs << "s)\n";
                total_msgs += r.msgs_sent;
                total_tp   += r.throughput;
            } else {
                std::cout << "  client " << r.client_id << ": FAILED — " << r.error << "\n";
            }
        }
        std::cout << "[Test 3] aggregate throughput: "
                  << std::fixed << std::setprecision(0) << total_tp << " msg/s  |  "
                  << "wall time: " << std::setprecision(3) << wall << "s\n";
        std::cout << "         (scalability ratio vs Test 1: "
                  << std::setprecision(2) << (total_tp / (N_THROUGHPUT / secs)) << "×)\n";

        separator();

        // ── TEST 4: Large payload throughput ─────────────────────────────────
        // Sends N_LARGE messages each carrying LARGE_PAYLOAD_SIZE bytes.
        // Stresses the serialisation / TLS record layer under high data volume
        // rather than high message rate. Reports both msg/s and MB/s.
        std::cout << "[Test 4] Large payload: " << N_LARGE << " × "
                  << LARGE_PAYLOAD_SIZE << "-byte messages...\n";

        std::string large_body(LARGE_PAYLOAD_SIZE, 'x');

        auto tl0 = steady_clock::now();
        for (int i = 0; i < N_LARGE; ++i)
            send_msg(sock, MessageType::CHAT_MSG, large_body);
        for (int i = 0; i < N_LARGE; ++i)
            recv_one(sock);
        double lsecs = duration_cast<microseconds>(steady_clock::now() - tl0).count() / 1e6;

        double bytes_total = static_cast<double>(N_LARGE) * LARGE_PAYLOAD_SIZE;
        std::cout << std::fixed << std::setprecision(3)
                  << "[Test 4] " << N_LARGE << " msgs in " << lsecs << "s  →  "
                  << std::setprecision(0) << (N_LARGE / lsecs) << " msg/s  |  "
                  << std::setprecision(2) << (bytes_total / lsecs / 1e6) << " MB/s\n";

        separator();
        std::cout << "Done.\n";

        sock.lowest_layer().close();

    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
