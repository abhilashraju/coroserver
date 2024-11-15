# Reactor 

The Reactor library provides asynchronous APIs for a https client and server appplications.The APIs are based on C++20 coroutine, boost/asio and boost/beast libraries.  

## Prerequisites

- C++20 or later
- Boost libraries (Asio, Beast, System)
- OpenSSL
- nlohmann Json
- sdbusplus

## Building the Project

### Using Meson Build System
#### Install Ninja

To build the project, you need to have Ninja installed. You can install Ninja using the following commands:

For Debian/Ubuntu:
```sh
sudo apt-get install ninja-build
```

For Fedora:
```sh
sudo dnf install ninja-build
```

For Arch Linux:
```sh
sudo pacman -S ninja
```

For macOS using Homebrew:
```sh
brew install ninja
```
#### Install Dependencies

Before building the project, you need to install the required dependencies. You can install them using the following commands:

For Debian/Ubuntu:
```sh
sudo apt-get install libboost-all-dev libssl-dev nlohmann-json3-dev
```

For Fedora:
```sh
sudo dnf install boost-devel openssl-devel nlohmann-json-devel
```

For Arch Linux:
```sh
sudo pacman -S boost openssl nlohmann-json
```

For macOS using Homebrew:
```sh
brew install boost openssl nlohmann-json
```
#### Clone and Build sdbusplus

Before building the project, you need to clone and build the sdbusplus library. You can do this using the following commands:

```sh
git clone https://github.com/openbmc/sdbusplus.git
cd sdbusplus
meson build
ninja -C build
sudo ninja -C build install
cd ..
```
#### Build the Project

Once you have installed all the dependencies and built the `sdbusplus` library, you can build the project using the following commands:

```sh
meson setup build
ninja -C build
```

#### Build Examples

To build the example applications provided in the project, you can use the following commands:

```sh
meson setup build -Dexamples=true
ninja -C build
```
## Example: Web Client to Download Content from Google.com

Here is an example of a simple web client that downloads content from `remote server(google.com)` using the Reactor library:

```cpp

net::awaitable<void> run_tcp_client(net::io_context& ioc, std::string_view ep,
                                    std::string_view port)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<beast::tcp_stream> client(ioc, ctx);

    client.withHost(ep.data())
        .withPort(port.data())
        .withMethod(http::verb::get)
        .withTarget("/")
        .withRetries(3)
        .withHeaders({{"User-Agent", "coro-client"}});
    auto [ec, res] = co_await client.execute<Response>();

    LOG_INFO("Error: {} {}", ec.message(), res.body());
}

```
You can find complete code in [Reactor Library Examples](https://github.com/abhilashraju/coroserver/blob/main/examples/web_client/web_client.cpp#L47).

## Example: Web Client to Download Content from Local Unix Domain Socket Server

Here is an example of a simple web client that downloads content from a local Unix domain socket server using the Reactor library:

```cpp
net::awaitable<void> run_unix_client(net::io_context& ioc,
                                     std::string_view name)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<unix_domain::socket> client(ioc, ctx);

    client.withName(name.data())
        .withMethod(http::verb::get)
        .withTarget("/")
        .withRetries(3)
        .withHeaders({{"User-Agent", "coro-client"}});
    auto [ec, res] = co_await client.execute<Response>();

    LOG_INFO("Error: {} {}", ec.message(), res.body());
}

```
You can find complete code in [Reactor Library Examples](https://github.com/abhilashraju/coroserver/blob/main/examples/unix_client/unix_client.cpp#L8).


## Example: Web Crawler to Extract Links from a Web Page

Here is an example of a simple web crawler that extracts links from a web page using the Reactor library:

```cpp
auto getLinks(const std::string& uri, net::io_context& ioc,
                 ssl::context& ctx,
                 int depth) -> net::awaitable<std::vector<std::string>>
{
    std::vector<std::string> links;
    if (depth == 0)
    {
        co_return links;
    }
    WebClient<beast::tcp_stream> client(ioc, ctx);
    client.withUrl(boost::urls::parse_uri(uri).value());
    auto [ec, response] = co_await client.execute<Response>();
    if (!ec)
    {
        for (const auto& link : extract_links(response.body()))
        {
            if (std::string_view(link).starts_with("https"))
            {
                links.push_back(link);
                auto newLinks = co_await getLinks(link, ioc, ctx, depth - 1);
                links.insert(links.end(), newLinks.begin(), newLinks.end());
            }
        }
    }
    co_return links;
}
net::awaitable<void> crawl(net::io_context& ioc, const std::string& ep)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);

    auto links = co_await getLinks(ep, ioc, ctx, 2);
    for (const auto& link : links)
    {
        LOG_INFO("Link: {}", link);
    }
}
```
You can find complete code in [Reactor Library Examples](https://github.com/abhilashraju/coroserver/blob/main/examples/web_crawler/web_crawler.cpp#L71).

## Example: Concurrent Downloads Using `when_all`

Here is an example of performing concurrent downloads using `when_all` with the Reactor library:

```cpp
net::awaitable<void> getAll(net::io_context& ioc, auto... tasks)
{
    auto [res1, res2] = co_await when_all(ioc, std::move(tasks)...);
    LOG_INFO("Respnses: {}\n\n {}", res1.body(), res2.body());
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

    ioc.run();
    return 0;
}
```
## Example: Request and Response Body Conversion

Here is an example of converting request and response bodies using the Reactor library:

```cpp
struct Person
{
    std::string name;
    int age;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Person, name, age)
};
net::awaitable<void> run_unix_client(
    net::io_context& ioc, std::string_view name, std::string_view target)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<unix_domain::socket> client(ioc, ctx);

    client.withName(name.data())
        .withMethod(http::verb::post)
        .withTarget(target.data())
        .withRetries(3)
        .withHeaders({{"User-Agent", "coro-client"}})
        .withBody(Person{"test", 20});
    auto [ec, res] = co_await client.executeAndReturnAs<Person>();

    LOG_INFO("Error: {} {} {}", ec.message(), res.name, res.age);
}
```

You can find complete code in [Reactor Library Examples](https://github.com/abhilashraju/coroserver/blob/main/examples/request_response_conversion/request_response_conversion.cpp#L10).
You can find complete code in [Reactor Library Examples](https://github.com/abhilashraju/coroserver/blob/main/examples/when_all/when_all.cpp#L10).

## Example: Simple HTTP Server

Here is an example of a simple HTTP server using the Reactor library:

```cpp
int main()
{
    try
    {
        boost::asio::io_context io_context;

        auto conn = std::make_shared<sdbusplus::asio::connection>(io_context);
        constexpr std::string_view busName = "xyz.openbmc_project.usermanager";
        constexpr std::string_view objPath = "/xyz/openbmc_project/user";
        
        boost::asio::ssl::context ssl_context(
            boost::asio::ssl::context::sslv23);

        // Load server certificate and private key
        ssl_context.set_options(boost::asio::ssl::context::default_workarounds |
                                boost::asio::ssl::context::no_sslv2 |
                                boost::asio::ssl::context::single_dh_use);
        
        ssl_context.use_certificate_chain_file(
            "/etc/ssl/private/server-cert.pem");
        ssl_context.use_private_key_file("/etc/ssl/private/server-key.pem",
                                         boost::asio::ssl::context::pem);
        
       
        HttpRouter router;
        router.setIoContext(io_context);
        TcpStreamType acceptor(io_context, 8080, ssl_context);
        // std::string socket_path = "/tmp/http_server.sock";
        // UnixStreamType unixAcceptor(io_context, socket_path, ssl_context);
        HttpServer server(io_context, acceptor, router);
        
        using variant = std::variant<bool, std::string>;
        router.add_get_handler(
            "/allmfaproperties",
            [&](auto& req, auto& params) -> net::awaitable<Response> {
                auto [ec, props] = co_await getAllProperties<variant>(
                    *conn, busName.data(), objPath.data(), interface.data());
                if (ec)
                {
                    LOG_ERROR("Error getting all properties: {}", ec.message());
                    co_return make_internal_server_error(
                        "Internal Server Error", req.version());
                }
                nlohmann::json jsonResponse;
                for (auto& prop : props)
                {
                    std::visit(
                        [&](auto val) { jsonResponse[prop.first] = val; },
                        prop.second);
                }

                co_return make_success_response(jsonResponse, http::status::ok,
                                                req.version());
            });

        router.add_post_handler(
            "/createSecretKey",
            [](Request& req,
               const http_function& params) -> net::awaitable<Response> {
                auto userName = params["userName"];
                if (userName.empty())
                {
                    co_return make_bad_request_error("userName is required",
                                                     req.version());
                }
                auto secretKey = createSecretKey(userName);
                co_return make_success_response(secretKey, http::status::ok,
                                                req.version());
            });

       

        io_context.run();
    }
    catch (std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return 1;
    }

    return 0;
}

```
You can find complete code in [Reactor Library Examples](https://github.com/abhilashraju/coroserver/blob/main/examples/server/sample_server.cpp#L100).
