// campfire.config — process configuration and command-line parsing.
export module campfire.config;

import std;

export namespace campfire {

struct Config {
    std::string     host    { "0.0.0.0" };
    std::uint16_t   port    { 3000 };
    int             threads { 0 };   // 0 = one thread per hardware core
};

std::expected<Config, std::string> parse_serve_args(int argc, char** argv);

std::string_view serve_usage();

}
