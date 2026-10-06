// bench.runner — the keep-alive load generator.
//
// Method matches the published table: N concurrent clients, one open
// connection each, sequential requests, responses counted per second of wall
// time. Every route is measured in isolation.
module;
#include <cstdio>
export module bench.runner;

import std;
import asio;

export namespace bench {

struct Options {
    std::string   host        { "127.0.0.1" };
    std::uint16_t port        { 3000 };
    std::string   route       { "room" };   // room|messages|sidebar|search|post
    int           connections { 16 };
    double        seconds     { 5.0 };
};

std::expected<Options, std::string> parse_args(int argc, char** argv);

int run(const Options& options);

inline int bench_main(int argc, char** argv) {
    auto options { parse_args(argc, argv) };
    if (!options) {
        std::println(stderr, "error: {}", options.error());
        return 2;
    }
    return run(*options);
}

}

// ---- implementation: file-local, below the exported surface ----------------

namespace bench {

namespace {

constexpr std::string_view kRoomId { "aaaaaaaaaaaaaaaa" };

struct RequestSpec {
    std::string method;
    std::string target;
    std::string contentType;   // empty for GET
    std::string body;
};

std::expected<RequestSpec, std::string> spec_for_route_(std::string_view route) {
    const std::string roomId { kRoomId };
    if (route == "room")     return RequestSpec { "GET", "/" + roomId, { }, { } };
    if (route == "messages") return RequestSpec { "GET", "/" + roomId + "/messages", { }, { } };
    if (route == "sidebar")  return RequestSpec { "GET", "/" + roomId + "/sidebar", { }, { } };
    if (route == "search")   return RequestSpec { "GET", "/" + roomId + "/search?term=bench", { }, { } };
    if (route == "post") {
        return RequestSpec { "POST", "/" + roomId + "/messages",
                             "application/x-www-form-urlencoded",
                             "message%5Bcontent%5D=bench+message" };
    }
    return std::unexpected {
        std::format("unknown route '{}' (room|messages|sidebar|search|post)", route) };
}

// Reads one full response off a blocking socket; returns the status code or 0
// on protocol error.
int read_response_(asio::ip::tcp::socket& socket, std::string& scratch) {
    scratch.clear();
    std::array<char, 16 * 1024> chunk { };
    std::size_t headEnd { std::string::npos };
    while (true) {
        headEnd = scratch.find("\r\n\r\n");
        if (headEnd != std::string::npos) break;
        const auto n { socket.read_some(asio::buffer(chunk)) };
        if (n == 0) return 0;
        scratch.append(chunk.data(), n);
        if (scratch.size() > 4 * 1024 * 1024) return 0;
    }
    int status { 0 };
    if (scratch.starts_with("HTTP/1.1 ")) {
        const auto sp { scratch.find(' ', 9) };
        const auto end { sp == std::string::npos ? scratch.size() : sp };
        auto [p, ec] { std::from_chars(scratch.data() + 9, scratch.data() + end, status) };
        if (ec != std::errc{}) status = 0;
    }
    std::size_t contentLength { 0 };
    std::string_view head { scratch.data(), headEnd };
    std::size_t start { 0 };
    while (start < head.size()) {
        const auto eol { head.find("\r\n", start) };
        const std::string_view line {
            head.substr(start, eol == std::string_view::npos ? head.size() - start : eol - start)
        };
        if (line.starts_with("Content-Length:")) {
            auto value { line.substr(15) };
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
            }
            auto [p, ec] { std::from_chars(value.data(), value.data() + value.size(),
                                           contentLength) };
            if (ec != std::errc{}) contentLength = 0;
        }
        if (eol == std::string_view::npos) break;
        start = eol + 2;
    }
    while (scratch.size() - (headEnd + 4) < contentLength) {
        const auto n { socket.read_some(asio::buffer(chunk)) };
        if (n == 0) return 0;
        scratch.append(chunk.data(), n);
    }
    return status;
}

struct ClientResult {
    std::uint64_t requests  { 0 };
    std::uint64_t successes { 0 };
    std::vector<double> latencyUs { };
};

ClientResult run_client_(const Options& options, const RequestSpec& spec,
                         const std::atomic<bool>& stop) {
    ClientResult result { };
    result.latencyUs.reserve(64 * 1024);

    asio::io_context ctx { };
    asio::ip::tcp::socket socket { ctx };
    try {
        const auto endpoints { asio::ip::tcp::resolver { ctx }
            .resolve(options.host, std::to_string(options.port)) };
        asio::connect(socket, endpoints);
        socket.set_option(asio::ip::tcp::no_delay { true });
    } catch (const std::system_error& e) {
        std::println(stderr, "connect failed: {}", e.what());
        return result;
    }

    std::string request { };
    request.reserve(256 + spec.body.size());
    std::string scratch { };
    scratch.reserve(256 * 1024);

    while (!stop.load(std::memory_order_relaxed)) {
        request.clear();
        std::format_to(std::back_inserter(request),
                       "{} {} HTTP/1.1\r\nHost: {}:{}\r\nAccept: */*\r\n",
                       spec.method, spec.target, options.host, options.port);
        if (!spec.body.empty()) {
            std::format_to(std::back_inserter(request),
                           "Content-Type: {}\r\nContent-Length: {}\r\n",
                           spec.contentType, spec.body.size());
        }
        request.append("Connection: keep-alive\r\n\r\n");
        request.append(spec.body);

        const auto begin { std::chrono::steady_clock::now() };
        try {
            asio::write(socket, asio::buffer(request));
            const int status { read_response_(socket, scratch) };
            const auto end { std::chrono::steady_clock::now() };
            if (status == 0) break;
            ++result.requests;
            if (status >= 200 && status < 400) {
                ++result.successes;
            }
            result.latencyUs.push_back(
                std::chrono::duration<double, std::micro>(end - begin).count());
        } catch (const std::system_error&) {
            break;
        }
    }
    std::error_code ignored { };
    socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
    return result;
}

double percentile_(std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return 0.0;
    const auto index { static_cast<std::size_t>(
        fraction * static_cast<double>(sorted.size() - 1)) };
    return sorted[index];
}

}  // namespace

std::expected<Options, std::string> parse_args(int argc, char** argv) {
    Options options { };
    for (int i { 1 }; i < argc; ++i) {
        const std::string_view arg { argv[i] };
        const auto next = [&](std::string_view& out) -> bool {
            if (i + 1 >= argc) return false;
            out = argv[i + 1];
            return true;
        };
        if (arg == "--host") {
            std::string_view value { };
            if (!next(value)) return std::unexpected { "missing value for --host" };
            options.host = std::string { value };
            ++i;
        } else if (arg == "--port") {
            std::string_view value { };
            if (!next(value)) return std::unexpected { "missing value for --port" };
            std::uint16_t port { 0 };
            auto [p, ec] { std::from_chars(value.data(), value.data() + value.size(), port) };
            if (ec != std::errc{}) return std::unexpected { "invalid --port" };
            options.port = port;
            ++i;
        } else if (arg == "--route") {
            std::string_view value { };
            if (!next(value)) return std::unexpected { "missing value for --route" };
            options.route = std::string { value };
            ++i;
        } else if (arg == "--connections") {
            std::string_view value { };
            if (!next(value)) return std::unexpected { "missing value for --connections" };
            int connections { 0 };
            auto [p, ec] { std::from_chars(value.data(), value.data() + value.size(),
                                           connections) };
            if (ec != std::errc{} || connections <= 0) {
                return std::unexpected { "invalid --connections" };
            }
            options.connections = connections;
            ++i;
        } else if (arg == "--seconds") {
            std::string_view value { };
            if (!next(value)) return std::unexpected { "missing value for --seconds" };
            options.seconds = std::atof(std::string { value }.c_str());
            if (options.seconds <= 0) return std::unexpected { "invalid --seconds" };
            ++i;
        } else {
            return std::unexpected { std::format("unknown flag: '{}'", arg) };
        }
    }
    return options;
}

int run(const Options& options) {
    auto spec { spec_for_route_(options.route) };
    if (!spec) {
        std::println(stderr, "error: {}", spec.error());
        return 2;
    }

    std::println("bench: route={} connections={} duration={:.1f}s target={}:{}",
                 options.route, options.connections, options.seconds,
                 options.host, options.port);

    std::atomic<bool> stop { false };
    std::vector<std::future<ClientResult>> futures { };
    futures.reserve(static_cast<std::size_t>(options.connections));
    const auto wallBegin { std::chrono::steady_clock::now() };
    for (int i { 0 }; i < options.connections; ++i) {
        futures.push_back(std::async(std::launch::async, [&] {
            return run_client_(options, *spec, stop);
        }));
    }
    std::this_thread::sleep_for(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double>(options.seconds)));
    stop.store(true, std::memory_order_relaxed);

    std::uint64_t total { 0 };
    std::uint64_t successes { 0 };
    std::vector<double> latencies { };
    for (auto& future : futures) {
        auto result { future.get() };
        total     += result.requests;
        successes += result.successes;
        latencies.insert(latencies.end(), result.latencyUs.begin(),
                         result.latencyUs.end());
    }
    const double wall {
        std::chrono::duration<double>(std::chrono::steady_clock::now() - wallBegin).count() };
    std::sort(latencies.begin(), latencies.end());

    std::println("  requests     : {}", total);
    std::println("  successes    : {}", successes);
    std::println("  req/s        : {:.0f}", static_cast<double>(total) / wall);
    if (!latencies.empty()) {
        std::println("  p50 / p90 / p99 (µs): {:.0f} / {:.0f} / {:.0f}",
                     percentile_(latencies, 0.50), percentile_(latencies, 0.90),
                     percentile_(latencies, 0.99));
        std::println("  max (µs)     : {:.0f}", latencies.back());
    }
    return 0;
}

}
