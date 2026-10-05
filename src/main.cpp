#include <csignal>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "app/app.hpp"
#include "app/cli_options.hpp"
#include "platform/terminal.hpp"
#include "util/log.hpp"

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    auto options = mod::parse_cli(args);
    if (!options) {
        (void)std::fprintf(stderr, "mod: %s\n%s", options.error().c_str(), mod::cli_usage().c_str());  // nowhere to report a failed report
        return 2;
    }
    if (options->action == mod::CliOptions::Action::help) {
        (void)std::fputs(mod::cli_usage().c_str(), stdout);
        return 0;
    }
    if (options->action == mod::CliOptions::Action::version) {
        (void)std::puts("mod " MOD_VERSION);
        return 0;
    }

    mod::init_logging();
    (void)std::signal(SIGPIPE, SIG_IGN);  // fails only for an invalid signal number

    auto terminal = mod::make_terminal();
    if (!terminal) {
        (void)std::fputs("mod: stdin/stdout must be a terminal\n", stderr);
        return 2;
    }
    try {
        mod::App app(*options, std::move(*terminal));
        return app.run();
    } catch (const std::exception& e) {
        // The terminal was restored while unwinding (by App's destructor, or the terminal's own).
        (void)std::fprintf(stderr, "mod: %s\n", e.what());
        return 1;
    } catch (...) {
        (void)std::fputs("mod: unknown error\n", stderr);
        return 1;
    }
}
