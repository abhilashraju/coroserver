#pragma once
#include "http_server.hpp"
#include "logger.hpp"
#include "redfish_client.hpp"
#include "webclient.hpp"

#include <boost/asio/spawn.hpp>
#include <nlohmann/json.hpp>
using namespace reactor;
struct EventListener
{
    HttpRouter& router;
    net::io_context& io_context;
    EventListener(HttpRouter& r, net::io_context& io_context,
                  const std::string& myIP, const std::string& port) :
        router(r), io_context(io_context)
    {
        // Register the event subscription handler
        router.add_post_handler(
            "/suscribetoevents",
            std::bind_front(&EventListener::subscribeToEvents, this, myIP,
                            port));
        router.add_post_handler(
            "/events", std::bind_front(&EventListener::handleEvents, this));
        router.add_get_handler(
            "/fetchips", std::bind_front(&EventListener::fetchRemoteIps, this));
    }
    net::awaitable<Response> handleEvents(Request& req,
                                          const http_function& params)
    {
        auto json = nlohmann::json::parse(req.body(), nullptr, false);
        if (json.is_discarded())
        {
            LOG_ERROR("Invalid JSON body: {}", req.body());
            co_return make_bad_request_error("Invalid JSON body",
                                             req.version());
        }
        // Filter "Events" array based on "OriginOfCondition" containing
        // any filter string
        std::vector<nlohmann::json> events = json["Events"];
        LOG_DEBUG("{}", json.dump(4));
        std::vector<std::string> selectors{
            "/redfish/v1/Managers/bmc/DedicatedNetworkPorts/eth0",
            "/redfish/v1/Managers/bmc/DedicatedNetworkPorts/eth1"};
        auto filtered =
            events | std::views::filter([&](const nlohmann::json& event) {
                if (event.contains("OriginOfCondition"))
                {
                    auto dataid = event["OriginOfCondition"]["@odata.id"]
                                      .get<std::string>();
                    return std::ranges::any_of(
                        selectors, [&](const std::string& selector) {
                            return dataid == selector;
                        });
                }
                return false;
            });

        // Print filtered events
        for (const auto& event : filtered)
        {
            LOG_INFO("Filtered Event: {}", event.dump(4));
        }
        nlohmann::json jsonResponse;
        jsonResponse["recieved"] = true;
        co_return make_success_response(jsonResponse, http::status::ok,
                                        req.version());
    }
    net::awaitable<Response> subscribeToEvents(
        const std::string& myIP, const std::string& port, Request& req,
        const http_function& params)
    {
        auto json = nlohmann::json::parse(req.body(), nullptr, false);
        if (json.is_discarded())
        {
            LOG_DEBUG("Invalid JSON body: {}", req.body());
            co_return make_bad_request_error("Invalid JSON body",
                                             req.version());
        }
        ssl::context ctx(ssl::context::tlsv12_client);

        // Load the root certificates
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_none);

        std::string dest = json["destination"];
        std::string user = json["username"];
        std::string password = json["password"];
        RedfishClient client(io_context, ctx);
        client.withHost(dest)
            .withPort("443")
            .withProtocol("https")
            .withUserName(user)
            .withPassword(password);
        nlohmann::json subscription;
        subscription["Destination"] =
            std::format("https://{}:{}/events", myIP, port);
        subscription["Protocol"] = "Redfish";
        subscription["DeliveryRetryPolicy"] = "RetryForever";
        RedfishClient::Request redReq;
        redReq.withMethod(http::verb::post)
            .withTarget("/redfish/v1/EventService/Subscriptions")
            .withBody(subscription.dump())
            .withHeaders({{"Content-Type", "application/json"}});
        auto [ec, res] = co_await client.execute(redReq);
        if (ec)
        {
            LOG_ERROR("Error executing request: {}", ec.message());
            co_return make_internal_server_error(
                "Failed to subscribe to events", req.version());
        }
        nlohmann::json jsonResponse;
        jsonResponse["status"] = true;
        co_return make_success_response(jsonResponse, http::status::ok,
                                        req.version());
    }
    net::awaitable<Response> fetchRemoteIps(Request& req,
                                            const http_function& params)
    {
        auto json = nlohmann::json::parse(req.body(), nullptr, false);
        if (json.is_discarded())
        {
            LOG_DEBUG("Invalid JSON body: {}", req.body());
            co_return make_bad_request_error("Invalid JSON body",
                                             req.version());
        }
        ssl::context ctx(ssl::context::tlsv12_client);

        // Load the root certificates
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_none);

        std::string dest = json["destination"];
        std::string user = json["username"];
        std::string password = json["password"];
        RedfishClient client(io_context, ctx);
        client.withHost(dest)
            .withPort("443")
            .withProtocol("https")
            .withUserName(user)
            .withPassword(password);

        auto ifaces = co_await fetchIfaces(client);
        std::vector<std::string> ips;
        for (auto& iface : ifaces)
        {
            LOG_DEBUG("Fetching IPs for interface: {}", iface);
            auto ifaceIps = co_await fetchIps(client, iface);
            ips.insert(ips.end(), ifaceIps.begin(), ifaceIps.end());
        }
        if (ips.empty())
        {
            LOG_ERROR("No IPs found for interfaces");
            co_return make_internal_server_error("No IPs found for interfaces",
                                                 req.version());
        }
        nlohmann::json jsonResponse;
        jsonResponse["ips"] = ips;
        co_return make_success_response(jsonResponse, http::status::ok,
                                        req.version());
    }
    net::awaitable<std::vector<std::string>> fetchIfaces(RedfishClient& client)
    {
        RedfishClient::Request redReq;
        redReq.withMethod(http::verb::get)
            .withTarget("/redfish/v1/Managers/bmc/EthernetInterfaces")
            .withHeaders({{"Content-Type", "application/json"}});
        auto [ec, res] = co_await client.execute(redReq);
        if (ec)
        {
            LOG_ERROR("Error executing request: {}", ec.message());
            co_return std::vector<std::string>{};
        }
        if (res.result() != http::status::ok)
        {
            LOG_ERROR("Failed to fetch interfaces: {}",
                      http_error_to_string.at(res.result()));
            co_return std::vector<std::string>{};
        }
        nlohmann::json jsonResponse = nlohmann::json::parse(res.body());
        std::vector<std::string> interfaces;
        for (const auto& iface : jsonResponse["Members"])
        {
            if (iface.contains("@odata.id"))
            {
                interfaces.push_back(iface["@odata.id"].get<std::string>());
            }
        }
        co_return interfaces;
    }
    net::awaitable<std::vector<std::string>> fetchIps(RedfishClient& client,
                                                      const std::string& iface)
    {
        RedfishClient::Request redReq;
        redReq.withMethod(http::verb::get)
            .withTarget(iface)
            .withHeaders({{"Content-Type", "application/json"}});
        auto [ec, res] = co_await client.execute(redReq);
        if (ec)
        {
            LOG_ERROR("Error executing request: {}", ec.message());
            co_return std::vector<std::string>{};
        }
        if (res.result() != http::status::ok)
        {
            LOG_ERROR("Failed to fetch interface details: {}",
                      http_error_to_string.at(res.result()));
            co_return std::vector<std::string>{};
        }
        nlohmann::json jsonResponse = nlohmann::json::parse(res.body());

        std::vector<std::string> ips;
        for (const auto& ip : jsonResponse["IPv4Addresses"])
        {
            if (ip.contains("Address"))
            {
                ips.push_back(ip["Address"].get<std::string>());
            }
        }
        for (const auto& ip : jsonResponse["IPv6Addresses"])
        {
            if (ip.contains("Address"))
            {
                ips.push_back(ip["Address"].get<std::string>());
            }
        }
        co_return ips;
    }
};
