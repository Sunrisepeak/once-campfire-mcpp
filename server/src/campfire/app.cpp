module;
#include <cstdio>
module campfire.app;

import std;
import campfire.config;
import campfire.fixtures;
import campfire.http.router;
import campfire.http.types;
import campfire.http.server;
import campfire.log;
import campfire.version;

namespace campfire::app {

using campfire::http::Method;
using campfire::http::Request;
using campfire::http::Response;
using campfire::http::Router;
using campfire::http::Status;

namespace {

// Campfire-style opaque room id: 1..32 chars of URL-safe base62.
constexpr bool valid_room_id_(std::string_view id) {
    if (id.empty() || id.size() > 32) return false;
    for (const char c : id) {
        const bool ok { (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                     || (c >= '0' && c <= '9') || c == '_' || c == '-' };
        if (!ok) return false;
    }
    return true;
}

Response serve_fixture_(const Request& req, const Fixture& fixture,
                        std::string_view contentType) {
    if (!req.roomId.empty() && !valid_room_id_(req.roomId)) {
        return Response::text(Status::not_found, "not found\n");
    }
    const std::string_view ifNoneMatch { req.header("If-None-Match") };
    if (!ifNoneMatch.empty() && ifNoneMatch == fixture.etag) {
        return Response::not_modified(fixture.etag);
    }
    return Response::make(Status::ok, contentType, fixture.body, fixture.etag);
}

Response room_page_(Request& req) {
    return serve_fixture_(req, Fixtures::instance().roomPage,
                          "text/html; charset=utf-8");
}

Response messages_page_(Request& req) {
    return serve_fixture_(req, Fixtures::instance().messagesPage,
                          "text/html; charset=utf-8");
}

Response sidebar_(Request& req) {
    return serve_fixture_(req, Fixtures::instance().sidebar,
                          "text/html; charset=utf-8");
}

Response search_(Request& req) {
    return serve_fixture_(req, Fixtures::instance().search,
                          "text/html; charset=utf-8");
}

Response post_message_(Request& req) {
    if (!valid_room_id_(req.roomId)) {
        return Response::text(Status::not_found, "not found\n");
    }
    // M4 replaces this stub with insert + broadcast; the route and its
    // redirect contract are already the real ones.
    return Response::redirect(std::format("/{}", req.roomId));
}

Response health_(Request&) {
    return Response::text(Status::ok, std::format("{} {}\n", kName, kVersion));
}

}  // namespace

Router build_router() {
    Router router { };
    router.add(Method::get,  "/healthz",             health_);
    router.add(Method::get,  "/:roomId",             room_page_);
    router.add(Method::get,  "/:roomId/messages",    messages_page_);
    router.add(Method::post, "/:roomId/messages",    post_message_);
    router.add(Method::get,  "/:roomId/sidebar",     sidebar_);
    router.add(Method::get,  "/:roomId/search",      search_);
    return router;
}

int main(int argc, char** argv) {
    if (argc > 1 && (std::string_view { argv[1] } == "--help"
                  || std::string_view { argv[1] } == "-h")) {
        std::print("{}", serve_usage());
        return 0;
    }
    auto config { parse_serve_args(argc, argv) };
    if (!config) {
        std::println(stderr, "error: {}", config.error());
        std::print(stderr, "{}", serve_usage());
        return 2;
    }
    const Router router { build_router() };
    return serve(*config, router);
}

}
