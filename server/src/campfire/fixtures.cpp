module campfire.fixtures;

import std;

namespace campfire {

namespace {

constexpr std::string_view kRoomId      { "aaaaaaaaaaaaaaaa" };
constexpr int           kRoomMessages { 40 };
constexpr int           kMessages     { 30 };
constexpr int           kSearchHits   { 24 };
constexpr int           kSidebarRooms { 40 };
constexpr int           kPresence     { 40 };

std::string hex64_(std::uint64_t value) {
    static constexpr std::string_view kDigits { "0123456789abcdef" };
    std::string out { };
    out.resize(16);
    for (int i { 15 }; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kDigits[value & 0xF];
        value >>= 4;
    }
    return out;
}

// One chat message, close to the shape Campfire renders (bubble, author,
// timestamp, rich-text body with multiple paragraphs, reactions trail).
// Roughly 1.5 KB of markup each.
void append_message_(std::string& out, int index) {
    std::format_to(std::back_inserter(out), R"(
<div class="chat__message" data-message-id="m{0:08}" data-author-id="u{1:04}"
     data-room-id="aaaaaaaaaaaaaaaa" data-created-at="2026-10-06T09:{2:02}:00Z"
     data-broadcast="false" data-highlightable="true">
  <img class="avatar" src="/avatars/u{1:04}.png" width="36" height="36" alt=""
       loading="lazy" decoding="async">
  <div class="chat__message-body">
    <header><b class="author">user{1:04}</b>
      <time datetime="2026-10-06T09:{2:02}:00Z">9:{2:02} AM</time>
      <span class="connection-badge" title="posted via client {7}">via {7}</span></header>
    <div class="rich-text">
      <p>Deploy report <b>#{0}</b>: build <code>once-campfire-mcpp-0.1.{0}</code>
        passed on all runners; latency p99 <var>{3}µs</var>, error rate 0.00%.
        Details at <a href="https://example.test/runs/{0}">run {0}</a> and the
        dashboard panel <em>edge throughput</em>.
        CC <mention data-user="u{1:04}">@user{1:04}</mention>.</p>
      <p>Follow-up: the shard histogram moved by {8}ms after the allocator
        change; the write path stayed flat at {9}k inserts per second and the
        read path held a cache hit ratio of 0.9{0} across all four cores.
        Next step is a rerun with the room fragment cache warmed.</p>
    </div>
    <ul class="trail">
      <li class="reaction" data-emoji="+1">+1 ×{4}</li>
      <li class="reaction" data-emoji="rocket">rocket ×{5}</li>
      <li class="reaction" data-emoji="eyes">eyes ×{10}</li>
      <li class="boost">boosted by user{6:04}</li>
      <li class="reply-link"><a href="/aaaaaaaaaaaaaaaa/messages#m{0:08}">reply</a></li>
    </ul>
  </div>
</div>)",
                   index, index % 64, index % 60, 420 + index, index % 7 + 1,
                   index % 3 + 1, (index + 11) % 64,
                   index % 2 == 0 ? "web" : "desktop", index % 9, 20 + index % 40,
                   index % 5 + 1);
}

void append_sidebar_rooms_(std::string& out) {
    for (int i { 0 }; i < kSidebarRooms; ++i) {
        std::format_to(std::back_inserter(out), R"(
  <li class="room{0}" data-room-id="r{1:016x}">
    <a href="/r{1:016x}" class="room-link room-link--{2}">
      <span class="room-name">room-{3:02}</span>
      <span class="room-topic">bench topics and throughput talk {3}</span>
      <span class="room-unread{4}">{5}</span></a></li>)",
                       i, i, i % 3, i, i % 2 == 0 ? " unread-dot" : "", (i * 3) % 9);
    }
}

void append_presence_(std::string& out) {
    for (int i { 0 }; i < kPresence; ++i) {
        std::format_to(std::back_inserter(out),
                       "\n  <li class=\"presence p{0:02}\" data-user-id=\"u{1:04}\">"
                       "<img src=\"/avatars/u{1:04}.png\" width=\"24\" height=\"24\" "
                       "alt=\"\" loading=\"lazy\">user{1:04}"
                       "<span class=\"presence-status\">in room-{2:02}</span></li>",
                       i, i, i % kSidebarRooms);
    }
}

std::string build_room_page_() {
    std::string out { };
    out.reserve(96 * 1024);
    std::format_to(std::back_inserter(out), R"(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Campfire — room-00</title>
  <link rel="stylesheet" href="/assets/application-0f4e9d2c.css" data-turbo-track="reload">
  <script type="importmap" data-turbo-track="reload">{{"imports":{{"@rails/actioncable":"data:text/javascript,"}}}}</script>
  <script src="/assets/application-9c41bbe2.js" type="module"></script>
</head>
<body data-controller="room" data-room-id="{0}">
  <header class="global-header">
    <a href="/" class="brand">Campfire</a>
    <nav><a href="/notifications" class="notifications">Notifications</a>
      <a href="/settings" class="settings">Settings</a></nav>
  </header>
  <main class="room-layout">
    <section class="room" id="room" data-room-id="{0}">)",
                       kRoomId);
    for (int i { 1 }; i <= kRoomMessages; ++i) {
        append_message_(out, i);
    }
    out.append(R"(
      <form class="composer" action="/aaaaaaaaaaaaaaaa/messages" method="post" data-controller="composer">
        <input type="hidden" name="authenticity_token" value="bench-mode-csrf-disabled">
        <input type="text" name="message[content]" class="composer__input" placeholder="Say something">
        <button type="submit" class="composer__send">Send</button>
      </form>
    </section>
    <aside class="sidebar" id="sidebar">
      <h2>Rooms</h2>
      <ul class="rooms">)");
    append_sidebar_rooms_(out);
    out.append("\n      </ul>\n      <h2>Online</h2>\n      <ul class=\"presences\">");
    append_presence_(out);
    out.append(R"(
      </ul>
      <h2>Pinned</h2>
      <div class="pinned"><p>Pinned: benchmarks run daily at 10:00; results in <b>#bench</b>.</p></div>
    </aside>
  </main>
  <footer class="global-footer"><span>once-campfire-mcpp transport floor fixture</span></footer>
</body>
</html>
)");
    return out;
}

std::string build_messages_page_() {
    std::string out { };
    out.reserve(80 * 1024);
    out.append("<div class=\"messages\" data-room-id=\"").append(kRoomId).append("\">");
    for (int i { 1 }; i <= kMessages; ++i) {
        append_message_(out, i);
    }
    out.append("\n</div>\n");
    return out;
}

std::string build_sidebar_() {
    std::string out { };
    out.reserve(24 * 1024);
    out.append("<aside class=\"sidebar\" id=\"sidebar\">\n  <h2>Rooms</h2>\n  <ul class=\"rooms\">");
    append_sidebar_rooms_(out);
    out.append("\n  </ul>\n  <h2>Online</h2>\n  <ul class=\"presences\">");
    append_presence_(out);
    out.append(R"(
  </ul>
  <h2>Pinned</h2>
  <div class="pinned"><p>Pinned: benchmarks run daily at 10:00; results in <b>#bench</b>.</p></div>
</aside>
)");
    return out;
}

std::string build_search_() {
    std::string out { };
    out.reserve(48 * 1024);
    out.append(R"(<div class="search-results" data-term="bench">
  <header><h2>Results for “bench”</h2></header>)");
    for (int i { 1 }; i <= kSearchHits; ++i) {
        std::format_to(std::back_inserter(out), R"(
  <article class="search-hit" data-message-id="s{0:08}" data-room-id="r{1:016x}">
    <a class="hit-room" href="/r{1:016x}">room-{2:02}</a>
    <time datetime="2026-10-06T10:{5:02}:00Z">10:{5:02} AM</time>
    <blockquote>bench result <b>#{0}</b>: throughput {3}, throughput/sec {4},
      <mark>bench</mark> suite green on every shard, allocator hit ratio 0.9{0},
      p99 latency {6}µs with the fragment cache warm across four io threads.</blockquote>
  </article>)",
                       i, i, i % kSidebarRooms, 24000 + i * 137, 900 + i, i % 60,
                       300 + i * 11);
    }
    out.append("\n</div>\n");
    return out;
}

}  // namespace

std::string etag_of(std::string_view body) {
    std::uint64_t hash { 0xcbf2'9ce4'8422'2325ULL };
    for (const unsigned char c : body) {
        hash ^= c;
        hash *= 0x0000'0100'0000'01B3ULL;
    }
    return std::format("\"fnv1a64-{}\"", hex64_(hash));
}

namespace {

Fixtures build_all_() {
    Fixtures fixtures { };
    fixtures.roomPage.body     = build_room_page_();
    fixtures.roomPage.etag     = etag_of(fixtures.roomPage.body);
    fixtures.messagesPage.body = build_messages_page_();
    fixtures.messagesPage.etag = etag_of(fixtures.messagesPage.body);
    fixtures.sidebar.body      = build_sidebar_();
    fixtures.sidebar.etag      = etag_of(fixtures.sidebar.body);
    fixtures.search.body       = build_search_();
    fixtures.search.etag       = etag_of(fixtures.search.body);
    return fixtures;
}

}  // namespace

const Fixtures& Fixtures::instance() {
    static const Fixtures fixtures { build_all_() };
    return fixtures;
}

}
