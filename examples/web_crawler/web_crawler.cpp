
#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "webclient.hpp"

#include <boost/asio/experimental/awaitable_operators.hpp>

#include <ranges>
#include <regex>
#include <vector>
using namespace boost::asio::experimental::awaitable_operators;
std::vector<std::string> extract_links(const std::string& html)
{
    std::vector<std::string> links;
    std::smatch match;
    std::string pattern = R"(<a\s+(?:[^>]*?\s+)?href="([^"]*))";
    std::regex href_regex(pattern);

    std::string::const_iterator search_start(html.cbegin());
    while (std::regex_search(search_start, html.cend(), match, href_regex))
    {
        links.push_back(match[1].str());
        search_start = match.suffix().first;
    }

    return links;
}
auto getLinks(const std::string& uri, net::io_context& ioc, ssl::context& ctx,
              int depth) -> net::awaitable<std::vector<std::string>>
{
    std::vector<std::string> links;
    if (depth == 0)
    {
        co_return links;
    }
    WebClient<tcp::socket> client(ioc, ctx);
    client.withUrl(boost::urls::parse_uri(uri).value())
        .then([&links](auto&& response)
                  -> AwaitableResult<boost::system::error_code> {
            for (const auto& link : extract_links(response.body()))
            {
                if (std::string_view(link).starts_with("https"))
                {
                    links.push_back(link);
                }
            }
            co_return boost::system::error_code{};
        })
        .orElse([](auto ec) -> AwaitableResult<boost::system::error_code> {
            LOG_ERROR("Error: {}", ec.message());
            co_return ec;
        });
    auto [ec] = co_await client.execute();
    std::vector<std::string> retLinks;
    if (!ec)
    {
        std::copy(links.begin(), links.end(), std::back_inserter(retLinks));
        for (const auto& link : links)
        {
            auto newLinks = co_await getLinks(link, ioc, ctx, depth - 1);
            retLinks.insert(retLinks.end(), newLinks.begin(), newLinks.end());
        }
    }

    co_return retLinks;
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

int main(int argc, const char* argv[])
{
    try
    {
        auto [url] = getArgs(parseCommandline(argc, argv), "--url,-u");
        if (!url)
        {
            LOG_ERROR("Usage: web_crawler --url <url>");
            return EXIT_FAILURE;
        }
        std::string strurl{url.value().data(), url.value().length()};
        net::io_context ioc;

        net::co_spawn(ioc, crawl(ioc, strurl), net::detached);
        ioc.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
