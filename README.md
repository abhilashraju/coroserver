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

#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "webclient.hpp"
#include <ranges>
#include <regex>
#include <vector>

net::awaitable<void> run_tcp_client(net::io_context& ioc, std::string_view ep,
                                    std::string_view port)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<beast::tcp_stream> client(ioc, ctx);

    client.withEndPoint(ep.data())      //remote ip/domain name
        .withPort(port.data())          //remote port
        .withMethod(http::verb::get)    //method
        .withTarget("/")                //target
        .withRetries(3)                 //retries on failure
        .withHeaders({{"User-Agent", "coro-client"}}); //headers if any
    auto [ec, res] = co_await client.execute<Response>(); //execute the request

    LOG_INFO("Error: {} {}", ec.message(), res.body());//print out the respose body
}

int main(int argc, const char* argv[])
{
    try
    {
        auto [domain, port] =
            getArgs(parseCommandline(argc, argv), "--domain,-d", "--port,-p");

        if (!domain.has_value() && !name.has_value())
        {
            LOG_ERROR("Domain or name is required");
            LOG_ERROR(
                "Usage: client --domain|-d <domain end point>");

            return EXIT_FAILURE;
        }
        net::io_context ioc;

        if (domain.has_value())
        {
            boost::urls::url url =
                boost::urls::parse_uri(domain.value().data()).value();
            net::co_spawn(ioc,
                          run_tcp_client(ioc, url.host(), port.value_or("443")),
                          net::detached);
        }
        if (name.has_value())
        {
            net::co_spawn(
                ioc,
                run_unix_client(ioc, name.value_or("/tmp/http_server.sock"),
                                port.value_or("")),
                net::detached);
        }
        ioc.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

```
## Example: Web Client to Download Content from Local Unix Domain Socket Server

Here is an example of a simple web client that downloads content from a local Unix domain socket server using the Reactor library:

```cpp
#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "webclient.hpp"

#include <ranges>
#include <regex>
#include <vector>

net::awaitable<void> run_unix_client(net::io_context& ioc, std::string_view name)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<unix_domain::socket> client(ioc, ctx);

    client.withName(name)
        .withMethod(http::verb::get)
        .withTarget("/")
        .withRetries(3)
        .withHeaders({{"User-Agent", "coro-client"}});
    auto [ec, res] = co_await client.execute<Response>();

    LOG_INFO("Error: {} {}", ec.message(), res.body());
}

int main(int argc, const char* argv[])
{
    try
    {
        auto [name] =
            getArgs(parseCommandline(argc, argv),
                    "--name,-n");

        if (!name.has_value())
        {
            
            LOG_ERROR(
                "Usage: client --name|-n <unix socket path name>");

            return EXIT_FAILURE;
        }
        net::io_context ioc;
        net::co_spawn(
            ioc,
            run_unix_client(ioc, name.value_or("/tmp/http_server.sock")),
            net::detached);

        ioc.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

```

