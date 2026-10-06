// Router unit tests: literal and parameter segments, method discrimination,
// 404 vs 405.
#include <cstdio>
import std;

import campfire.http.types;
import campfire.http.router;

using namespace campfire::http;

namespace {

int failures { 0 };

void check(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::println(stderr, "FAIL: {}", what);
    }
}

Request make_request_(Method method, std::string path) {
    Request req { };
    req.method = method;
    req.path   = path;
    return req;
}

}  // namespace

int main() {
    Router router { };
    bool healthSeen { false };
    bool roomSeen { false };
    bool postSeen { false };

    router.add(Method::get, "/healthz", [&](Request&) {
        healthSeen = true;
        return Response::text(Status::ok, "ok");
    });
    router.add(Method::get, "/:roomId", [](Request& req) {
        return Response::text(Status::ok, std::string { req.roomId });
    });
    router.add(Method::get, "/:roomId/messages", [](Request& req) {
        return Response::text(Status::ok, std::format("messages of {}", req.roomId));
    });
    router.add(Method::post, "/:roomId/messages", [&](Request& req) {
        postSeen = true;
        return Response::redirect(std::format("/{}", req.roomId));
    });

    auto dispatch = [&](Request req) { return router.dispatch(req); };

    {
        auto response { dispatch(make_request_(Method::get, "/healthz")) };
        check(response.status == Status::ok && healthSeen, "literal route reached");
    }
    {
        auto response { dispatch(make_request_(Method::get, "/abcdefgh")) };
        check(response.status == Status::ok, "room page reached");
        check(response.body == "abcdefgh", "roomId captured");
    }
    {
        auto response { dispatch(make_request_(Method::get, "/abcdefgh/messages")) };
        check(response.status == Status::ok, "messages route reached");
        check(response.body == "messages of abcdefgh", "roomId captured on nested route");
    }
    {
        auto response { dispatch(make_request_(Method::post, "/abcdefgh/messages")) };
        check(postSeen, "post route reached");
        check(response.status == Status::moved_temporarily, "post redirects");
        check(response.headers[0].value == "/abcdefgh", "redirect location");
    }
    {
        check(dispatch(make_request_(Method::put, "/abcdefgh/messages")).status
                  == Status::method_not_allowed,
              "shape match with wrong method is 405");
        check(dispatch(make_request_(Method::get, "/no/such/route")).status
                  == Status::not_found,
              "unknown shape is 404");
        check(dispatch(make_request_(Method::get, "/abcdefgh/messages/extra")).status
                  == Status::not_found,
              "extra segment is 404");
        check(dispatch(make_request_(Method::get, "/")).status == Status::not_found,
              "empty root is 404 (no / route registered)");
    }
    {
        check(!roomSeen, "room handler used only for its own dispatches");
    }

    if (failures == 0) {
        std::println("test_router: all checks passed");
        return 0;
    }
    std::println(stderr, "test_router: {} failure(s)", failures);
    return 1;
}
