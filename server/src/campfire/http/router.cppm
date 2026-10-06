// campfire.http.router — pattern matcher for the route table.
//
// Patterns are absolute paths whose segments are either literal text or a
// ':'-prefixed parameter (the first one captures into Request::roomId).
// Registration order decides precedence among same-shape patterns.
export module campfire.http.router;

import std;
import campfire.http.types;

export namespace campfire::http {

using Handler = std::function<Response(Request&)>;

class Router {
public:
    void add(Method method, std::string_view pattern, Handler handler) {
        routes_.push_back(Route { method, split_(pattern), std::move(handler) });
    }

    Response dispatch(Request& req) const {
        bool shapeMatched { false };
        for (const auto& route : routes_) {
            if (auto capture { match_(route.segments, req.path) }) {
                shapeMatched = true;
                if (route.method == req.method) {
                    req.roomId = *capture;
                    return route.handler(req);
                }
            }
        }
        if (shapeMatched) {
            Response response { Response::text(Status::method_not_allowed, "method not allowed\n") };
            response.add_header("Allow", "GET, POST");
            return response;
        }
        return Response::text(Status::not_found, "not found\n");
    }

private:
    struct Seg {
        bool        isParam { false };
        std::string text    { };
    };

    struct Route {
        Method              method   { Method::get };
        std::vector<Seg>    segments { };
        Handler             handler  { };
    };

    static std::vector<Seg> split_(std::string_view pattern) {
        std::vector<Seg> segments { };
        if (pattern == "/") {
            segments.push_back(Seg { false, "" });
            return segments;
        }
        std::size_t start { 0 };
        if (!pattern.empty() && pattern.front() == '/') {
            start = 1;
        }
        while (start <= pattern.size()) {
            const auto slash { pattern.find('/', start) };
            const bool last { slash == std::string_view::npos };
            const std::string_view seg {
                pattern.substr(start, last ? pattern.size() - start : slash - start)
            };
            if (!seg.empty()) {
                if (seg.front() == ':') {
                    segments.push_back(Seg { true, std::string { seg.substr(1) } });
                } else {
                    segments.push_back(Seg { false, std::string { seg } });
                }
            }
            if (last) break;
            start = slash + 1;
        }
        return segments;
    }

    // Returns the captured parameter (possibly empty) when the path shape
    // matches; the capture is a view into `path`.
    static std::optional<std::string_view> match_(const std::vector<Seg>& segments,
                                                  std::string_view path) {
        std::size_t start { 0 };
        if (!path.empty() && path.front() == '/') {
            start = 1;
        }
        std::string_view capture { };
        for (std::size_t i { 0 }; i < segments.size(); ++i) {
            const auto slash { path.find('/', start) };
            const bool last { slash == std::string_view::npos };
            const std::string_view seg {
                path.substr(start, last ? path.size() - start : slash - start)
            };
            const bool moreSegmentsAfter { i + 1 < segments.size() };
            if (!last && !moreSegmentsAfter) {
                return std::nullopt;   // path has extra segments
            }
            if (segments[i].isParam) {
                if (seg.empty()) {
                    return std::nullopt;   // "/:param" does not match "/"
                }
                if (i == 0) capture = seg;
            } else if (segments[i].text != seg) {
                return std::nullopt;
            }
            if (last) {
                if (moreSegmentsAfter) {
                    return std::nullopt;   // pattern longer than path
                }
                return capture;
            }
            start = slash + 1;
        }
        return std::nullopt;
    }

    std::vector<Route> routes_ { };
};

}
