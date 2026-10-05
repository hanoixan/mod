// Prints settings.json with every setting at its default, for `cmake --install`.
#include <cstdio>

#include "app/settings.hpp"

int main() {
    const std::string json = mod::default_settings_json();
    return std::fwrite(json.data(), 1, json.size(), stdout) == json.size() ? 0 : 1;
}
