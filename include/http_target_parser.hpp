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
        std::string value; // owns decoded content
        parameter(std::string_view n, std::string v) :
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

// Decode a URL-encoded string: %XX hex escapes and '+' → space.
inline std::string url_decode(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '+')
        {
            out += ' ';
        }
        else if (s[i] == '%' && i + 2 < s.size())
        {
            auto hexNibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            int hi = hexNibble(s[i + 1]);
            int lo = hexNibble(s[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            }
            else
            {
                out += s[i]; // leave malformed % literal
            }
        }
        else
        {
            out += s[i];
        }
    }
    return out;
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
            if (pairs.size() < 2 || pairs[0].empty())
                continue; // skip empty tokens (e.g. trailing '&') or bare keys
            parampairs.emplace_back(pairs[0], url_decode(pairs[1]));
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
                               s1.substr(1, s1.length() - 2),
                               std::string(s2)};
                       }

                       return http_function::parameter{s1, std::string{}};
                   });
}
}