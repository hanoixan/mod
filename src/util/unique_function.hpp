#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace mod {

template <class Signature>
class UniqueFunction;

// A move-only callable, like std::move_only_function, which not every standard library
// has (libc++ does not): it can hold a lambda that owns a unique_ptr. Empty by default.
template <class R, class... Args>
class UniqueFunction<R(Args...)> {
public:
    UniqueFunction() noexcept = default;
    UniqueFunction(std::nullptr_t) noexcept {}  // NOLINT(google-explicit-constructor): like std::function

    template <class F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, UniqueFunction> && std::is_invocable_r_v<R, F&, Args...>)
    UniqueFunction(F&& f)  // NOLINT(google-explicit-constructor): like std::function
        : impl_(std::make_unique<Holder<std::decay_t<F>>>(std::forward<F>(f))) {}

    UniqueFunction(UniqueFunction&&) noexcept = default;
    UniqueFunction& operator=(UniqueFunction&&) noexcept = default;
    UniqueFunction(const UniqueFunction&) = delete;
    UniqueFunction& operator=(const UniqueFunction&) = delete;
    ~UniqueFunction() = default;

    explicit operator bool() const noexcept { return impl_ != nullptr; }
    R operator()(Args... args) { return impl_->call(std::forward<Args>(args)...); }

private:
    struct Base {
        Base() = default;
        Base(const Base&) = delete;
        Base& operator=(const Base&) = delete;
        Base(Base&&) = delete;
        Base& operator=(Base&&) = delete;
        virtual ~Base() = default;
        virtual R call(Args... args) = 0;
    };
    template <class F>
    struct Holder final : Base {
        explicit Holder(F&& f) : fn(std::move(f)) {}
        explicit Holder(const F& f) : fn(f) {}
        R call(Args... args) override { return fn(std::forward<Args>(args)...); }
        F fn;
    };
    std::unique_ptr<Base> impl_;
};

}  // namespace mod
