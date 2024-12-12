
#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "webclient.hpp"
#include "when_all.hpp"

#include <boost/asio/experimental/awaitable_operators.hpp>

#include <format>
#include <fstream>
#include <ranges>
#include <regex>
#include <vector>
using namespace boost::asio::experimental::awaitable_operators;
auto getQuestionMessage(const nlohmann::json& comments,
                        const std::string& inreplyto)
{
    std::string questionby;
    std::string questionMessage;
    if (inreplyto.empty())
    {
        return std::make_pair(questionby, questionMessage);
    }
    // auto comments = json_data["designs/reactor.md"];
    auto question =
        std::ranges::find_if(comments, [&](const nlohmann::json& comment) {
            return comment["id"] == inreplyto;
        });
    if (question != std::end(comments))
    {
        questionby = (*question)["author"]["username"];
        questionMessage = (*question)["message"];
    }
    return std::make_pair(questionby, questionMessage);
}
net::awaitable<void> getGerritData(net::io_context& ioc, const std::string& api,
                                   const std::string& token,
                                   const std::string& user)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    std::string url = "https://gerrit.openbmc.org/a";
    url.append(api);
    WebClient<beast::tcp_stream> client(ioc, ctx);
    client.withUrl(url);
    client.withMethod(http::verb::get);
    std::map<std::string, std::string> headers;
    headers["Accept"] = "application/json";
    headers["Authorization"] = "Basic " + token;
    client.withHeaders(std::move(headers));
    client.withRetries(3);
    auto [ec, response] = co_await client.execute<Response>();
    if (!ec)
    {
        try
        {
            auto data = response.body();
            auto json_data = nlohmann::json::parse(data.substr(4));
            std::ofstream file("temp.json");
            if (file.is_open())
            {
                file << json_data.dump(4);
                file.close();
            }
            else
            {
                LOG_ERROR("Unable to open file for writing");
            }
            std::vector<nlohmann::json> comments =
                json_data["designs/reactor.md"];
            auto filtered_comments =
                comments |
                std::views::filter([&](const nlohmann::json& comment) {
                    return comment["author"]["username"] == user;
                }) |
                std::views::transform(
                    [&comments](const nlohmann::json& comment) {
                        auto comment_text = comment["message"];
                        auto inreplyto = comment["in_reply_to"];
                        auto date = comment["updated"];
                        auto line = comment["line"];
                        auto [questionby, questionMessage] =
                            getQuestionMessage(comments, inreplyto);
                        nlohmann::json newcomment = {
                            {"Replay by", comment["author"]["username"]},
                            {"Comment", comment_text},
                            {"Question", questionMessage},
                            {"Question by", questionby},
                            {"date", date},
                            {"line", line}};
                        return newcomment;
                    });
            std::vector<nlohmann::json> qanda;
            std::ranges::copy(filtered_comments, std::back_inserter(qanda));

            std::sort(qanda.begin(), qanda.end(),
                      [](const nlohmann::json& a, const nlohmann::json& b) {
                          return a["date"] > b["date"];
                      });

            std::vector<nlohmann::json> newcomments_json;
            std::ranges::copy(qanda | std::views::take(5),
                              std::back_inserter(newcomments_json));
            std::ofstream qanda_file("qanda.json");
            if (qanda_file.is_open())
            {
                qanda_file << nlohmann::json(newcomments_json).dump(4);
                qanda_file.close();
            }
        }
        catch (const nlohmann::json::parse_error& e)
        {
            LOG_ERROR("Custom JSON parsing error: {}", e.what());
            LOG_ERROR("{}", response.body());
        }
    }
    else
    {
        LOG_ERROR("Error: {}", ec.message());
    }
}

int main(int argc, const char* argv[])
{
    try
    {
        net::io_context ioc;
        auto [token, user] = getArgs(parseCommandline(argc, argv), "-a", "-u");
        std::string t{token.value().data(), token.value().length()};
        std::string u{user.value().data(), user.value().length()};
        net::co_spawn(ioc, getGerritData(ioc, "/changes/67077/comments", t, u),
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
