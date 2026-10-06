// campfire.http.server — transport layer.
//
// M1 shape: one acceptor feeding a shared io_context that N worker threads
// run. Each connection chains one async operation at a time (read -> handle
// -> write -> read), so no per-connection strand is needed. The M3 milestone
// replaces this with thread-per-core SO_REUSEPORT shards behind the same
// serve() signature.
export module campfire.http.server;

import std;
import campfire.config;
import campfire.http.router;

export namespace campfire::http {

// Blocks until shutdown; returns a process exit code.
int serve(const Config& config, const Router& router);

}
