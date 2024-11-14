#pragma once
#include <algorithm>
#include <vector>

template <typename Key, typename Value>
struct flat_map
{
    using value_type = std::pair<Key, Value>;
    std::vector<value_type> map;
    auto begin()
    {
        return map.begin();
    }
    auto end()
    {
        return map.end();
    }
    auto find(const Key& key)
    {
        return std::find_if(begin(), end(), [&](auto& v) {
            return v.first == key;
        });
    }
    auto contains(const Key& key) const
    {
        return find(key) != end();
    }
    Value& operator[](const Key& key)
    {
        auto iter = find(key);
        if (iter != end())
        {
            return iter->second;
        }
        return put(key, Value{});
    }
    Value& put(const Key& key, Value value)
    {
        map.emplace_back(key, std::move(value));
        return map.back().second;
    }
    void remove(const Key& key)
    {
        auto iter = find(key);
        if (iter != end())
        {
            map.erase(iter);
        }
    }
};
