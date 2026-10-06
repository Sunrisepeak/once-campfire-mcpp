// campfire.app — route table and process entry wiring.
export module campfire.app;

import std;
import campfire.config;
import campfire.http.router;
import campfire.http.types;

export namespace campfire::app {

// The five benchmarked routes plus an ops endpoint. M1 serves static
// fixtures; M2+ replaces handler bodies with the real pipeline while the
// route shapes stay.
campfire::http::Router build_router();

// Process entry: flag parsing and the default "serve" path.
int main(int argc, char** argv);

}
