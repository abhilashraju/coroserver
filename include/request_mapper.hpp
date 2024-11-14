
#include "beastdefs.hpp"

#include <string>

struct request_mapper
{
    std::string path;
    http::verb method;

    friend bool operator==(const request_mapper& first,
                           const request_mapper& second)
    {
        if (first.method != second.method)
            return false;
        auto segs1 = split(first.path, '/');
        auto segs2 = split(second.path, '/');
        if (segs1.size() != segs2.size())
            return false;
        std::vector<bool> matches;
        std::transform(begin(segs1), end(segs1), begin(segs2),
                       std::back_inserter(matches), [](auto& s1, auto& s2) {
                           if (s1 == s2)
                               return true;
                           if (s1[0] == '{' && s1.back() == '}')
                               return true;
                           return false;
                       });
        return std::all_of(begin(matches), end(matches),
                           [](auto b) { return b; });
    }
    friend bool operator!=(const request_mapper& first,
                           const request_mapper& second)
    {
        return !(first == second);
    }
    friend bool operator<(const request_mapper& first,
                          const request_mapper& second)
    {
        return first.path < second.path;
    }
};
