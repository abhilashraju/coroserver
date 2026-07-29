#pragma once
/**
 * Lightweight utilities shared by all async SPDM coroutine headers.
 *
 * Placing these here avoids repeating the definitions in every per-command
 * header (get_version, get_capabilities, negotiate_algorithms, …).
 *
 * Contents
 * --------
 *  detail::ScopeExit   — RAII scope-exit guard (like C++23 std::scope_exit).
 *  detail::as_void_pp  — casts T** to void** for libspdm acquire-buffer APIs.
 */

#include <utility>

namespace detail
{

// ---------------------------------------------------------------------------
// ScopeExit — executes a callable on scope exit.
//
// Replaces std::scope_exit (<scope>, C++23) for older toolchains.
// Usage:
//   ScopeExit guard([&] { cleanup(); });
// The deduction guide below means the lambda type need not be spelled out.
// ---------------------------------------------------------------------------
template <typename F>
class ScopeExit
{
  public:
    explicit ScopeExit(F&& f) noexcept : fn_(std::move(f)) {}
    ~ScopeExit()
    {
        if (active_)
            fn_();
    }

    /// Cancel the deferred action (e.g. when ownership was transferred early).
    void dismiss() noexcept { active_ = false; }

    ScopeExit(const ScopeExit&)            = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

  private:
    F    fn_;
    bool active_ = true;
};

template <typename F>
ScopeExit(F&&) -> ScopeExit<F>;

// ---------------------------------------------------------------------------
// as_void_pp — casts the address of a typed pointer to void**.
//
// libspdm's acquire-buffer APIs use void** out-parameters.  Writing
//   as_void_pp(&ptr)
// is cleaner than sprinkling reinterpret_cast<void**>(&ptr) everywhere.
// ---------------------------------------------------------------------------
template <typename T>
[[nodiscard]] constexpr void** as_void_pp(T** ptr) noexcept
{
    return reinterpret_cast<void**>(ptr);
}

} // namespace detail
