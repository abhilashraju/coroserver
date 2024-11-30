#include "when_all.hpp"

#include "logger.hpp"
#include "webclient.hpp"
net::awaitable<void> getAll(net::io_context& ioc, auto... tasks)
{
    auto [res1, res2] = co_await when_all(ioc, std::move(tasks)...);
    LOG_INFO("Respnses: {}\n\n {}", res1.body(), res2.body());
}
net::awaitable<void> getAllResults(net::io_context& ioc, const auto& tasks)
{
    auto results = co_await when_all(ioc, tasks);
    for (auto& res : results)
    {
        LOG_INFO("Print: {}\n\n", res);
    }
}

int main()
{
    net::io_context ioc;
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<unix_domain::socket> client(ioc, ctx);
    auto task_maker = [&](std::string ep) {
        return [ep, &ctx, &ioc]() -> net::awaitable<Response> {
            WebClient<beast::tcp_stream> client(ioc, ctx);

            client.withHost(ep)
                .withPort("443")
                .withMethod(http::verb::get)
                .withTarget("/")
                .withRetries(3)
                .withHeaders({{"User-Agent", "coro-client"}});
            auto [ec, res] = co_await client.execute<Response>();
            co_return res;
        };
    };

    net::co_spawn(
        ioc,
        getAll(ioc, task_maker("www.google.com"), task_maker("www.yahoo.com")),
        net::detached);

    auto printerTask = [](int i) {
        return [i]() -> net::awaitable<int> {
            net::steady_timer timer(co_await net::this_coro::executor);
            timer.expires_after(std::chrono::seconds(2));
            co_await timer.async_wait(net::use_awaitable);
            co_return i;
        };
    };
    std::vector tasks = {printerTask(1), printerTask(2), printerTask(3)};
    net::co_spawn(ioc, getAllResults(ioc, tasks), net::detached);
    ioc.run();
    return 0;
}
