// campfire.http.parser — incremental HTTP/1.1 request parser.
//
// The parser never copies: on `ok`, every view in the Request points into the
// first `consumed` bytes of the input. One request at a time; the caller keeps
// unconsumed bytes for the next request on the connection (keep-alive
// pipelining falls out of that slicing).
export module campfire.http.parser;

import std;
import campfire.http.types;

export namespace campfire::http {

enum class ParseResult : std::uint8_t { need_more, ok, error };

struct ParseLimits {
    std::size_t maxHeadBytes { 32 * 1024 };
    std::size_t maxBodyBytes { 1 * 1024 * 1024 };
};

class RequestParser {
public:
    explicit RequestParser(const ParseLimits& limits = { }) : limits_ { limits } { }

    void reset();

    // On `ok`, `consumed` is the full request size in bytes and the views in
    // `req` are valid until the caller reuses the input storage. On
    // `need_more` nothing has been consumed. On `error` the connection must
    // be closed; `error()` names the reason.
    ParseResult parse(std::string_view data, Request& req, std::size_t& consumed);

    std::string_view error() const { return errorMessage_; }

private:
    enum class State : std::uint8_t { head, body };

    ParseLimits  limits_       { };
    State        state_        { State::head };
    std::size_t  headEnd_      { 0 };   // includes the trailing CRLFCRLF
    std::size_t  contentLength_ { 0 };
    std::string  errorMessage_ { };
};

}
