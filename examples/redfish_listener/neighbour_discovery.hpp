#pragma once
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct ArpEntry
{
    std::string ip;
    std::string hw_type;
    std::string flags;
    std::string mac;
    std::string mask;
    std::string device;
};

std::vector<ArpEntry> getArpTable()
{
    std::vector<ArpEntry> entries;
    std::ifstream arp("/proc/net/arp");
    std::string line;
    std::getline(arp, line); // skip header
    while (std::getline(arp, line))
    {
        std::istringstream iss(line);
        ArpEntry entry;
        iss >> entry.ip >> entry.hw_type >> entry.flags >> entry.mac >>
            entry.mask >> entry.device;
        if (entry.mac != "00:00:00:00:00:00")
            entries.push_back(entry);
    }

    return entries;
}
