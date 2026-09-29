/**
 * @file test_dbusproperty_watcher.cpp
 * @brief Unit tests for DbusWatcher.
 *
 * Tests are driven synchronously by running the io_context to completion.
 * A real D-Bus daemon is not required: TestWatcher<T> inherits DbusWatcher<>
 * directly, holds a real boost::asio::io_context for the timer machinery,
 * constructs the base with a null connection, and exposes fire helpers that
 * call notifyChange() / cancelWatch() directly — the same code paths that a
 * real D-Bus callback would trigger.
 */
#define BOOST_TEST_MODULE DbusPropertyWatcherTests
#include <boost/test/included/unit_test.hpp>

// Pull in the production header under test.
// On a build host without sdbusplus, guard with a feature macro.
#ifdef HAVE_SDBUSPLUS
#include "dbusproperty_watcher.hpp"
#else
#warning "sdbusplus not found — skipping DbusWatcher tests"
int main()
{
    return 0;
}
#endif

#ifdef HAVE_SDBUSPLUS

#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace net = boost::asio;
using namespace std::chrono_literals;

// ─────────────────────────────────────────────────────────────────────────────
// TestWatcher — a concrete DbusWatcher<> that skips real D-Bus subscription.
//
// Construction:
//   auto w = TestWatcher<std::string>::make(ioc);
//
// Injection helpers (call from within a coroutine posted to ioc):
//   w->fireSignal("hello")        — simulates a D-Bus property-change callback
//   w->fireCancel()               — simulates a timeout / cancelWatch()
//   w->scheduleSignalAfterDelay(ms, v) — post-delay signal injection
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
struct TestWatcher : reactor::DbusWatcher<TestWatcher<T>, T>
{
    using BASE = reactor::DbusWatcher<TestWatcher<T>, T>;

  private:
    struct PrivateTag
    {};

  public:
    static std::shared_ptr<TestWatcher<T>> make(net::io_context& ioc)
    {
        // Pass nullptr for the sdbusplus connection — TestWatcher never calls
        // addMatch() or any real D-Bus operation.
        return std::make_shared<TestWatcher<T>>(PrivateTag{}, nullptr, ioc);
    }

    TestWatcher(PrivateTag, std::shared_ptr<sdbusplus::asio::connection> conn,
                net::io_context& ioc) : BASE(std::move(conn)), ioc_(ioc)
    {}

    // Override getIoContextImpl() so watchOnce() constructs its steady_timer
    // on the real io_context without going through the null conn pointer.
    net::io_context& getIoContextImpl()
    {
        return ioc_;
    }

    // ── Injection helpers ─────────────────────────────────────────────────

    /// Deliver a successful signal value.
    void fireSignal(T value)
    {
        BASE::notifyChange(boost::system::error_code{}, std::move(value));
    }

    /// Deliver a cancellation.
    void fireCancel()
    {
        BASE::cancelWatch();
    }

    /// Deliver an error.
    void fireError(boost::system::error_code ec)
    {
        BASE::notifyChange(ec, T{});
    }

    /// Post a signal delivery after `delay`.
    void scheduleSignalAfterDelay(std::chrono::milliseconds delay, T value)
    {
        auto timer = std::make_shared<net::steady_timer>(ioc_);
        timer->expires_after(delay);
        std::weak_ptr<TestWatcher<T>> weak = BASE::derived().shared_from_this();
        timer->async_wait([timer, weak, value = std::move(value)](
                              const boost::system::error_code& ec) mutable {
            if (!ec)
            {
                if (auto self = weak.lock())
                {
                    self->fireSignal(std::move(value));
                }
            }
        });
    }

    /// Post a cancellation after `delay`.
    void scheduleCancelAfterDelay(std::chrono::milliseconds delay)
    {
        auto timer = std::make_shared<net::steady_timer>(ioc_);
        timer->expires_after(delay);
        std::weak_ptr<TestWatcher<T>> weak = BASE::derived().shared_from_this();
        timer->async_wait([timer, weak](const boost::system::error_code& ec) {
            if (!ec)
            {
                if (auto self = weak.lock())
                {
                    self->fireCancel();
                }
            }
        });
    }

  private:
    net::io_context& ioc_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: run a coroutine on ioc and block until it finishes, capturing any
// unhandled exception as a test failure.
// ─────────────────────────────────────────────────────────────────────────────
template <typename Coro>
void runCoro(net::io_context& ioc, Coro&& coro)
{
    bool finished = false;
    net::co_spawn(
        ioc,
        [&finished,
         coro = std::forward<Coro>(coro)]() mutable -> net::awaitable<void> {
            co_await std::move(coro);
            finished = true;
        },
        [](std::exception_ptr ep) {
            if (ep)
                std::rethrow_exception(ep);
        });
    ioc.run();
    ioc.restart();
    BOOST_TEST(finished);
}

// ─────────────────────────────────────────────────────────────────────────────
// SUITE 1 — watchOnce() happy path
// ─────────────────────────────────────────────────────────────────────────────
BOOST_AUTO_TEST_SUITE(WatchOnce_HappyPath)

BOOST_AUTO_TEST_CASE(signal_arrives_before_timeout_returns_value)
{
    // Arrange
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    watcher->scheduleSignalAfterDelay(10ms, "hello");

    // Act
    std::optional<std::string> result;
    runCoro(ioc, [&]() -> net::awaitable<void> {
        result = co_await watcher->watchOnce(5s);
    }());

    // Assert
    BOOST_REQUIRE(result.has_value());
    BOOST_TEST(*result == "hello");
}

BOOST_AUTO_TEST_CASE(match_is_reset_after_watcher_destroyed)
{
    // After watchOnce() returns, propHandler is null. Dropping the watcher
    // posts the match destructor to the event loop; draining it must not crash.
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);
    watcher->scheduleSignalAfterDelay(10ms, "value");

    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watchOnce(5s);
    }());

    BOOST_TEST(!watcher->propHandler);
    watcher.reset();
    ioc.run();
    BOOST_TEST(true);
}

BOOST_AUTO_TEST_CASE(propHandler_is_null_after_successful_watchOnce)
{
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);
    watcher->scheduleSignalAfterDelay(10ms, "value");

    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watchOnce(5s);
    }());

    BOOST_TEST(!watcher->propHandler);
}

BOOST_AUTO_TEST_SUITE_END()

// ─────────────────────────────────────────────────────────────────────────────
// SUITE 2 — watchOnce() timeout path
// ─────────────────────────────────────────────────────────────────────────────
BOOST_AUTO_TEST_SUITE(WatchOnce_Timeout)

BOOST_AUTO_TEST_CASE(timeout_fires_returns_nullopt)
{
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    // Schedule a cancel before the watchOnce timeout elapses; no signal fires.
    watcher->scheduleCancelAfterDelay(10ms);

    std::optional<std::string> result = "sentinel"; // must be overwritten
    runCoro(ioc, [&]() -> net::awaitable<void> {
        result = co_await watcher->watchOnce(5s);
    }());

    BOOST_TEST(!result.has_value());
}

BOOST_AUTO_TEST_CASE(match_is_reset_when_watcher_dropped_after_timeout)
{
    // propHandler is nulled on timeout; dropping the watcher posts the match
    // destructor to the next event-loop iteration — must not crash.
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);
    watcher->scheduleCancelAfterDelay(10ms);

    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watchOnce(5s);
    }());

    BOOST_TEST(!watcher->propHandler);
    watcher.reset();
    ioc.run();
    BOOST_TEST(true);
}

BOOST_AUTO_TEST_CASE(propHandler_is_null_after_timeout)
{
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);
    watcher->scheduleCancelAfterDelay(10ms);

    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watchOnce(5s);
    }());

    BOOST_TEST(!watcher->propHandler);
}

BOOST_AUTO_TEST_CASE(stale_signal_after_timeout_does_not_crash)
{
    // A signal that arrives after watchOnce() has already returned via timeout
    // must be silently dropped (propHandler is null → notifyChange is a no-op).
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    bool staleSignalFired = false;

    watcher->scheduleCancelAfterDelay(10ms);

    // Schedule a late signal that fires after the timeout path has completed.
    {
        auto timer = std::make_shared<net::steady_timer>(ioc);
        timer->expires_after(30ms);
        auto weak = std::weak_ptr<TestWatcher<std::string>>(watcher);
        timer->async_wait([timer, weak, &staleSignalFired](
                              const boost::system::error_code& ec) mutable {
            staleSignalFired = true;
            if (!ec)
            {
                if (auto self = weak.lock())
                {
                    self->fireSignal("late_signal");
                }
            }
        });
    }

    std::optional<std::string> result;
    runCoro(ioc, [&]() -> net::awaitable<void> {
        result = co_await watcher->watchOnce(5s);
        // Yield past the late-signal timer so it fires before ioc finishes.
        net::steady_timer yield(co_await net::this_coro::executor);
        yield.expires_after(50ms);
        co_await yield.async_wait(net::use_awaitable);
    }());

    BOOST_TEST(!result.has_value()); // timeout → nullopt
    BOOST_TEST(staleSignalFired);    // the late signal did fire
    BOOST_TEST(!watcher->propHandler);
}

BOOST_AUTO_TEST_SUITE_END()

// ─────────────────────────────────────────────────────────────────────────────
// SUITE 3 — cancelWatch() robustness
// ─────────────────────────────────────────────────────────────────────────────
BOOST_AUTO_TEST_SUITE(CancelWatch_Robustness)

BOOST_AUTO_TEST_CASE(cancelWatch_on_null_propHandler_is_noop)
{
    // Calling cancelWatch() when propHandler is already null must not crash.
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    BOOST_REQUIRE(!watcher->propHandler);
    BOOST_CHECK_NO_THROW(watcher->fireCancel());
}

BOOST_AUTO_TEST_CASE(cancelWatch_twice_is_safe)
{
    // A second cancelWatch() after the first must be a no-op.
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    bool handlerCalled = false;
    runCoro(ioc, [&]() -> net::awaitable<void> {
        net::co_spawn(
            co_await net::this_coro::executor,
            [&]() -> net::awaitable<void> {
                watcher->fireCancel();
                watcher->fireCancel(); // second call must be a no-op
                co_return;
            }(),
            net::detached);

        auto result = co_await watcher->watchOnce(5s);
        handlerCalled = !result.has_value();
    }());

    BOOST_TEST(handlerCalled);
}

BOOST_AUTO_TEST_SUITE_END()

// ─────────────────────────────────────────────────────────────────────────────
// SUITE 4 — notifyChange() move-out safety
// ─────────────────────────────────────────────────────────────────────────────
BOOST_AUTO_TEST_SUITE(NotifyChange_MoveOutSafety)

BOOST_AUTO_TEST_CASE(signal_delivery_resumes_coroutine_exactly_once)
{
    // The first signal must resume the coroutine and deliver the value.
    // A second signal arriving after watchOnce() has returned must be a no-op
    // (propHandler is null at that point).
    net::io_context ioc;
    auto watcher = TestWatcher<int>::make(ioc);

    watcher->scheduleSignalAfterDelay(10ms, 42);
    watcher->scheduleSignalAfterDelay(20ms, 99); // arrives after watchOnce returns

    int received = -1;
    runCoro(ioc, [&]() -> net::awaitable<void> {
        auto result = co_await watcher->watchOnce(5s);
        if (result)
            received = *result;
        // Yield past the second-signal timer so it fires before ioc finishes.
        net::steady_timer yield(co_await net::this_coro::executor);
        yield.expires_after(30ms);
        co_await yield.async_wait(net::use_awaitable);
    }());

    BOOST_TEST(received == 42);        // only the first signal is received
    BOOST_TEST(!watcher->propHandler); // second fire was a no-op
}

BOOST_AUTO_TEST_SUITE_END()

// ─────────────────────────────────────────────────────────────────────────────
// SUITE 5 — watch() loop with WatchHandlerBool
// ─────────────────────────────────────────────────────────────────────────────
BOOST_AUTO_TEST_SUITE(Watch_BoolCallback_Loop)

BOOST_AUTO_TEST_CASE(watch_loop_receives_multiple_signals_until_false)
{
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    // Fire three signals at increasing delays; the callback stops on the third.
    for (int i = 1; i <= 3; ++i)
    {
        watcher->scheduleSignalAfterDelay(
            std::chrono::milliseconds(i * 10),
            std::string(1, static_cast<char>('A' + i - 1)));
    }

    std::vector<std::string> received;
    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watch(
            [&received](
                const boost::system::error_code&,
                std::optional<std::string> val) -> net::awaitable<bool> {
                if (val)
                    received.push_back(*val);
                co_return received.size() < 3; // stop after 3
            });
    }());

    BOOST_TEST(received.size() == 3u);
    BOOST_TEST(received[0] == "A");
    BOOST_TEST(received[1] == "B");
    BOOST_TEST(received[2] == "C");
}

BOOST_AUTO_TEST_CASE(
    watch_loop_match_reset_when_watcher_dropped_after_bool_false)
{
    // propHandler is null after the loop exits; dropping the watcher posts the
    // match destructor to the event loop — must not crash.
    net::io_context ioc;
    auto watcher = TestWatcher<int>::make(ioc);
    watcher->scheduleSignalAfterDelay(10ms, 1);

    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watch([](const boost::system::error_code&,
                                   std::optional<int>) -> net::awaitable<bool> {
            co_return false; // stop immediately on first signal
        });
    }());

    BOOST_TEST(!watcher->propHandler);
    watcher.reset();
    ioc.run();
    BOOST_TEST(true);
}

BOOST_AUTO_TEST_CASE(watch_loop_propHandler_null_in_error_callback)
{
    // propHandler must be null when the error callback is entered so that a
    // stale signal cannot double-complete the promise.
    net::io_context ioc;
    auto watcher = TestWatcher<int>::make(ioc);

    watcher->scheduleSignalAfterDelay(10ms, 0);
    {
        // After the first good signal the loop re-suspends; fire an error.
        auto timer = std::make_shared<net::steady_timer>(ioc);
        timer->expires_after(30ms);
        auto weak = std::weak_ptr<TestWatcher<int>>(watcher);
        timer->async_wait([timer, weak](const boost::system::error_code& tec) {
            if (!tec)
                if (auto self = weak.lock())
                    self->fireError(boost::asio::error::operation_aborted);
        });
    }

    bool errorCallbackSawNullPropHandler = false;
    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watch(
            [&](const boost::system::error_code& ec,
                std::optional<int>) -> net::awaitable<void> {
                if (ec)
                {
                    errorCallbackSawNullPropHandler = !watcher->propHandler;
                }
                co_return;
            });
    }());

    BOOST_TEST(errorCallbackSawNullPropHandler);
    watcher.reset();
    ioc.run();
    BOOST_TEST(true);
}

BOOST_AUTO_TEST_CASE(stale_signal_after_watch_loop_exit_does_not_crash)
{
    // A signal that arrives after watch() exits (propHandler null) must be
    // silently dropped — must not crash.
    net::io_context ioc;
    auto watcher = TestWatcher<int>::make(ioc);
    watcher->scheduleSignalAfterDelay(10ms, 1);

    bool staleSignalFired = false;
    {
        auto timer = std::make_shared<net::steady_timer>(ioc);
        timer->expires_after(40ms);
        auto weak = std::weak_ptr<TestWatcher<int>>(watcher);
        timer->async_wait([timer, weak, &staleSignalFired](
                              const boost::system::error_code& ec) mutable {
            staleSignalFired = true;
            if (!ec)
                if (auto self = weak.lock())
                    self->fireSignal(999);
        });
    }

    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watch([](const boost::system::error_code&,
                                   std::optional<int>) -> net::awaitable<bool> {
            co_return false; // stop on first signal
        });
        // Yield to let the late-signal timer fire.
        net::steady_timer yield(co_await net::this_coro::executor);
        yield.expires_after(60ms);
        co_await yield.async_wait(net::use_awaitable);
    }());

    BOOST_TEST(staleSignalFired);
    BOOST_TEST(!watcher->propHandler);
}

BOOST_AUTO_TEST_SUITE_END()

// ─────────────────────────────────────────────────────────────────────────────
// SUITE 5b — watch() with timeout
//
// watch() accepts an optional duration.  When it fires, cancelWatch() delivers
// operation_aborted which watch() translates to timed_out and passes to the
// callback as callback(timed_out, nullopt) before breaking the loop.
// ─────────────────────────────────────────────────────────────────────────────
BOOST_AUTO_TEST_SUITE(Watch_WithTimeout)

BOOST_AUTO_TEST_CASE(timeout_before_any_signal_delivers_timed_out_error)
{
    // No signals are scheduled; the timeout must fire, call the callback with
    // timed_out and nullopt, then let watch() return.
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    boost::system::error_code callbackEc;
    bool callbackGotNullopt = false;

    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watch(
            [&](const boost::system::error_code& ec,
                std::optional<std::string> val) -> net::awaitable<void> {
                callbackEc = ec;
                callbackGotNullopt = !val.has_value();
                co_return;
            },
            50ms);
    }());

    BOOST_TEST(callbackEc == boost::asio::error::timed_out);
    BOOST_TEST(callbackGotNullopt);
    BOOST_TEST(!watcher->propHandler);
}

BOOST_AUTO_TEST_CASE(signal_before_timeout_is_delivered_then_timeout_stops_loop)
{
    // One signal arrives before the timeout; the bool callback returns true to
    // continue.  The timeout then fires and stops the loop.
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    watcher->scheduleSignalAfterDelay(10ms, "early");

    std::vector<std::string> received;
    boost::system::error_code finalEc;

    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watch(
            [&](const boost::system::error_code& ec,
                std::optional<std::string> val) -> net::awaitable<bool> {
                if (val)
                    received.push_back(*val);
                else
                    finalEc = ec;
                co_return val.has_value(); // continue while value present
            },
            100ms);
    }());

    BOOST_TEST(received.size() == 1u);
    BOOST_TEST(received[0] == "early");
    BOOST_TEST(finalEc == boost::asio::error::timed_out);
    BOOST_TEST(!watcher->propHandler);
}

BOOST_AUTO_TEST_CASE(multiple_signals_before_timeout_all_delivered)
{
    // Several signals arrive within the timeout window; all must be delivered.
    // After the last one the bool callback returns false, stopping the loop
    // before the timeout fires.
    net::io_context ioc;
    auto watcher = TestWatcher<int>::make(ioc);

    for (int i = 0; i < 3; ++i)
        watcher->scheduleSignalAfterDelay(std::chrono::milliseconds((i + 1) * 10),
                                         i + 1);

    std::vector<int> received;
    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watch(
            [&](const boost::system::error_code&,
                std::optional<int> val) -> net::awaitable<bool> {
                if (val)
                    received.push_back(*val);
                co_return received.size() < 3; // stop after 3
            },
            500ms);
    }());

    BOOST_TEST(received.size() == 3u);
    BOOST_TEST(received[0] == 1);
    BOOST_TEST(received[1] == 2);
    BOOST_TEST(received[2] == 3);
    BOOST_TEST(!watcher->propHandler);
}

BOOST_AUTO_TEST_CASE(timeout_fires_propHandler_is_null_afterwards)
{
    // propHandler must be null after watch() returns via timeout.
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watch(
            [](const boost::system::error_code&,
               std::optional<std::string>) -> net::awaitable<void> {
                co_return;
            },
            30ms);
    }());

    BOOST_TEST(!watcher->propHandler);
}

BOOST_AUTO_TEST_CASE(watch_with_timeout_void_callback_receives_timed_out)
{
    // Void-callback overload: the timeout callback receives timed_out + nullopt
    // and then watch() returns normally.
    net::io_context ioc;
    auto watcher = TestWatcher<int>::make(ioc);

    boost::system::error_code seenEc;
    bool seenNullopt = false;
    int callCount = 0;

    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watch(
            [&](const boost::system::error_code& ec,
                std::optional<int> val) -> net::awaitable<void> {
                ++callCount;
                seenEc = ec;
                seenNullopt = !val.has_value();
                co_return;
            },
            30ms);
    }());

    BOOST_TEST(callCount == 1);
    BOOST_TEST(seenEc == boost::asio::error::timed_out);
    BOOST_TEST(seenNullopt);
}

BOOST_AUTO_TEST_CASE(stale_signal_after_watch_timeout_does_not_crash)
{
    // A signal that arrives after watch() has exited via timeout (propHandler
    // null) must be silently dropped — must not crash.
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    bool staleSignalFired = false;
    {
        auto timer = std::make_shared<net::steady_timer>(ioc);
        timer->expires_after(80ms);
        auto weak = std::weak_ptr<TestWatcher<std::string>>(watcher);
        timer->async_wait([timer, weak, &staleSignalFired](
                              const boost::system::error_code& ec) mutable {
            staleSignalFired = true;
            if (!ec)
                if (auto self = weak.lock())
                    self->fireSignal("late");
        });
    }

    runCoro(ioc, [&]() -> net::awaitable<void> {
        co_await watcher->watch(
            [](const boost::system::error_code&,
               std::optional<std::string>) -> net::awaitable<void> {
                co_return;
            },
            30ms);
        // Yield past the late-signal timer so it fires before ioc finishes.
        net::steady_timer yield(co_await net::this_coro::executor);
        yield.expires_after(100ms);
        co_await yield.async_wait(net::use_awaitable);
    }());

    BOOST_TEST(staleSignalFired);
    BOOST_TEST(!watcher->propHandler);
}

BOOST_AUTO_TEST_SUITE_END()

// ─────────────────────────────────────────────────────────────────────────────
// SUITE 6 — watcher lifetime / shared_ptr safety
// ─────────────────────────────────────────────────────────────────────────────
BOOST_AUTO_TEST_SUITE(WatcherLifetime)

BOOST_AUTO_TEST_CASE(watcher_destroyed_before_signal_fires)
{
    // The weak_ptr guard in scheduleSignalAfterDelay must prevent a
    // use-after-free when the watcher is destroyed before the delayed signal
    // fires.
    net::io_context ioc;

    {
        auto watcher = TestWatcher<std::string>::make(ioc);
        watcher->scheduleSignalAfterDelay(50ms, "ghost");
        // watcher destroyed here — only the timer's weak_ptr remains
    }

    // Run long enough for the timer to fire; weak.lock() returns null → no-op.
    net::steady_timer drain(ioc);
    drain.expires_after(80ms);
    drain.async_wait([](const boost::system::error_code&) {});
    ioc.run();
    ioc.restart();
    BOOST_TEST(true);
}

BOOST_AUTO_TEST_CASE(watcher_outlives_watchOnce_coroutine)
{
    // The watcher shared_ptr must keep the object alive across the coroutine
    // suspension point even when the outer caller does not hold a reference.
    net::io_context ioc;
    std::weak_ptr<TestWatcher<int>> weak;

    {
        auto watcher = TestWatcher<int>::make(ioc);
        weak = watcher;

        watcher->scheduleSignalAfterDelay(10ms, 7);

        net::co_spawn(
            ioc,
            [w = watcher]() mutable -> net::awaitable<void> {
                // w is the only reference; the coroutine must keep the watcher alive.
                co_await w->watchOnce(5s);
            }(),
            net::detached);
        // 'watcher' goes out of scope — only the lambda capture keeps it alive
    }

    ioc.run();
    ioc.restart();

    // After the coroutine finishes the last shared_ptr is gone.
    BOOST_TEST(weak.expired());
}

BOOST_AUTO_TEST_SUITE_END()

// ─────────────────────────────────────────────────────────────────────────────
// SUITE 7 — MatchContext lifetime safety
// ─────────────────────────────────────────────────────────────────────────────
BOOST_AUTO_TEST_SUITE(ExactProductionCrashReproduction)

BOOST_AUTO_TEST_CASE(signal_after_watchonce_returns_is_safe_noop)
{
    // A signal fired after watchOnce() has already returned (propHandler null)
    // must be a no-op — must not crash or resume the completed coroutine.
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    watcher->scheduleCancelAfterDelay(10ms);

    std::optional<std::string> result;
    runCoro(ioc, [&]() -> net::awaitable<void> {
        result = co_await watcher->watchOnce(5s);
    }());

    // Fire a late signal after watchOnce has returned — must be a no-op.
    watcher->fireSignal("late_signal");
    ioc.run();

    BOOST_TEST(!result.has_value());
    BOOST_TEST(true);
}

BOOST_AUTO_TEST_CASE(signal_after_watcher_destroyed_is_safe_noop)
{
    // A signal fired after the watcher has been destroyed must be dropped by
    // the weak_ptr guard in MatchContext::callback — no use-after-free.
    net::io_context ioc;
    auto watcher = TestWatcher<std::string>::make(ioc);

    using Ctx = reactor::MatchContext<TestWatcher<std::string>>;
    auto ctx =
        std::make_shared<Ctx>(std::weak_ptr<TestWatcher<std::string>>(watcher));

    int fireCount = 0;

    // Drop the watcher — the weak_ptr inside ctx is now expired.
    watcher.reset();
    ioc.run();

    // Simulate a callback arriving after the slot was freed.
    if (auto self = ctx->weak.lock())
    {
        fireCount++;
        self->fireSignal("should_not_fire");
    }

    BOOST_TEST(fireCount == 0); // weak was expired, no fire
    BOOST_TEST(true);
}

BOOST_AUTO_TEST_SUITE_END()

#endif // HAVE_SDBUSPLUS
