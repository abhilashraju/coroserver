#pragma once

#include "utilities.hpp"

#include <numeric>
#include <string>
#include <vector>
namespace NSNAME
{
struct http_function
{
    struct parameter
    {
        std::string_view name;
        std::string_view value;
        parameter(std::string_view n, std::string_view v) :
            name(std::move(n)), value(std::move(v))
        {}
    };
    using parameters = std::vector<parameter>;
    std::string _name;
    parameters _params;
    tcp::endpoint rep;
    const auto& name() const
    {
        return _name;
    }
    const auto& params() const
    {
        return _params;
    }
    auto& params()
    {
        return _params;
    }
    const tcp::endpoint& endpoint() const
    {
        return rep;
    }
    void setEndpoint(tcp::endpoint&& ep)
    {
        rep = std::move(ep);
    }
    std::string operator[](const std::string& name) const
    {
        if (auto iter = std::find_if(begin(_params), end(_params),
                                     [&](auto& p) { return p.name == name; });
            iter != end(_params) && !iter->value.empty())
        {
            return {iter->value.data(), iter->value.size()};
        }
        return std::string();
    }
};
std::string to_string(std::string_view vw)
{
    return std::string(vw.data(), vw.length());
}

inline http_function parse_function(std::string_view target)
{
    auto index = target.find_last_of("/");
    if (index != std::string::npos)
    {
        auto nameandparams = split(target, '?');
        auto func = nameandparams[0];
        if (nameandparams.size() == 1)
        {
            return http_function{to_string(func), http_function::parameters{}};
        }
        auto paramstring = nameandparams[1];
        auto params = split(paramstring, '&');
        http_function::parameters parampairs;
        for (auto& p : params)
        {
            auto pairs = split(p, '=');
            parampairs.emplace_back(pairs[0], pairs[1]);
        }
        return http_function{to_string(func), std::move(parampairs)};
    }
    return http_function{to_string(target), http_function::parameters{}};
}

void extract_params_from_path(http_function& func,
                              const std::string& handlerfuncname,
                              const std::string& pathfuncname)
{
    auto segs1 = split(handlerfuncname, '/', 1);
    auto segs2 = split(pathfuncname, '/', 1);
    if (segs1.size() != segs2.size())
        return;
    auto& params = func.params();
    std::transform(begin(segs1), end(segs1), begin(segs2),
                   std::back_inserter(params), [](auto& s1, auto& s2) {
                       if (s1[0] == '{' && s1.back() == '}')
                       {
                           return http_function::parameter{
                               s1.substr(1, s1.length() - 2), s2};
                       }

                       return http_function::parameter{s1, ""};
                   });
}
}