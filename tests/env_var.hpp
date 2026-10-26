#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace mod::test {

// Sets an environment variable (or, given nullopt, unsets it) for one scope, then puts
// back what was there.
class EnvVar {
public:
    EnvVar(const char* name, const std::optional<std::string>& value) : name_(name) {
        if (const char* old = std::getenv(name)) old_ = old;
        set(value);
    }
    ~EnvVar() { set(old_); }
    EnvVar(const EnvVar&) = delete;
    EnvVar& operator=(const EnvVar&) = delete;

private:
    void set(const std::optional<std::string>& value) const {
        if (value) {
            ::setenv(name_, value->c_str(), 1);
        } else {
            ::unsetenv(name_);
        }
    }

    const char* name_;
    std::optional<std::string> old_;
};

}  // namespace mod::test
