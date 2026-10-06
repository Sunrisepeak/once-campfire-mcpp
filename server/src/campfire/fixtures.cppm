// campfire.fixtures — M1 response bodies.
//
// Shape and sizes follow the real routes (room page with chat + sidebar,
// messages partial, sidebar partial, search partial). Content is static
// until M2/M3 wire the database and the fragment cache; the ETag machinery
// (FNV-1a 64 over the body, quoted hex) is the real one the cache layer
// will keep.
export module campfire.fixtures;

import std;

export namespace campfire {

struct Fixture {
    std::string body { };
    std::string etag { };
};

struct Fixtures {
    Fixture roomPage     { };
    Fixture messagesPage { };
    Fixture sidebar      { };
    Fixture search       { };

    // Built once, on first use, deterministically.
    static const Fixtures& instance();
};

// Quoted strong ETag: "fnv1a64-<hex>".
std::string etag_of(std::string_view body);

}
