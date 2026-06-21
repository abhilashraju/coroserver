/**
 * @file webclient_stdexec_example.cpp
 * @brief Example demonstrating functional web crawling with stdexec pipelines
 *
 * This example shows how to:
 * 1. Use WebClient with stdexec integration for web crawling
 * 2. Build functional, declarative pipelines for crawling
 * 3. Pattern: just(initial_url) | fetch | extract_links | enqueue | repeat
 * 4. Control crawl depth with stdexec algorithms
 * 5. Track visited URLs to avoid duplicates
 *
 * Requires: stdexec library (https://github.com/NVIDIA/stdexec)
 */

#include "logger.hpp"
#include "stdexec_asio_adapters.hpp"
#include "webclient.hpp"

#include <exec/repeat_until.hpp>
#include <exec/start_detached.hpp>

#include <algorithm>
#include <deque>
#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

using namespace reactor;
using stdexec_adapters::to_sender;

// ============================================================================
// Web Crawler Data Structures
// ============================================================================

/**
 * @brief Represents a URL to be crawled with its depth level
 */
struct CrawlTask
{
    std::string url;
    int depth;
    std::string host;
    std::string path;

    CrawlTask(std::string u, int d) : url(std::move(u)), depth(d)
    {
        parseUrl();
    }

  private:
    void parseUrl()
    {
        // Simple URL parsing: extract host and path
        std::regex url_regex(R"(https?://([^/]+)(/.*)?)", std::regex::icase);
        std::smatch matches;
        if (std::regex_match(url, matches, url_regex))
        {
            host = matches[1].str();
            path = matches.size() > 2 && !matches[2].str().empty()
                       ? matches[2].str()
                       : "/";
        }
        else
        {
            host = url;
            path = "/";
        }
    }
};

/**
 * @brief Shared state for the functional web crawler
 */
struct CrawlerState
{
    std::set<std::string> visited_urls;
    std::deque<CrawlTask> url_queue; // Queue for BFS crawling
    std::vector<std::string> all_extracted_links;
    int max_depth;
    int pages_crawled = 0;

    explicit CrawlerState(int depth) : max_depth(depth) {}

    bool shouldCrawl(const std::string& url, int depth) const
    {
        return depth <= max_depth &&
               visited_urls.find(url) == visited_urls.end();
    }

    void markVisited(const std::string& url)
    {
        visited_urls.insert(url);
    }

    void addExtractedLink(const std::string& url)
    {
        all_extracted_links.push_back(url);
    }

    void enqueueTask(CrawlTask task)
    {
        url_queue.push_back(std::move(task));
    }

    std::optional<CrawlTask> dequeueTask()
    {
        if (url_queue.empty())
            return std::nullopt;
        auto task = std::move(url_queue.front());
        url_queue.pop_front();
        return task;
    }

    bool hasMoreWork() const
    {
        return !url_queue.empty();
    }
};

// ============================================================================
// Link Extraction
// ============================================================================

/**
 * @brief Extract links from HTML content
 */
std::vector<std::string> extractLinks(const std::string& html,
                                      const std::string& base_host)
{
    std::vector<std::string> links;

    // Match href attributes in anchor tags
    std::regex link_regex(R"(href\s*=\s*["']([^"']+)["'])", std::regex::icase);
    std::sregex_iterator iter(html.begin(), html.end(), link_regex);
    std::sregex_iterator end;

    for (; iter != end; ++iter)
    {
        std::string link = (*iter)[1].str();

        // Convert relative URLs to absolute
        if (link.starts_with("/"))
        {
            link = "https://" + base_host + link;
        }
        else if (!link.starts_with("http"))
        {
            continue; // Skip non-HTTP links
        }

        // Only crawl links from the same host
        if (link.find(base_host) != std::string::npos)
        {
            links.push_back(link);
        }
    }

    return links;
}

// ============================================================================
// Functional Stdexec Pipeline Components
// ============================================================================

/**
 * @brief Fetch URL and return response body - pure function
 */
auto fetch_url(WebClient<beast::tcp_stream>& client,
               net::any_io_executor executor)
{
    return [&client, executor](const CrawlTask& task) {
        LOG_INFO("Fetching: {} (depth {})", task.url, task.depth);
        return to_sender(client.withHost(task.host)
                             .withPort("443")
                             .withTarget(task.path)
                             .execute<Response>(),
                         executor) |
               stdexec::then(
                   [task](
                       std::tuple<boost::system::error_code, Response> result) {
                       auto [ec, response] = result;
                       if (ec)
                       {
                           LOG_ERROR("Fetch failed: {}", ec.message());
                           return std::make_pair(task, std::string{});
                       }
                       return std::make_pair(task, response.body());
                   });
    };
}

/**
 * @brief Extract links from HTML - pure function
 */
auto extract_links(std::shared_ptr<CrawlerState> state)
{
    return stdexec::then([state](std::pair<CrawlTask, std::string> data) {
        auto [task, body] = std::move(data);

        if (body.empty())
        {
            return std::make_tuple(state, task, std::vector<std::string>{});
        }

        state->pages_crawled++;
        auto links = extractLinks(body, task.host);

        return std::make_tuple(state, task, links);
    });
}

/**
 * @brief Enqueue new tasks from extracted links - pure function
 */
auto enqueue_links(std::shared_ptr<CrawlerState> state)
{
    return stdexec::then([state](std::tuple<std::shared_ptr<CrawlerState>,
                                            CrawlTask, std::vector<std::string>>
                                     data) {
        auto [state_ptr, task, links] = std::move(data);
        int next_depth = task.depth + 1;

        for (const auto& link : links)
        {
            state_ptr->addExtractedLink(link);

            if (state_ptr->shouldCrawl(link, next_depth))
            {
                state_ptr->markVisited(link);
                state_ptr->enqueueTask(CrawlTask(link, next_depth));
                LOG_INFO("Enqueued: {} (depth {})", link, next_depth);
            }
        }

        return state_ptr;
    });
}

/**
 * @brief Process one URL from the queue - functional pipeline
 * Pattern: dequeue | fetch | extract_links | enqueue
 */
net::awaitable<std::shared_ptr<CrawlerState>> process_one_url(
    WebClient<beast::tcp_stream>& client, std::shared_ptr<CrawlerState> state,
    net::any_io_executor executor)
{
    auto task_opt = state->dequeueTask();
    if (!task_opt)
    {
        co_return state;
    }

    // Functional pipeline: just(task) | fetch | extract_links | enqueue
    auto pipeline = stdexec::just(*task_opt) |
                    stdexec::let_value(fetch_url(client, executor)) |
                    extract_links(state) | enqueue_links(state);

    co_return co_await stdexec_adapters::as_awaitable(std::move(pipeline),
                                                      executor);
}

// ============================================================================
// Functional Web Crawler
// ============================================================================

/**
 * @brief Crawl recursively using stdexec - functional style
 * Pattern: just(initial_state) | process_url | repeat_while(has_work)
 */
net::awaitable<void> crawl_functional(
    net::io_context& io, ssl::context& ssl_ctx, const std::string& start_url,
    int max_depth)
{
    LOG_INFO("=== Starting Functional Web Crawler ===");
    LOG_INFO("Start URL: {}", start_url);
    LOG_INFO("Max depth: {}", max_depth);
    LOG_INFO("Pattern: just(url) | fetch | extract_links | enqueue | repeat");

    auto state = std::make_shared<CrawlerState>(max_depth);
    WebClient<beast::tcp_stream> client(io, ssl_ctx);
    auto executor = net::any_io_executor(io.get_executor());

    // Initialize: just(start_url) | add_to_queue
    CrawlTask start_task(start_url, 0);
    state->markVisited(start_url);
    state->enqueueTask(std::move(start_task));

    // Functional pipeline: process URLs until queue is empty
    // Each iteration uses: just(task) | fetch | extract_links | enqueue
    while (state->hasMoreWork())
    {
        state = co_await process_one_url(client, state, executor);
    }

    // Report results
    LOG_INFO("=== Crawl Complete ===");
    LOG_INFO("Total pages crawled: {}", state->pages_crawled);
    LOG_INFO("Total unique URLs visited: {}", state->visited_urls.size());
    LOG_INFO("Total links extracted: {}", state->all_extracted_links.size());

    LOG_INFO("\n=== All Extracted Links ===");
    for (size_t i = 0; i < state->all_extracted_links.size(); ++i)
    {
        LOG_INFO("[{}] {}", i + 1, state->all_extracted_links[i]);
    }

    co_return;
}

// ============================================================================
// Example: Functional Web Crawler with Configurable Depth
// ============================================================================

net::awaitable<void> example_web_crawler(
    net::io_context& io, ssl::context& ssl_ctx, const std::string& start_url,
    int max_depth)
{
    try
    {
        co_await crawl_functional(io, ssl_ctx, start_url, max_depth);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Crawler failed: {}", e.what());
    }

    co_return;
}

int main(int argc, char* argv[])
{
    try
    {
        reactor::getLogger().setLogLevel(reactor::LogLevel::INFO);

        // Parse command line arguments
        std::string start_url = "https://google.com";
        int max_depth = 2;

        if (argc > 1)
        {
            start_url = argv[1];
        }
        if (argc > 2)
        {
            max_depth = std::stoi(argv[2]);
        }

        LOG_INFO("Web Crawler Configuration:");
        LOG_INFO("  Start URL: {}", start_url);
        LOG_INFO("  Max Depth: {}", max_depth);

        net::io_context io;
        ssl::context ssl_ctx{ssl::context::tlsv12_client};
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(ssl::verify_none);

        // Start the functional web crawler
        net::co_spawn(io,
                      example_web_crawler(io, ssl_ctx, start_url, max_depth),
                      net::detached);

        io.run();

        LOG_INFO("Crawler finished successfully");
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
