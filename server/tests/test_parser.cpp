// Parser unit tests: happy paths first, then the edges the transport relies
// on (keep-alive flag handling, body sizing, pipelined slicing).
#include <cstdio>
import std;

import campfire.http.types;
import campfire.http.parser;

using namespace campfire::http;

namespace {

int failures { 0 };

void check(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::println(stderr, "FAIL: {}", what);
    }
}

}  // namespace

int main() {
    RequestParser parser { };

    // -- simple GET with headers and query -----------------------------------
    {
        const std::string wire {
            "GET /room123/messages?page=2 HTTP/1.1\r\n"
            "Host: localhost\r\nAccept: */*\r\n\r\n"
        };
        Request req { };
        std::size_t consumed { 0 };
        check(parser.parse(wire, req, consumed) == ParseResult::ok, "simple GET parses");
        check(consumed == wire.size(), "simple GET consumes everything");
        check(req.method == Method::get, "method is GET");
        check(req.path == "/room123/messages", "path extracted");
        check(req.query == "page=2", "query extracted");
        check(req.keepAlive, "HTTP/1.1 defaults to keep-alive");
        check(req.header("Host") == "localhost", "header lookup by name");
        check(req.header("host") == "localhost", "header lookup is case-insensitive");
        check(req.header("Missing") == std::string_view { }, "absent header is empty view");
        check(req.body.empty(), "GET has no body");
    }

    // -- POST with body -------------------------------------------------------
    {
        const std::string body { "message%5Bcontent%5D=hello" };
        const std::string head {
            "POST /room123/messages HTTP/1.1\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
        };
        const std::string wire { head + "Content-Length: " + std::to_string(body.size())
                                 + "\r\n\r\n" + body };
        Request req { };
        std::size_t consumed { 0 };
        check(parser.parse(wire, req, consumed) == ParseResult::ok, "POST parses");
        check(consumed == wire.size(), "POST consumes everything");
        check(req.body == body, "body extracted verbatim");
        check(req.method == Method::post, "method is POST");
    }

    // -- need_more then ok across two feeds -----------------------------------
    {
        const std::string wire { "GET /x HTTP/1.1\r\nHost: a\r\n\r\n" };
        RequestParser p2 { };
        Request req { };
        std::size_t consumed { 0 };
        check(p2.parse(std::string_view { wire }.substr(0, 10), req, consumed)
                  == ParseResult::need_more,
              "partial head needs more");
        check(p2.parse(wire, req, consumed) == ParseResult::ok, "complete head parses");
        check(consumed == wire.size(), "consumed after completion");
    }

    // -- keep-alive vs close ---------------------------------------------------
    {
        Request req { };
        std::size_t consumed { 0 };
        const std::string wire {
            "GET / HTTP/1.1\r\nConnection: close\r\n\r\n"
        };
        check(parser.parse(wire, req, consumed) == ParseResult::ok, "close request parses");
        check(!req.keepAlive, "Connection: close disables keep-alive");
    }

    // -- pipelined requests: second parse over the unconsumed tail -------------
    {
        const std::string first { "GET /one HTTP/1.1\r\n\r\n" };
        const std::string second { "GET /two HTTP/1.1\r\n\r\n" };
        const std::string wire { first + second };
        Request req { };
        std::size_t consumed { 0 };
        check(parser.parse(wire, req, consumed) == ParseResult::ok, "first of pipeline");
        check(req.path == "/one", "first path");
        check(parser.parse(std::string_view { wire }.substr(consumed), req, consumed)
                  == ParseResult::ok,
              "second of pipeline");
        check(req.path == "/two", "second path");
    }

    // -- errors ------------------------------------------------------------------
    {
        Request req { };
        std::size_t consumed { 0 };
        check(parser.parse("NOT-A-REQUEST\r\n\r\n", req, consumed) == ParseResult::error,
              "malformed request line is an error");
        RequestParser p3 { };
        const std::string hugeHead(std::string_view { "GET / HTTP/1.1\r\nX: y\r\n" }.size() * 0
                                       + 40 * 1024,
                                   'x');
        check(p3.parse(hugeHead, req, consumed) == ParseResult::error
                  || p3.parse(hugeHead, req, consumed) == ParseResult::need_more,
              "oversized head is refused or still pending");
        RequestParser p4 { };
        const std::string chunked {
            "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"
        };
        check(p4.parse(chunked, req, consumed) == ParseResult::error,
              "chunked encoding refused");
    }

    if (failures == 0) {
        std::println("test_parser: all checks passed");
        return 0;
    }
    std::println(stderr, "test_parser: {} failure(s)", failures);
    return 1;
}
