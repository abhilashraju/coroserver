#pragma once
#include "beastdefs.hpp"
template <typename Tuple, std::size_t Index = 0>
void set_value(Tuple& tuple, auto v, std::size_t index)
{
    if constexpr (Index < std::tuple_size_v<Tuple>)
    {
        if (index == Index)
        {
            if constexpr (std::is_same_v<
                              std::remove_reference_t<decltype(v)>,
                              std::remove_reference_t<
                                  std::tuple_element_t<Index, Tuple>>>)
            {
                std::get<Index>(tuple) = v;
            }
        }
        else
        {
            set_value<Tuple, Index + 1>(tuple, std::move(v), index);
        }
    }
    else
    {
        throw std::out_of_range("Index out of range");
    }
}

template <std::size_t... Indices, typename... Awaitables>
net::awaitable<
    std::tuple<typename std::invoke_result_t<Awaitables>::value_type...>>
    invoke_awaitables(net::io_context& ioc, std::index_sequence<Indices...>,
                      std::tuple<Awaitables...> awaitables)
{
    std::tuple<typename std::invoke_result_t<Awaitables>::value_type...>
        results;
    std::atomic<int> counter(sizeof...(Awaitables));
    std::promise<void> all_done_promise;
    auto all_done_future = all_done_promise.get_future();

    auto handler = [&](auto result, auto index) {
        set_value(results, std::move(result), index);
        if (--counter == 0)
        {
            all_done_promise.set_value();
        }
    };

    (
        [index = Indices, &handler, &ioc](auto await) {
            net::co_spawn(
                ioc,
                [index, &handler,
                 await = std::move(await)]() mutable -> net::awaitable<void> {
                    auto result = co_await await();
                    handler(std::move(result), index);
                },
                net::detached);
        }(std::get<Indices>(awaitables)),
        ...);
    // co_await net::use_future(all_done_future);
    while (counter > 0)
    {
        co_await net::post(co_await net::this_coro::executor,
                           net::use_awaitable);
    }
    co_return results;
}

template <typename... Awaitables>
net::awaitable<
    std::tuple<typename std::invoke_result_t<Awaitables>::value_type...>>
    when_all(net::io_context& ioc, Awaitables... awaitables)
{
    co_return co_await invoke_awaitables(
        ioc, std::index_sequence_for<Awaitables...>{},
        std::make_tuple(std::move(awaitables)...));
}

template <typename Awaitable>
net::awaitable<
    std::vector<typename std::invoke_result_t<Awaitable>::value_type>>
    when_all(net::io_context& ioc, const std::vector<Awaitable>& awaitables)
{
    std::vector<typename std::invoke_result_t<Awaitable>::value_type> results(
        awaitables.size());
    std::atomic<int> counter(awaitables.size());
    for (const auto& index : std::views::iota(0u, awaitables.size()))
    {
        net::co_spawn(
            ioc,
            [index, &results, &counter,
             awaitable = awaitables[index]]() mutable -> net::awaitable<void> {
                results[index] = co_await awaitable();
                counter--;
            },
            net::detached);
    }
    // co_await net::use_future(all_done_future);
    while (counter > 0)
    {
        co_await net::post(co_await net::this_coro::executor,
                           net::use_awaitable);
    }
    co_return results;
}
