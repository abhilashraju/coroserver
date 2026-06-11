#include "when_all.hpp"

#include "logger.hpp"
#include "webclient.hpp"
using namespace NSNAME;
net::awaitable<void> getAll(net::awaitable<Response> await1,
                            net::awaitable<Response> await2)
{
    LOG_INFO("getAll: entered");
    LOG_INFO("getAll: awaiting tuple when_all");
    auto [res1, res2] = co_await when_all(std::move(await1), std::move(await2));
    LOG_INFO("getAll: tuple when_all completed");
    LOG_INFO("getAll: network requests completed");
}
net::awaitable<void> getAllResults(auto tasks)
{
    LOG_INFO("getAllResults: entered");
    auto results = co_await when_all(std::move(tasks));
    LOG_INFO("getAllResults: vector when_all completed");
    for (auto& res : results)
    {
        LOG_INFO("Print: {}\n\n", res);
    }
}

net::awaitable<Response> getSite(std::string ep, net::io_context& ioc,
                                 ssl::context& ctx)
{
    WebClient<beast::tcp_stream> client(ioc, ctx);

    client.withHost(ep)
        .withPort("443")
        .withMethod(http::verb::get)
        .withTarget("/")
        .withRetries(3)
        .withHeaders({{"User-Agent", "coro-client"}});
    auto [ec, res] = co_await client.execute<Response>();
    co_return res;
}

int main()
{
    // Set log level to INFO so we can see the output
    NSNAME::getLogger().setLogLevel(NSNAME::LogLevel::DEBUG);

    net::io_context ioc;
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<unix_domain::socket> client(ioc, ctx);

    auto printerTask = [](int i) -> net::awaitable<int> {
        LOG_INFO("printerTask {}: created", i);
        net::steady_timer timer(co_await net::this_coro::executor);
        LOG_INFO("printerTask {}: timer start", i);
        timer.expires_after(std::chrono::seconds(2));
        co_await timer.async_wait(net::use_awaitable);
        LOG_INFO("printerTask {}: timer done", i);
        co_return i;
    };

    net::co_spawn(
        ioc,
        [&]() -> net::awaitable<void> {
            LOG_INFO("main coroutine: entered");
            std::vector<net::awaitable<int>> tasks;
            tasks.push_back(printerTask(1));
            tasks.push_back(printerTask(2));
            tasks.push_back(printerTask(3));
            LOG_INFO("main coroutine: awaiting outer when_all");

            co_await when_all(getAll(getSite("www.google.com", ioc, ctx),
                                     getSite("www.yahoo.com", ioc, ctx)),
                              getAllResults(std::move(tasks)));
            LOG_INFO("main coroutine: outer when_all completed");
        }(),
        net::detached);
    ioc.run();
    return 0;
}
