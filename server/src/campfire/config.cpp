module campfire.config;

import std;

namespace campfire {

namespace {

std::expected<std::uint16_t, std::string> parse_port_(std::string_view text) {
    std::uint16_t value { 0 };
    const auto* first { text.data() };
    const auto* last  { text.data() + text.size() };
    const auto result { std::from_chars(first, last, value) };
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::unexpected { std::format("invalid port: '{}'", text) };
    }
    return value;
}

std::expected<int, std::string> parse_threads_(std::string_view text) {
    int value { 0 };
    const auto* first { text.data() };
    const auto* last  { text.data() + text.size() };
    const auto result { std::from_chars(first, last, value) };
    if (result.ec != std::errc{} || result.ptr != last || value < 0) {
        return std::unexpected { std::format("invalid thread count: '{}'", text) };
    }
    return value;
}

}  // namespace

std::string_view serve_usage() {
    return "usage: campfire [--host ADDR] [--port N] [--threads N]\n"
           "routes: GET /:roomId, /:roomId/messages, /:roomId/sidebar,\n"
           "        /:roomId/search, POST /:roomId/messages, GET /healthz\n"
           "load generator lives in the bench/ workspace member (target: loadgen)\n";
}

std::expected<Config, std::string> parse_serve_args(int argc, char** argv) {
    Config config { };
    for (int i { 1 }; i < argc; ++i) {
        const std::string_view arg { argv[i] };
        const auto value_of = [&](int& out) -> std::expected<std::string_view, std::string> {
            if (i + 1 >= argc) {
                return std::unexpected { std::format("missing value for {}", arg) };
            }
            out = i + 1;
            return std::string_view { argv[i + 1] };
        };
        if (arg == "--port") {
            int next { 0 };
            auto value { value_of(next) };
            if (!value) return std::unexpected { value.error() };
            auto port { parse_port_(*value) };
            if (!port) return std::unexpected { port.error() };
            config.port = *port;
            i = next;
        } else if (arg == "--threads") {
            int next { 0 };
            auto value { value_of(next) };
            if (!value) return std::unexpected { value.error() };
            auto threads { parse_threads_(*value) };
            if (!threads) return std::unexpected { threads.error() };
            config.threads = *threads;
            i = next;
        } else if (arg == "--host") {
            int next { 0 };
            auto value { value_of(next) };
            if (!value) return std::unexpected { value.error() };
            config.host = std::string { *value };
            i = next;
        } else {
            return std::unexpected { std::format("unknown flag: '{}'", arg) };
        }
    }
    return config;
}

}
