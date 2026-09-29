#pragma once
#include "logger.hpp"
#include "sdbus_calls.hpp"
#include "utilities.hpp"

#include <systemd/sd-bus.h>

#include <chrono>
#include <ranges>
namespace NSNAME
{
template <typename Handler, typename PropType>
concept WatchHandler =
    requires(Handler handler, const boost::system::error_code& ec,
             std::optional<PropType> result) {
        { handler(ec, result) } -> std::same_as<boost::asio::awaitable<void>>;
    };

// Variant that allows the callback to return bool: true = keep watching,
// false = stop.
template <typename Handler, typename PropType>
concept WatchHandlerBool =
    requires(Handler handler, const boost::system::error_code& ec,
             std::optional<PropType> result) {
        { handler(ec, result) } -> std::same_as<boost::asio::awaitable<bool>>;
    };

// Trampoline context for the raw sd_bus C callback.
//
// Owns a weak_ptr to the watcher so that if the watcher has already been
// destroyed the callback is a safe no-op.  The context object is
// heap-allocated alongside the sdbusplus::bus::match slot and is freed
// only after sd_bus_slot_unref() returns — guaranteeing the raw pointer
// passed to sd_bus remains valid for the full lifetime of the slot.
template <typename Derived>
struct MatchContext
{
    explicit MatchContext(std::weak_ptr<Derived> w) : weak(std::move(w)) {}

    std::weak_ptr<Derived> weak;

    // C-style trampoline registered with sd_bus_add_match.
    // 'userdata' points to this MatchContext instance.
    static int callback(sd_bus_message* m, void* userdata,
                        sd_bus_error* /*e*/) noexcept
    {
        auto* ctx = static_cast<MatchContext*>(userdata);
        if (auto self = ctx->weak.lock())
        {
            try
            {
                sdbusplus::message_t msg{m};
                self->dispatchMessage(msg);
            }
            catch (...)
            {}
        }
        return 0;
    }
};

// Bundles a match slot with the context object whose raw pointer is held
// by sd_bus for the lifetime of that slot.
//
// Destruction order is critical: the slot (matchObj) must be destroyed
// first so sd_bus_slot_unref() fires before the context pointer becomes
// dangling; then matchCtx is destroyed.  The struct members are declared
// in that order, and C++ destroys members in reverse-declaration order,
// so the invariant is upheld automatically.
template <typename Derived>
struct MatchRegistration
{
    // matchObj must be declared first — destroyed last relative to matchCtx
    // is wrong; we need matchObj destroyed FIRST (reverse-declaration order).
    // Declare matchCtx first so it is destroyed last.
    std::shared_ptr<MatchContext<Derived>> matchCtx;
    std::optional<sdbusplus::bus::match::match> matchObj;

    MatchRegistration(sdbusplus::asio::connection& conn,
                      const std::string& rule, std::weak_ptr<Derived> weak) :
        matchCtx(std::make_shared<MatchContext<Derived>>(std::move(weak))),
        matchObj(std::in_place, conn, rule, &MatchContext<Derived>::callback,
                 static_cast<void*>(matchCtx.get()))
    {}
};

template <typename Derived, typename PropType>
struct DbusWatcher : std::enable_shared_from_this<Derived>
{
    using PROPERTY_HANDLER =
        std::function<void(boost::system::error_code, PropType)>;
    PROPERTY_HANDLER propHandler;
    std::shared_ptr<sdbusplus::asio::connection> conn;

    // Owns the match slot and its trampoline context together.
    // MatchRegistration enforces the correct teardown order: the slot is
    // destroyed before the context, so sd_bus can never fire a callback
    // through a dangling pointer.
    std::optional<MatchRegistration<Derived>> matchReg;

    DbusWatcher() = delete;
    DbusWatcher(const DbusWatcher&) = delete;
    DbusWatcher& operator=(const DbusWatcher&) = delete;
    DbusWatcher(DbusWatcher&&) = delete;
    DbusWatcher& operator=(DbusWatcher&&) = delete;
    DbusWatcher(std::shared_ptr<sdbusplus::asio::connection> c) : conn(c) {}
    ~DbusWatcher()
    {
        LOG_DEBUG(
            "DbusWatcher destructor called - cleaning up match and handler");
        propHandler = nullptr;
        matchReg.reset();
    }

    Derived& derived()
    {
        return static_cast<Derived&>(*this);
    }
    net::io_context& getIoContext()
    {
        return derived().getIoContextImpl();
    }
    // Default implementation — delegates to conn.  Derived classes (e.g.
    // TestWatcher in tests) may override this to supply a different io_context
    // without needing a real sdbusplus connection.
    net::io_context& getIoContextImpl()
    {
        return conn->get_io_context();
    }
    void startTimeout(std::shared_ptr<net::steady_timer> timer,
                      std::chrono::steady_clock::duration timeout)
    {
        timer->expires_after(timeout);
        std::weak_ptr<Derived> weak = derived().shared_from_this();
        auto secs =
            std::chrono::duration_cast<std::chrono::seconds>(timeout).count();
        LOG_INFO("[diag] startTimeout: arming {}s timer for watcher={}", secs,
                 static_cast<const void*>(this));
        // Capture timer by value (shared_ptr) so the timer object outlives
        // the coroutine frame.  When co_return destroys the frame the timer
        // is still held alive by this lambda until async_wait returns to Asio.
        timer->async_wait([timer, weak,
                           secs](const boost::system::error_code& ec) {
            if (!ec)
            {
                if (auto self = weak.lock())
                {
                    LOG_ERROR(
                        "Timeout occurred after {}s — watcher={} (firing cancelWatch)",
                        secs, static_cast<const void*>(self.get()));
                    self->cancelWatch();
                }
            }
            else
            {
                LOG_DEBUG("startTimeout timer cancelled ec={} watcher={}",
                          ec.message(), weak.expired() ? "expired" : "alive");
            }
        });
    }
    net::awaitable<void> watch(auto callback,
                               std::chrono::steady_clock::duration timeout =
                                   std::chrono::steady_clock::duration::max())
    {
        // Arm a timer when a finite timeout is requested.
        std::shared_ptr<net::steady_timer> timer;
        if (timeout != std::chrono::steady_clock::duration::max())
        {
            timer = std::make_shared<net::steady_timer>(getIoContext());
            startTimeout(timer, timeout);
        }

        boost::system::error_code ec{};
        while (true)
        {
            // makeWatchHandler() must be called inside the loop: it captures
            // the initiator lambda by move into async_initiate, so the first
            // h() call consumes the lambda.  A fresh h is needed each
            // iteration so the initiator re-runs and re-sets propHandler.
            auto h = makeWatchHandler();
            PropType res{};
            std::tie(ec, res) = co_await h();
            if (!ec)
            {
                if constexpr (WatchHandlerBool<decltype(callback), PropType>)
                {
                    if (!co_await callback(ec, std::optional(std::move(res))))
                        break;
                }
                else
                {
                    co_await callback(ec, std::optional(std::move(res)));
                }
                continue;
            }
            // operation_aborted is posted by cancelWatch() which is called by
            // the timeout timer — treat it as a timeout expiry and stop.
            if (ec == boost::asio::error::operation_aborted)
            {
                LOG_DEBUG("Watch timed out — stopping watch");
                co_await callback(boost::asio::error::timed_out, std::nullopt);
                break;
            }
            if (ec != boost::asio::error::no_such_device)
            {
                LOG_DEBUG(
                    "Unsupported type or some other error occurred: {} stopping watch",
                    ec.message());
                // Null propHandler so no further signals invoke it while the
                // callback is suspended.  matchObj is reset in ~DbusWatcher.
                propHandler = nullptr;
                co_await callback(ec, std::nullopt);
                break;
            }
        }
        // Cancel the timer (no-op if it already fired or was never armed).
        if (timer)
            timer->cancel();
        // propHandler is already null (moved out in notifyChange).
        // matchObj is reset in ~DbusWatcher when the caller drops the
        // watcher shared_ptr.
        co_return;
    }
    net::awaitable<std::optional<PropType>> watchOnce(
        std::chrono::seconds timeout = std::chrono::seconds(1))
    {
        auto timer = std::make_shared<net::steady_timer>(getIoContext());
        startTimeout(timer, timeout);
        auto h = makeWatchHandler();
        auto [ec, res] = co_await h(); // propHandler set + coroutine suspends
        timer->cancel();
        // Null propHandler so any late D-Bus callback finds nothing to invoke.
        propHandler = nullptr;
        if (ec)
        {
            LOG_ERROR("Error in watching Dbus: {}", ec.message());
            co_return std::nullopt;
        }
        co_return std::optional(res);
    }
    template <typename... Args>
    static void watch(net::io_context& ctx,
                      std::shared_ptr<sdbusplus::asio::connection> conn,
                      WatchHandler<PropType> auto callback, Args... args)
    {
        auto watcher = Derived::create(conn, args...);
        net::co_spawn(
            ctx,
            [&ctx, watcher,
             callback = std::move(callback)]() -> net::awaitable<void> {
                co_await watcher->watch([&ctx, callback = std::move(callback)](
                                            const boost::system::error_code& ec,
                                            std::optional<PropType> val)
                                            -> net::awaitable<void> {
                    net::co_spawn(
                        ctx,
                        [ec, callback,
                         val = std::move(val)]() -> net::awaitable<void> {
                            LOG_DEBUG(
                                "Invoking user async callback for Dbus property change");
                            co_await callback(ec, std::move(val));
                        },
                        net::detached);
                    co_return;
                });
            },
            net::detached);
    }
    auto makeWatchHandler()
    {
        return make_awaitable_handler<PropType>([this](auto promise) {
            auto promise_ptr =
                std::make_shared<decltype(promise)>(std::move(promise));

            propHandler = [promise_ptr](const boost::system::error_code& ec,
                                        PropType value) {
                promise_ptr->setValues(ec, std::move(value));
            };
        });
    }
    void notifyChange(const boost::system::error_code& ec, PropType value)
    {
        if (propHandler)
        {
            // Move out before invoking so the std::function stays alive on
            // the stack for the duration of its own execution.  The resumed
            // coroutine may clear propHandler (watchOnce line 133); moving
            // first means it clears the now-empty member, not the live
            // function on the stack — avoiding a use-after-free / SEGV.
            // watch() re-sets propHandler on the next loop iteration via a
            // fresh makeWatchHandler() call before it is needed again.
            auto h = std::move(propHandler);
            h(ec, std::move(value));
        }
    }
    void cancelWatch()
    {
        if (propHandler)
        {
            auto h = std::move(propHandler);
            h(boost::asio::error::operation_aborted, PropType{});
        }
    }
    void removeMatch()
    {
        matchReg.reset();
    }
};
template <typename TYPE>
struct DbusPropertyWatcher : public DbusWatcher<DbusPropertyWatcher<TYPE>, TYPE>
{
    using BASE = DbusWatcher<DbusPropertyWatcher<TYPE>, TYPE>;
    using PropType = TYPE;
    std::string propMatchRule;
    std::string propName;

  private:
    struct PrivateTag
    {};

  public:
    static std::shared_ptr<DbusPropertyWatcher<TYPE>> create(
        std::shared_ptr<sdbusplus::asio::connection> conn,
        const std::string& path, const std::string& intf,
        const std::string& prop)
    {
        auto watcher = std::make_shared<DbusPropertyWatcher<TYPE>>(
            PrivateTag{}, conn, path, intf, prop);
        watcher->addMatch();
        return watcher;
    }

    DbusPropertyWatcher(PrivateTag,
                        std::shared_ptr<sdbusplus::asio::connection> conn,
                        const std::string& path, const std::string& intf,
                        const std::string& prop) : BASE(conn), propName(prop)
    {
        propMatchRule =
            sdbusplus::bus::match::rules::propertiesChanged(path, intf);
    }

  public:
    // dispatchMessage is called by MatchContext::callback when a matching
    // D-Bus message arrives.  The watcher is alive (weak.lock() succeeded).
    void dispatchMessage(sdbusplus::message_t& msg)
    {
        handlePropertyChange(msg);
    }

  private:
    void addMatch()
    {
        BASE::matchReg.emplace(*BASE::conn, propMatchRule,
                               BASE::derived().shared_from_this());
    }
    void printChangedProperties(
        const std::map<std::string, std::variant<PropType>>& changedProperties)
    {
        for (const auto& [key, value] : changedProperties)
        {
            if (std::holds_alternative<PropType>(value))
            {
                auto val = std::get<PropType>(value);
                LOG_DEBUG("Changed Property: {} Value: {}", key, val);
            }
            else
            {
                LOG_DEBUG("Changed Property: {} Value: <non-string type>", key);
            }
        }
    }
    void handlePropertyChange(sdbusplus::message_t& msg)
    {
        std::string interfaceName;
        PropertyMap changedProperties;
        std::vector<std::string> invalidatedProperties;

        msg.read(interfaceName, changedProperties, invalidatedProperties);

        LOG_INFO("Properties changed on interface: {}", interfaceName);
        // printChangedProperties(changedProperties);
        auto [ec, ipaddress] =
            getPropertiesFromMap<PropType>(changedProperties, propName);

        if (ec && ec != boost::asio::error::not_found)
        {
            LOG_ERROR("Error getting property {}: {}", propName, ec.message());
            BASE::notifyChange(ec, PropType{});
            return;
        }
        LOG_DEBUG("Property {} changed: {}", propName, ipaddress);
        BASE::notifyChange(boost::system::error_code{}, ipaddress);
    }
};
template <typename TYPE>
struct DbusSignalWatcher : public DbusWatcher<DbusSignalWatcher<TYPE>, TYPE>
{
    using BASE = DbusWatcher<DbusSignalWatcher<TYPE>, TYPE>;
    using PropType = TYPE;

    std::string signalMatchRule;

  private:
    struct PrivateTag
    {};

  public:
    // ── Static factories ─────────────────────────────────────────────────
    // Each factory constructs the watcher with the correct match rule and
    // calls addMatch() exactly once.  There is no separate make() / builder
    // workflow; the rule is always fixed at construction time.

    static std::shared_ptr<DbusSignalWatcher<TYPE>> nameOwnerChanged(
        std::shared_ptr<sdbusplus::asio::connection> conn)
    {
        return makeWith(conn, sdbusplus::bus::match::rules::nameOwnerChanged());
    }

    static std::shared_ptr<DbusSignalWatcher<TYPE>> interfacesAdded(
        std::shared_ptr<sdbusplus::asio::connection> conn)
    {
        return makeWith(conn, sdbusplus::bus::match::rules::interfacesAdded());
    }

    static std::shared_ptr<DbusSignalWatcher<TYPE>> interfacesAdded(
        std::shared_ptr<sdbusplus::asio::connection> conn, std::string_view p)
    {
        return makeWith(conn, sdbusplus::bus::match::rules::interfacesAdded(p));
    }

    static std::shared_ptr<DbusSignalWatcher<TYPE>> interfacesAddedAtPath(
        std::shared_ptr<sdbusplus::asio::connection> conn, std::string_view p)
    {
        return makeWith(conn,
                        sdbusplus::bus::match::rules::interfacesAddedAtPath(p));
    }

    static std::shared_ptr<DbusSignalWatcher<TYPE>> interfacesRemoved(
        std::shared_ptr<sdbusplus::asio::connection> conn)
    {
        return makeWith(conn,
                        sdbusplus::bus::match::rules::interfacesRemoved());
    }

    static std::shared_ptr<DbusSignalWatcher<TYPE>> interfacesRemoved(
        std::shared_ptr<sdbusplus::asio::connection> conn, std::string_view p)
    {
        return makeWith(conn,
                        sdbusplus::bus::match::rules::interfacesRemoved(p));
    }

    static std::shared_ptr<DbusSignalWatcher<TYPE>> interfacesRemovedAtPath(
        std::shared_ptr<sdbusplus::asio::connection> conn, std::string_view p)
    {
        return makeWith(
            conn, sdbusplus::bus::match::rules::interfacesRemovedAtPath(p));
    }

    // create() — for callers that supply an explicit intf+signal pair or a
    // raw match rule string.
    static std::shared_ptr<DbusSignalWatcher<TYPE>> create(
        std::shared_ptr<sdbusplus::asio::connection> conn,
        const std::string& intf, const std::string& signal)
    {
        return makeWith(conn,
                        std::format("type='signal',interface='{}',member='{}'",
                                    intf, signal));
    }

    static std::shared_ptr<DbusSignalWatcher<TYPE>> create(
        std::shared_ptr<sdbusplus::asio::connection> conn,
        const std::string& matchRule)
    {
        return makeWith(conn, matchRule);
    }

    DbusSignalWatcher(PrivateTag,
                      std::shared_ptr<sdbusplus::asio::connection> conn,
                      std::string rule) :
        BASE(conn), signalMatchRule(std::move(rule))
    {}

  public:
    // dispatchMessage is called by MatchContext::callback when a matching
    // D-Bus message arrives.  The watcher is alive (weak.lock() succeeded).
    void dispatchMessage(sdbusplus::message_t& msg)
    {
        handleSignalChange(msg);
    }

  private:
    static std::shared_ptr<DbusSignalWatcher<TYPE>> makeWith(
        std::shared_ptr<sdbusplus::asio::connection> conn, std::string rule)
    {
        auto watcher = std::make_shared<DbusSignalWatcher<TYPE>>(
            PrivateTag{}, conn, std::move(rule));
        watcher->addMatch();
        return watcher;
    }

    void addMatch()
    {
        LOG_DEBUG("Adding signal match rule: {}", signalMatchRule);

        BASE::matchReg.emplace(*BASE::conn, signalMatchRule,
                               BASE::derived().shared_from_this());
    }
    void handleSignalChange(sdbusplus::message_t& msg)
    {
        if constexpr (std::is_same_v<PropType, sdbusplus::message_t>)
        {
            // LOG_DEBUG("Received Signal message");
            BASE::notifyChange(boost::system::error_code{}, std::move(msg));
            return;
        }
        else
        {
            PropType value;
            msg.read(value);
            LOG_DEBUG("Recieved Signal value {}", value);
            BASE::notifyChange(boost::system::error_code{}, value);
        }
    }
};

} // namespace NSNAME
