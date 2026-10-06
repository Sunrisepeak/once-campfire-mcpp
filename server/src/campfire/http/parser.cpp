module;
#include <cctype>
module campfire.http.parser;

import std;
import campfire.http.types;

namespace campfire::http {

namespace {

constexpr bool equals_ci_(std::string_view a, std::string_view b) {
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(),
                      [](char x, char y) {
                          return std::tolower(static_cast<unsigned char>(x))
                              == std::tolower(static_cast<unsigned char>(y));
                      });
}

std::string_view trim_(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

std::expected<std::size_t, std::string_view> parse_digits_(std::string_view text) {
    if (text.empty()) return std::unexpected { "empty number" };
    std::size_t value { 0 };
    for (const char c : text) {
        if (c < '0' || c > '9') return std::unexpected { "not a number" };
        value = value * 10 + static_cast<std::size_t>(c - '0');
        if (value > (std::size_t { 1 } << 32)) return std::unexpected { "number too large" };
    }
    return value;
}

}  // namespace

void RequestParser::reset() {
    state_         = State::head;
    headEnd_       = 0;
    contentLength_ = 0;
    errorMessage_.clear();
}

ParseResult RequestParser::parse(std::string_view data, Request& req, std::size_t& consumed) {
    if (state_ == State::head) {
        const auto headEnd { data.find("\r\n\r\n") };
        if (headEnd == std::string_view::npos) {
            if (data.size() > limits_.maxHeadBytes) {
                errorMessage_ = "head section too large";
                return ParseResult::error;
            }
            return ParseResult::need_more;
        }
        headEnd_ = headEnd + 4;

        req = Request { };
        const std::string_view head { data.substr(0, headEnd) };

        std::size_t lineCount { 0 };
        std::size_t lineStart { 0 };
        while (lineStart <= head.size()) {
            if (lineCount > 128) {
                errorMessage_ = "too many header lines";
                return ParseResult::error;
            }
            const auto lineEnd { head.find("\r\n", lineStart) };
            const bool last { lineEnd == std::string_view::npos };
            const std::string_view line {
                head.substr(lineStart, last ? head.size() - lineStart : lineEnd - lineStart)
            };
            if (lineCount == 0) {
                // request-line: METHOD SP request-target SP HTTP/x.y
                const auto sp1 { line.find(' ') };
                const auto sp2 { sp1 == std::string_view::npos
                                     ? std::string_view::npos
                                     : line.find(' ', sp1 + 1) };
                if (sp1 == std::string_view::npos || sp2 == std::string_view::npos) {
                    errorMessage_ = "malformed request line";
                    return ParseResult::error;
                }
                req.method = method_from_string(line.substr(0, sp1));
                if (req.method == Method::unknown) {
                    errorMessage_ = "unknown method";
                    return ParseResult::error;
                }
                req.target = line.substr(sp1 + 1, sp2 - sp1 - 1);
                const std::string_view version { line.substr(sp2 + 1) };
                if (version == "HTTP/1.1") {
                    req.minorVersion = 1;
                } else if (version == "HTTP/1.0") {
                    req.minorVersion = 0;
                } else {
                    errorMessage_ = "unsupported HTTP version";
                    return ParseResult::error;
                }
                const auto queryStart { req.target.find('?') };
                if (queryStart == std::string_view::npos) {
                    req.path  = req.target;
                    req.query = { };
                } else {
                    req.path  = req.target.substr(0, queryStart);
                    req.query = req.target.substr(queryStart + 1);
                }
            } else if (!line.empty()) {
                const auto colon { line.find(':') };
                if (colon == std::string_view::npos) {
                    errorMessage_ = "malformed header line";
                    return ParseResult::error;
                }
                const std::string_view name  { trim_(line.substr(0, colon)) };
                const std::string_view value { trim_(line.substr(colon + 1)) };
                if (equals_ci_(name, "Content-Length")) {
                    auto parsed { parse_digits_(value) };
                    if (!parsed) {
                        errorMessage_ = "malformed Content-Length";
                        return ParseResult::error;
                    }
                    contentLength_ = *parsed;
                } else if (equals_ci_(name, "Connection")) {
                    if (equals_ci_(value, "close")) {
                        req.keepAlive = false;
                    } else if (equals_ci_(value, "keep-alive")) {
                        req.keepAlive = true;
                    }
                } else if (equals_ci_(name, "Transfer-Encoding")) {
                    errorMessage_ = "chunked transfer encoding not supported";
                    return ParseResult::error;
                }
                req.headers.push_back(HeaderIn { name, value });
            }
            if (last) break;
            lineStart = lineEnd + 2;
            ++lineCount;
        }

        if (req.minorVersion == 0 && req.keepAlive) {
            req.keepAlive = req.header("Connection") == "keep-alive";
        }
        if (contentLength_ > limits_.maxBodyBytes) {
            errorMessage_ = "body too large";
            return ParseResult::error;
        }
        state_ = State::body;
    }

    if (data.size() < headEnd_ + contentLength_) {
        return ParseResult::need_more;
    }
    req.body    = data.substr(headEnd_, contentLength_);
    consumed    = headEnd_ + contentLength_;
    reset();
    return ParseResult::ok;
}

}
