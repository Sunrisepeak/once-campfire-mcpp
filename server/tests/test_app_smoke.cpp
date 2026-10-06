// Application smoke tests: every benchmarked route answers through the real
// router, fixtures carry ETags, conditional requests collapse to 304.
#include <cstdio>
import std;

import campfire.app;
import campfire.fixtures;
import campfire.http.types;

using namespace campfire::http;

namespace {

int failures { 0 };

void check(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::println(stderr, "FAIL: {}", what);
    }
}

// A request plus the storage its zero-copy views point at. Requests never own
// their strings — the fixture keeps the target alive across the dispatch.
struct RequestFixture {
    std::string target;
    Request     req;
};

RequestFixture make_request_(Method method, std::string target) {
    RequestFixture fixture { std::move(target), Request { } };
    fixture.req.method = method;
    fixture.req.target = fixture.target;
    const auto q { fixture.target.find('?') };
    fixture.req.path  = q == std::string::npos
        ? std::string_view { fixture.target }
        : std::string_view { fixture.target }.substr(0, q);
    fixture.req.query = q == std::string::npos
        ? std::string_view { }
        : std::string_view { fixture.target }.substr(q + 1);
    return fixture;
}

bool has_header_(const Response& response, std::string_view name, std::string_view value) {
    for (const auto& h : response.headers) {
        if (h.name == name && h.value == value) return true;
    }
    return false;
}

}  // namespace

int main() {
    const auto router { campfire::app::build_router() };
    auto dispatch = [&](Request& req) { return router.dispatch(req); };

    // -- every benchmarked route answers 200 with html + etag ------------------
    {
        auto roomFx { make_request_(Method::get, "/aaaaaaaaaaaaaaaa") };
        const auto room { dispatch(roomFx.req) };
        check(room.status == Status::ok, "room page is 200");
        check(room.body.size() > 50 * 1024, "room page fixture is room-page sized");
        check(has_header_(room, "Content-Type", "text/html; charset=utf-8"),
              "room page is html");
        check(has_header_(room, "ETag", campfire::Fixtures::instance().roomPage.etag),
              "room page carries its etag");

        auto messagesFx { make_request_(Method::get, "/aaaaaaaaaaaaaaaa/messages") };
        const auto messages { dispatch(messagesFx.req) };
        check(messages.status == Status::ok && messages.body.size() > 35 * 1024,
              "messages page fixture is messages-sized");

        auto sidebarFx { make_request_(Method::get, "/aaaaaaaaaaaaaaaa/sidebar") };
        const auto sidebar { dispatch(sidebarFx.req) };
        check(sidebar.status == Status::ok && sidebar.body.size() > 5 * 1024,
              "sidebar fixture is sidebar-sized");

        auto searchFx { make_request_(Method::get,
                                      "/aaaaaaaaaaaaaaaa/search?term=bench") };
        const auto search { dispatch(searchFx.req) };
        check(search.status == Status::ok && search.body.size() > 10 * 1024,
              "search fixture is search-sized");
        check(searchFx.req.query == "term=bench", "search query propagates");
    }

    // -- conditional request flow ------------------------------------------------
    {
        const auto& etag { campfire::Fixtures::instance().roomPage.etag };
        auto conditionalFx { make_request_(Method::get, "/aaaaaaaaaaaaaaaa") };
        conditionalFx.req.headers.push_back(HeaderIn { "If-None-Match", etag });
        const auto notModified { router.dispatch(conditionalFx.req) };
        check(notModified.status == Status::not_modified, "matching etag yields 304");
        check(notModified.body.empty(), "304 carries no body");
        check(has_header_(notModified, "ETag", etag), "304 echoes the etag");
    }

    // -- post redirects; invalid ids 404; health answers --------------------------
    {
        auto postFx { make_request_(Method::post, "/aaaaaaaaaaaaaaaa/messages") };
        postFx.req.body = "message%5Bcontent%5D=hello";
        const auto response { dispatch(postFx.req) };
        check(response.status == Status::moved_temporarily, "post message redirects");
        check(has_header_(response, "Location", "/aaaaaaaaaaaaaaaa"),
              "redirect points at the room");

        auto badIdFx { make_request_(Method::get, "/bad id!") };
        check(dispatch(badIdFx.req).status == Status::not_found,
              "invalid room id is 404");
        auto badIdPostFx { make_request_(Method::post, "/bad id!/messages") };
        check(dispatch(badIdPostFx.req).status == Status::not_found,
              "invalid room id on post is 404");

        auto healthFx { make_request_(Method::get, "/healthz") };
        const auto health { dispatch(healthFx.req) };
        check(health.status == Status::ok, "healthz is 200");
    }

    if (failures == 0) {
        std::println("test_app_smoke: all checks passed");
        return 0;
    }
    std::println(stderr, "test_app_smoke: {} failure(s)", failures);
    return 1;
}
