#pragma once

#include "event_broker_plugin.hpp"
#include "logger.hpp"

#include <dlfcn.h>

#include <filesystem>
#include <ranges>
#include <vector>
using PluginFunc = std::shared_ptr<PluginIface> (*)();
struct SharedLibrary
{
    void* handle;
    bool persist{false};

    SharedLibrary(const std::filesystem::path& path)
    {
        handle = dlopen(path.c_str(), RTLD_LAZY);
        if (!handle)
        {
            LOG_ERROR("Cannot open library: {}", dlerror());
            return;
        }
    }
    SharedLibrary(SharedLibrary&& other) :
        handle(other.handle), persist(other.persist)
    {
        other.handle = nullptr;
    }
    void setPersist()
    {
        persist = true;
    }
    ~SharedLibrary()
    {
        if (handle && !persist)
        {
            dlclose(handle);
        }
    }

    std::optional<PluginFunc> loadSymbol(const std::string& symbolName)
    {
        dlerror(); // Reset errors
        auto symbol =
            reinterpret_cast<PluginFunc>(dlsym(handle, symbolName.c_str()));
        const char* dlsym_error = dlerror();
        if (dlsym_error)
        {
            LOG_ERROR("Cannot load symbol {} : {} ", symbolName, dlsym_error);
            return std::nullopt;
        }
        return symbol;
    }
};

struct PluginLoader
{
    SharedLibrary lib;
    PluginLoader(const std::filesystem::path& path) : lib(path) {}
    void setPersist()
    {
        lib.setPersist();
    }
    std::shared_ptr<PluginIface> loadPlugin(std::string_view symbolName)
    {
        auto expected_func = lib.loadSymbol(symbolName.data());
        if (expected_func)
        {
            PluginFunc func = expected_func.value();
            auto plugin = func();
            return plugin;
        }
        return {};
    }
};
struct PluginDb
{
    std::vector<PluginLoader> loaders;
    std::vector<std::shared_ptr<PluginIface>> plugins;

    PluginDb(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
        {
            LOG_ERROR("Plugin folder {} does not exist", path.string());
            return;
        }
        for (const auto& entry : std::filesystem::directory_iterator(path))
        {
            if (entry.path().extension() == ".dylib" ||
                entry.path().extension() == ".so")
            {
                LOG_INFO("Loading plugin {}", entry.path().string());
                PluginLoader loader(entry.path());
                loader.setPersist();
                auto plugin = loader.loadPlugin("create_object");
                if (plugin)
                {
                    loaders.push_back(std::move(loader));
                    plugins.push_back(plugin);
                    LOG_INFO("Loaded plugin {}", entry.path().string());
                }
            }
        }
    }
    template <typename T>
    std::vector<std::shared_ptr<T>> getInterFaces()
    {
        LOG_INFO("Begin Looking for interface {}", T::iid());
        std::vector<std::shared_ptr<T>> ret;
        for (auto& plugin : plugins)
        {
            LOG_INFO("Looking for interface {}", T::iid());
            auto iface = PluginIface::getInterface<T>(plugin.get());
            if (iface)
            {
                LOG_INFO("Found interface {}", T::iid());
                ret.push_back(iface);
            }
        }

        return ret;
    }
};
