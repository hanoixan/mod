#include "app/cli_options.hpp"

#include <charconv>
#include <format>

#include "app/settings.hpp"
#include "ui/theme.hpp"

namespace mod {
namespace {

bool scalar(const SettingSpec& s) { return s.type != SettingType::keymap && s.type != SettingType::colors; }

// "tab_width" -> "tab-width"
std::string flag_name(std::string_view key) {
    std::string out(key);
    for (char& c : out) c = c == '_' ? '-' : c;
    return out;
}

const SettingSpec* setting_for_flag(std::string_view name) {
    for (const SettingSpec& s : setting_specs())
        if (scalar(s) && flag_name(s.key) == name) return &s;
    return nullptr;
}

// "a", "a or b", "a, b or c"
std::string either(std::span<const std::string_view> words) {
    std::string out;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i > 0) out += i + 1 == words.size() ? " or " : ", ";
        out += words[i];
    }
    return out;
}

// The choice names as flags take them: dashes for spaces.
std::vector<std::string> choice_names(const SettingSpec& s) {
    std::vector<std::string> out;
    for (std::string_view c : s.choices) {
        std::string name(c);
        for (char& ch : name) ch = ch == ' ' ? '-' : ch;
        out.push_back(std::move(name));
    }
    return out;
}

std::expected<std::int64_t, std::string> setting_value(const SettingSpec& s, std::string_view flag, std::optional<std::string_view> value) {
    switch (s.type) {
        case SettingType::boolean: {
            if (!value) return 1;
            for (std::string_view on : {"on", "true", "yes", "1"})
                if (*value == on) return 1;
            for (std::string_view off : {"off", "false", "no", "0"})
                if (*value == off) return 0;
            return std::unexpected(std::format("--{} wants on or off", flag));
        }
        case SettingType::integer: {
            std::int64_t n = 0;
            const std::string_view text = value.value_or(std::string_view{});
            const bool ok = !text.empty() && std::from_chars(text.data(), text.data() + text.size(), n).ptr == text.data() + text.size();
            if (!ok || n < s.min || n > s.max) return std::unexpected(std::format("--{} wants a number from {} to {}", flag, s.min, s.max));
            return n;
        }
        case SettingType::choice: {
            const std::vector<std::string> names = choice_names(s);
            if (value) {
                for (std::size_t i = 0; i < names.size(); ++i)
                    if (*value == names[i] || *value == s.choices[i]) return static_cast<std::int64_t>(i);
            }
            const std::vector<std::string_view> views(names.begin(), names.end());
            return std::unexpected(std::format("--{} wants {}", flag, either(views)));
        }
        default: break;
    }
    return std::unexpected(std::format("unknown option --{}", flag));
}

std::expected<std::pair<std::string, std::string>, std::string> color_value(std::string_view text) {
    const std::size_t eq = text.find('=');
    if (eq == std::string_view::npos || eq == 0) return std::unexpected(std::string("--color wants name=spec"));
    std::string name(text.substr(0, eq));
    std::string spec(text.substr(eq + 1));
    ColorTheme scratch;  // validates the name and the spec as the theme will
    if (auto s = scratch.set_session(name, spec); !s) return std::unexpected(std::format("--color {}: {}", name, s.error().message));
    return std::pair{std::move(name), std::move(spec)};
}

}  // namespace

std::expected<CliOptions, std::string> parse_cli(std::span<const std::string_view> args) {
    CliOptions o;
    bool options_done = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (options_done || arg == "-" || !arg.starts_with('-')) {
            o.paths.emplace_back(arg);
            continue;
        }
        if (arg == "--") {
            options_done = true;
        } else if (arg == "-h" || arg == "--help") {
            o.action = CliOptions::Action::help;
            return o;
        } else if (arg == "--version") {
            o.action = CliOptions::Action::version;
            return o;
        } else if (arg == "-ro" || arg == "--read-only") {
            o.read_only = true;
        } else if (arg == "--persist-history") {
            o.persist_history = true;
        } else if (arg == "--color" || arg.starts_with("--color=")) {
            std::string_view text;
            if (arg == "--color") {
                if (i + 1 >= args.size()) return std::unexpected(std::string("--color wants name=spec"));
                text = args[++i];
            } else {
                text = arg.substr(8);
            }
            auto c = color_value(text);
            if (!c) return std::unexpected(c.error());
            o.colors.push_back(std::move(*c));
        } else if (arg.starts_with("--")) {
            std::string_view name = arg.substr(2);
            std::optional<std::string_view> value;
            if (const std::size_t eq = name.find('='); eq != std::string_view::npos) {
                value = name.substr(eq + 1);
                name = name.substr(0, eq);
            }
            const SettingSpec* spec = setting_for_flag(name);
            bool negated = false;
            if (!spec && name.starts_with("no-") && !value) {
                spec = setting_for_flag(name.substr(3));
                negated = spec && spec->type == SettingType::boolean;
                if (!negated) spec = nullptr;
            }
            if (!spec) return std::unexpected(std::format("unknown option {}", arg));
            if (negated) {
                o.settings.emplace_back(std::string(spec->key), 0);
                continue;
            }
            auto v = setting_value(*spec, name, value);
            if (!v) return std::unexpected(v.error());
            o.settings.emplace_back(std::string(spec->key), *v);
        } else {
            return std::unexpected(std::format("unknown option {}", arg));
        }
    }
    return o;
}

std::string cli_usage() {
    std::string out =
        "usage: mod [OPTION...] [PATH...]\n"
        "  PATH                  files to edit, each as a document; without any, an untitled buffer\n"
        "  -ro, --read-only      open the files in read-only mode\n"
        "  --persist-history     keep the files' undo history in their .history sidecars\n"
        "  --color NAME=SPEC     a color for this session, such as --color 'keyword=bold red'\n"
        "  -h, --help            print this and exit\n"
        "  --version             print the version and exit\n"
        "  --                    the rest are paths, even if they start with -\n"
        "\n"
        "Settings for this session only (never saved), defaults in brackets:\n";
    for (const SettingSpec& s : setting_specs()) {
        if (!scalar(s)) continue;
        std::string flag = "--" + flag_name(s.key);
        std::string values;
        std::string def;
        switch (s.type) {
            case SettingType::boolean:
                flag = "--[no-]" + flag_name(s.key);
                def = s.def ? "on" : "off";
                break;
            case SettingType::integer:
                values = std::format("={}..{}", s.min, s.max);
                def = std::to_string(s.def);
                break;
            case SettingType::choice: {
                const std::vector<std::string> names = choice_names(s);
                values = "=";
                for (std::size_t i = 0; i < names.size(); ++i) values += (i ? "|" : "") + names[i];
                def = names[static_cast<std::size_t>(s.def)];
                break;
            }
            default: break;
        }
        const std::string left = flag + values;
        if (left.size() > 38) {
            out += std::format("  {}\n  {:<38} {} [{}]\n", left, "", s.label, def);  // too wide: on its own line
        } else {
            out += std::format("  {:<38} {} [{}]\n", left, s.label, def);
        }
    }
    return out;
}

}  // namespace mod
