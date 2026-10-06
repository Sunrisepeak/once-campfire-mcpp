// campfire.version — single place that names the product and its version.
export module campfire.version;

import std;

export namespace campfire {

inline constexpr std::string_view kName    { "once-campfire-mcpp" };
inline constexpr std::string_view kVersion { "0.1.0" };
inline constexpr std::string_view kServerToken { "once-campfire-mcpp/0.1.0" };

}
