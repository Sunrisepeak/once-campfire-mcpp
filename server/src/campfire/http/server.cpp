module;
#include <csignal>
module campfire.http.server;

import std;
import asio;
import campfire.config;
import campfire.log;
import campfire.version;
import campfire.http.types;
import campfire.http.parser;
import campfire.http.router;

namespace campfire::http {

namespace {

constexpr std::size_t kInitialCapacity { 16 * 1024 };
constexpr std::size_t kMaxCapacity     { 4 * 1024 * 1024 };

// RFC 9110 IMF-fixdate, computed without <ctime> so the TU stays on the std
// module. Civil-from-days (Hinnant).
void append_http_date_(std::string& out) {
    const auto nowTicks { std::chrono::system_clock::now().time_since_epoch() };
    const auto days { std::chrono::floor<std::chrono::days>(nowTicks) };
    const auto daytime { std::chrono::hh_mm_ss {
        nowTicks - std::chrono::duration_cast<std::chrono::seconds>(days) } };
    const std::int64_t z { days.count() + 719'468 };
    const std::int64_t era { (z >= 0 ? z : z - 146'096) / 146'097 };
    const std::int64_t doe { z - era * 146'097 };
    const std::int64_t yoe { (doe - doe / 1'460 + doe / 36'524 - doe / 146'096) / 365 };
    const std::int64_t y { yoe + era * 400 };
    const std::int64_t doy { doe - (365 * yoe + yoe / 4 - yoe / 100) };
    const std::int64_t mp { (5 * doy + 2) / 153 };
    const std::int64_t d { doy - (153 * mp + 2) / 5 + 1 };
    const std::int64_t m { mp < 10 ? mp + 3 : mp - 9 };
    const std::int64_t yy { m <= 2 ? y + 1 : y };

    static constexpr std::string_view kDays[7] {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static constexpr std::string_view kMonths[12] {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    const int weekday { static_cast<int>((z + 4) % 7) };

    std::format_to(std::back_inserter(out), "{}, {:02} {} {} {:02}:{:02}:{:02} GMT",
                   kDays[weekday], d, kMonths[m - 1], yy,
                   daytime.hours().count(), daytime.minutes().count(),
                   daytime.seconds().count());
}

// Renders the head section (status line + headers + trailing blank line).
// Transfer-level headers (Content-Length, Connection, Server, Date) are owned
// here and never delegated to handlers.
std::string finalize_(const Response& response, bool keepAlive) {
    std::string out { };
    out.reserve(192 + response.headers.size() * 32);
    std::format_to(std::back_inserter(out), "HTTP/1.1 {} {}\r\n",
                   static_cast<int>(response.status), status_reason(response.status));
    for (const auto& h : response.headers) {
        std::format_to(std::back_inserter(out), "{}: {}\r\n", h.name, h.value);
    }
    if (response.status != Status::not_modified) {
        std::format_to(std::back_inserter(out), "Content-Length: {}\r\n", response.body.size());
    }
    std::format_to(std::back_inserter(out), "Connection: {}\r\nServer: {}\r\nDate: ",
                   keepAlive ? "keep-alive" : "close", kServerToken);
    append_http_date_(out);
    out.append("\r\n\r\n");
    return out;
}

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(asio::ip::tcp::socket socket, const Router& router)
        : socket_ { std::move(socket) }, router_ { router } {
        buffer_.resize(kInitialCapacity);
    }

    void start() {
        std::error_code ec;
        socket_.set_option(asio::ip::tcp::no_delay { true }, ec);
        do_read();
    }

private:
    void do_read() {
        if (filled_ == buffer_.size()) {
            if (buffer_.size() >= kMaxCapacity) {
                fail_(Status::bad_request, "request too large");
                return;
            }
            buffer_.resize(std::min(buffer_.size() * 2, kMaxCapacity));
        }
        auto self { shared_from_this() };
        socket_.async_read_some(
            asio::buffer(buffer_.data() + filled_, buffer_.size() - filled_),
            [this, self](std::error_code ec, std::size_t n) {
                if (ec) {
                    close_();
                    return;
                }
                filled_ += n;
                on_data_();
            });
    }

    void on_data_() {
        const std::string_view data { buffer_.data(), filled_ };
        std::size_t consumed { 0 };
        const auto result { parser_.parse(data, request_, consumed) };
        if (result == ParseResult::need_more) {
            do_read();
            return;
        }
        if (result == ParseResult::error) {
            fail_(Status::bad_request, parser_.error());
            return;
        }
        consumed_  = consumed;
        auto response { router_.dispatch(request_) };
        const bool keepAlive { request_.keepAlive };
        wire_ = finalize_(response, keepAlive);
        do_write_(std::move(response.body), keepAlive);
    }

    void do_write_(std::string body, bool keepAlive) {
        auto self { shared_from_this() };
        std::vector<asio::const_buffer> pieces { };
        pieces.push_back(asio::buffer(wire_));
        if (!body.empty()) {
            pieces.push_back(asio::buffer(body));
        }
        bodyStore_ = std::move(body);
        asio::async_write(socket_, pieces,
            [this, self, keepAlive](std::error_code ec, std::size_t) {
                bodyStore_.clear();
                bodyStore_.shrink_to_fit();
                if (ec || !keepAlive) {
                    close_();
                    return;
                }
                // Slide any pipelined bytes of the next request to the front.
                if (consumed_ > 0) {
                    const std::size_t rest { filled_ - consumed_ };
                    if (rest > 0) {
                        std::memmove(buffer_.data(), buffer_.data() + consumed_, rest);
                    }
                    filled_   = rest;
                    consumed_ = 0;
                }
                on_data_();
            });
    }

    void fail_(Status status, std::string_view reason) {
        auto response { Response::text(status, std::format("{}\n", reason)) };
        wire_ = finalize_(response, false);
        do_write_(std::move(response.body), false);
    }

    void close_() {
        std::error_code ignored { };
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);
    }

    asio::ip::tcp::socket socket_;
    const Router&         router_    { };
    RequestParser         parser_    { };
    Request               request_   { };
    std::string           buffer_    { };
    std::string           bodyStore_ { };
    std::string           wire_      { };
    std::size_t           filled_    { 0 };
    std::size_t           consumed_  { 0 };
};

class Acceptor {
public:
    Acceptor(asio::ip::tcp::acceptor& acceptor, const Router& router)
        : acceptor_ { acceptor }, router_ { router } { }

    void start() {
        accept();
    }

private:
    void accept() {
        acceptor_.async_accept(
            [this](std::error_code ec, asio::ip::tcp::socket socket) {
                if (!ec) {
                    std::make_shared<Connection>(std::move(socket), router_)->start();
                } else if (ec == asio::error::operation_aborted) {
                    return;
                }
                accept();
            });
    }

    asio::ip::tcp::acceptor& acceptor_;
    const Router&            router_;
};

}  // namespace

int serve(const Config& config, const Router& router) {
    asio::io_context ctx { };
    auto work { asio::make_work_guard(ctx) };

    try {
        asio::ip::tcp::acceptor acceptor { ctx };
        const auto address { asio::ip::make_address(config.host) };
        const asio::ip::tcp::endpoint endpoint { address, config.port };
        acceptor.open(endpoint.protocol());
        acceptor.set_option(asio::socket_base::reuse_address { true });
        acceptor.bind(endpoint);
        acceptor.listen(asio::socket_base::max_listen_connections);

        int threadCount { config.threads };
        if (threadCount <= 0) {
            threadCount = static_cast<int>(std::thread::hardware_concurrency());
            threadCount = std::max(threadCount, 1);
        }

        log_info(std::format("listening on http://{}:{} with {} io thread(s)",
                             config.host, config.port, threadCount));

        std::vector<std::thread> workers { };
        workers.reserve(static_cast<std::size_t>(std::max(threadCount - 1, 0)));
        for (int i { 1 }; i < threadCount; ++i) {
            workers.emplace_back([&ctx] { ctx.run(); });
        }

        asio::signal_set signals { ctx, SIGINT, SIGTERM };
        signals.async_wait([&](std::error_code, int signalNumber) {
            log_info(std::format("received signal {}, shutting down", signalNumber));
            acceptor.close();
            ctx.stop();
        });

        Acceptor loop { acceptor, router };
        loop.start();
        ctx.run();

        for (auto& worker : workers) {
            worker.join();
        }
    } catch (const std::system_error& e) {
        log_error(std::format("fatal: {}", e.what()));
        return 1;
    }
    return 0;
}

}
