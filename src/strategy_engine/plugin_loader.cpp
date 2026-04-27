#include "plugin_loader.hpp"

#include <dlfcn.h>
#include <strategy_registry.hpp>

bool LoadPlugin(const std::string& path, std::string& err) {
    err.clear();
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        err = "dlopen failed for '" + path + "': " + dlerror();
        return false;
    }

    using AbiFn = int (*)();
    auto abi_fn = reinterpret_cast<AbiFn>(dlsym(handle, "sdk_plugin_abi_version"));
    if (!abi_fn) {
        err = "plugin '" + path +
              "' is missing sdk_plugin_abi_version symbol "
              "(use DECLARE_PLUGIN_ABI() macro in your plugin)";
        return false;
    }

    int plugin_abi = abi_fn();
    if (plugin_abi != SDK_ABI_VERSION) {
        err = "plugin '" + path + "' ABI version mismatch: plugin=" + std::to_string(plugin_abi) +
              " host=" + std::to_string(SDK_ABI_VERSION);
        return false;
    }

    return true;
}
