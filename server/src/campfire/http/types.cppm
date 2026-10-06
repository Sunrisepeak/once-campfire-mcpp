// campfire.http.types — wire types shared by the parser, the router and the
// application. Requests are zero-copy: every string_view points into the
// connection's read buffer and is valid until the response for that request
// has been handed to the transport.
module;
#include <cctype>
export module campfire.http.types;

import std;

export namespace campfire::http {

enum class Method : std::uint8_t {
    get, post, put, patch, del, head, options, unknown
};

constexpr Method method_from_string(std::string_view text) {
    if (text == "GET")     return Method::get;
    if (text == "POST")    return Method::post;
    if (text == "PUT")     return Method::put;
    if (text == "PATCH")   return Method::patch;
    if (text == "DELETE")  return Method::del;
    if (text == "HEAD")    return Method::head;
    if (text == "OPTIONS") return Method::options;
    return Method::unknown;
}

constexpr std::string_view method_to_string(Method method) {
    switch (method) {
        case Method::get:     return "GET";
        case Method::post:    return "POST";
        case Method::put:     return "PUT";
        case Method::patch:   return "PATCH";
        case Method::del:     return "DELETE";
        case Method::head:    return "HEAD";
        case Method::options: return "OPTIONS";
        case Method::unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

enum class Status : std::uint16_t {
    ok                   = 200,
    created              = 201,
    no_content           = 204,
    moved_temporarily    = 302,
    not_modified         = 304,
    bad_request          = 400,
    not_found            = 404,
    method_not_allowed   = 405,
    payload_too_large    = 413,
    internal_server_error = 500,
};

constexpr std::string_view status_reason(Status status) {
    switch (status) {
        case Status::ok:                    return "OK";
        case Status::created:               return "Created";
        case Status::no_content:            return "No Content";
        case Status::moved_temporarily:     return "Moved Temporarily";
        case Status::not_modified:          return "Not Modified";
        case Status::bad_request:           return "Bad Request";
        case Status::not_found:             return "Not Found";
        case Status::method_not_allowed:    return "Method Not Allowed";
        case Status::payload_too_large:     return "Payload Too Large";
        case Status::internal_server_error: return "Internal Server Error";
    }
    return "Unknown";
}

struct HeaderIn {
    std::string_view name;
    std::string_view value;
};

struct Request {
    Method         method     { Method::unknown };
    std::string_view target   { };   // raw request-target
    std::string_view path     { };   // target up to '?'
    std::string_view query    { };   // after '?', possibly empty
    std::string_view body     { };
    std::string_view roomId   { };   // router's ':'-segment capture, view into path
    bool             keepAlive { true };
    std::uint8_t     minorVersion { 1 };
    std::vector<HeaderIn> headers { };

    // RFC 9110: field names are case-insensitive.
    std::string_view header(std::string_view name) const {
        const auto equalsCi = [](std::string_view a, std::string_view b) {
            return a.size() == b.size()
                && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                       return std::tolower(static_cast<unsigned char>(x))
                           == std::tolower(static_cast<unsigned char>(y));
                   });
        };
        for (const auto& h : headers) {
            if (equalsCi(h.name, name)) {
                return h.value;
            }
        }
        return { };
    }
};

struct HeaderOut {
    std::string name;
    std::string value;
};

struct Response {
    Status                 status  { Status::ok };
    std::string            body    { };
    std::vector<HeaderOut> headers { };

    void add_header(std::string name, std::string value) {
        headers.emplace_back(std::move(name), std::move(value));
    }

    static Response make(Status status, std::string_view contentType,
                         std::string body, std::string_view etag = { }) {
        Response response { };
        response.status = status;
        response.body   = std::move(body);
        response.add_header("Content-Type", std::string { contentType });
        if (!etag.empty()) {
            response.add_header("ETag", std::string { etag });
        }
        return response;
    }

    static Response not_modified(std::string_view etag) {
        Response response { };
        response.status = Status::not_modified;
        response.add_header("ETag", std::string { etag });
        return response;
    }

    static Response redirect(std::string_view location) {
        Response response { };
        response.status = Status::moved_temporarily;
        response.add_header("Location", std::string { location });
        return response;
    }

    static Response text(Status status, std::string_view body) {
        return make(status, "text/plain; charset=utf-8", std::string { body });
    }
};

}
