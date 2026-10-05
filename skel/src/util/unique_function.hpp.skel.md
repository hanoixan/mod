---
role: product
untested: exercised through every EventQueue test, which posts move-only lambdas
stamp: source c4cb5917, stand-in c756e97d
---
# module: unique_function

A move-only callable, `UniqueFunction<R(Args...)>`, standing in for `std::move_only_function`, which libc++ (macOS) does not have. It holds any callable invocable as `R(Args...)`, including lambdas that own a `unique_ptr`, so tasks can carry move-only state across threads.

- **Owns:** the callable, on the heap.
- **Access:** public, header-only.
- **Required:** always.
- **Failure modes:** calling an empty one is undefined, as for `std::move_only_function`; callers check `operator bool` first.
- **Depends on:** none
- **Unknowns:** none

## class: UniqueFunction

- **Inputs:** a callable (implicitly, like `std::function`), `nullptr`, or nothing (empty).
- **State changes:** movable, not copyable; `explicit operator bool` says whether it holds one; `operator()` calls it.
- **Owns:** the callable.
- **Access:** public.
- **Referred by:** [event_queue](./event_queue.hpp.skel.md)
