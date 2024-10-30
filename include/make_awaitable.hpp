#pragma once
#include "beastdefs.hpp"
inline net::use_awaitable_t<>& mut_awaitable()
{
    static net::use_awaitable_t<> myawitable;
    return myawitable;
}

template <typename... Types>
using PrependEC = std::tuple<boost::system::error_code, Types...>;
template <typename... RetTypes>
using ReturnTuple = std::conditional_t<
    std::is_same_v<boost::system::error_code,
                   std::tuple_element_t<0, std::tuple<RetTypes...>>>,
    std::tuple<RetTypes...>, PrependEC<RetTypes...>>;

template <typename... Types>
using AwaitableResult = net::awaitable<ReturnTuple<Types...>>;

template <typename... Ret, typename HanlderFunc>
auto make_awaitable_handler(HanlderFunc&& h)
{
    return [h = std::move(h)]() -> AwaitableResult<Ret...> {
        co_return co_await net::async_initiate<
            net::use_awaitable_t<>, ReturnTuple<Ret...>(ReturnTuple<Ret...>)>(
            [h = std::move(h)](auto handler) {
                if constexpr (std::is_same_v<
                                  boost::system::error_code,
                                  std::tuple_element_t<0, std::tuple<Ret...>>>)
                {
                    auto callback =
                        [handler = std::move(handler)](Ret... values) mutable {
                            handler(ReturnTuple<Ret...>{std::move(values)...});
                        };
                    h(std::move(callback));
                }
                else
                {
                    auto callback = [handler = std::move(handler)](
                                        boost::system::error_code ec,
                                        Ret... values) mutable {
                        handler(ReturnTuple<Ret...>{ec, std::move(values)...});
                    };
                    h(std::move(callback));
                }
            },
            mut_awaitable());
    };
}