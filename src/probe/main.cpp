// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
//
// vloomdaw-plugin-probe: loads ONE plugin module and prints what its factory
// says about the plugins inside (name, vendor, version, categories), then
// exits. VloomDAW runs it as a separate process for every plugin it scans,
// so a plugin that crashes or hangs while loading cannot take the app down.
//
// It never creates a plugin instance and never processes audio. It is not a
// security sandbox: the module's own code (static initialisers, entry
// functions) runs with the user's rights.
//
//   vloomdaw-plugin-probe --vst3 <path.vst3>
//   vloomdaw-plugin-probe --clap <path.clap>
//
// Output (UTF-8, one record per line, tab-separated; other lines a plugin
// prints itself are to be ignored):
//   @@vloom	probe	1
//   @@vloom	plugin	<name>	<vendor>	<version>	<tags, '|'-separated>
//   @@vloom	error	<message>              (exit code 1)

#include "probe/ClapAbi.hpp"
#include "probe/Vst3Abi.hpp"

#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// After windows.h:
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;
using namespace vloomdaw;

namespace {

/// One field of an output line: no tabs, line breaks or other control
/// characters, at most 200 bytes.
std::string field(const char* text, std::size_t maxBytes = 200)
{
    std::string out;
    if (text == nullptr)
        return out;
    for (const char* p = text; *p != '\0' && out.size() < maxBytes; ++p)
        out.push_back(static_cast<unsigned char>(*p) < 0x20 ? ' ' : *p);
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

/// Same for a fixed-size char array that may lack its terminating zero.
template <std::size_t N> std::string field(const char (&text)[N])
{
    return field(std::string(text, strnlen(text, N)).c_str());
}

void emit(std::string_view kind, const std::vector<std::string>& fields)
{
    std::string line = "@@vloom\t";
    line += kind;
    for (const std::string& f : fields) {
        line += '\t';
        line += f;
    }
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

int fail(const std::string& message)
{
    emit("error", {field(message.c_str())});
    return 1;
}

/// A loaded plugin module (DLL, shared object or macOS bundle).
class Module {
public:
    Module() = default;
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    ~Module() { close(); }

    bool open(const fs::path& path)
    {
#if defined(_WIN32)
        // Altered search path: the plugin's own folder resolves its DLLs.
        handle_ = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        return handle_ != nullptr;
#elif defined(__APPLE__)
        const std::string text = path.string();
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            nullptr, reinterpret_cast<const UInt8*>(text.c_str()), static_cast<CFIndex>(text.size()), true);
        if (url == nullptr)
            return false;
        bundle_ = CFBundleCreate(nullptr, url);
        CFRelease(url);
        if (bundle_ == nullptr || !CFBundleLoadExecutable(bundle_)) {
            close();
            return false;
        }
        return true;
#else
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        return handle_ != nullptr;
#endif
    }

    [[nodiscard]] void* symbol(const char* name) const
    {
#if defined(_WIN32)
        if (handle_ == nullptr)
            return nullptr;
        const FARPROC address = GetProcAddress(handle_, name);
        return address != nullptr ? std::bit_cast<void*>(address) : nullptr;
#elif defined(__APPLE__)
        if (bundle_ == nullptr)
            return nullptr;
        CFStringRef key = CFStringCreateWithCString(nullptr, name, kCFStringEncodingUTF8);
        void* p = CFBundleGetFunctionPointerForName(bundle_, key);
        if (p == nullptr)
            p = CFBundleGetDataPointerForName(bundle_, key);
        CFRelease(key);
        return p;
#else
        return handle_ != nullptr ? dlsym(handle_, name) : nullptr;
#endif
    }

#if defined(__APPLE__)
    [[nodiscard]] CFBundleRef bundle() const { return bundle_; }
#else
    [[nodiscard]] void* handle() const { return reinterpret_cast<void*>(handle_); }
#endif

    void close()
    {
#if defined(_WIN32)
        if (handle_ != nullptr)
            FreeLibrary(handle_);
        handle_ = nullptr;
#elif defined(__APPLE__)
        if (bundle_ != nullptr)
            CFRelease(bundle_);
        bundle_ = nullptr;
#else
        if (handle_ != nullptr)
            dlclose(handle_);
        handle_ = nullptr;
#endif
    }

private:
#if defined(_WIN32)
    HMODULE handle_ = nullptr;
#elif defined(__APPLE__)
    CFBundleRef bundle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};

/// The file to load for a plugin: the bundle's binary on Windows / Linux, the
/// bundle itself on macOS, or the plugin file when it is not a bundle.
fs::path moduleFile(const fs::path& plugin)
{
    std::error_code ec;
    if (!fs::is_directory(plugin, ec))
        return plugin;
#if defined(_WIN32)
    return plugin / "Contents" / "x86_64-win" / plugin.filename();
#elif defined(__APPLE__)
    return plugin;
#else
    fs::path binary = plugin / "Contents" / "x86_64-linux" / plugin.filename();
    binary.replace_extension(".so");
    return binary;
#endif
}

/// An exported function. bit_cast: converting an object pointer to a
/// function pointer directly is only conditionally supported in C++.
template <typename T> T proc(const Module& module, const char* name)
{
    static_assert(sizeof(T) == sizeof(void*));
    void* address = module.symbol(name);
    return address != nullptr ? std::bit_cast<T>(address) : nullptr;
}

int probeVst3(const fs::path& plugin)
{
    Module module;
    if (!module.open(moduleFile(plugin)))
        return fail("could not load the module");

    // The module entry each OS requires before GetPluginFactory.
    bool entered = true;
#if defined(_WIN32)
    if (auto init = proc<bool(VLOOM_VST3_API*)()>(module, "InitDll"))
        entered = init();
#elif defined(__APPLE__)
    auto entry = proc<bool (*)(CFBundleRef)>(module, "bundleEntry");
    if (entry == nullptr)
        entry = proc<bool (*)(CFBundleRef)>(module, "BundleEntry");
    if (entry != nullptr)
        entered = entry(module.bundle());
#else
    if (auto entry = proc<bool (*)(void*)>(module, "ModuleEntry"))
        entered = entry(module.handle());
#endif
    if (!entered)
        return fail("the module refused to initialise");

    auto getFactory = proc<vst3::GetFactoryProc>(module, "GetPluginFactory");
    if (getFactory == nullptr)
        return fail("not a VST3 module (no GetPluginFactory)");
    vst3::IPluginFactory* factory = getFactory();
    if (factory == nullptr)
        return fail("the module returned no factory");

    vst3::PFactoryInfo factoryInfo{};
    factory->getFactoryInfo(&factoryInfo);
    const std::string factoryVendor = field(factoryInfo.vendor);

    void* object = nullptr;
    vst3::IPluginFactory2* factory2 = nullptr;
    if (factory->queryInterface(vst3::kIPluginFactory2.bytes, &object) == vst3::kResultOk
        && object != nullptr)
        factory2 = static_cast<vst3::IPluginFactory2*>(object);

    emit("probe", {"1"});
    const std::int32_t count = factory->countClasses();
    for (std::int32_t i = 0; i < count; ++i) {
        if (factory2 != nullptr) {
            vst3::PClassInfo2 info{};
            if (factory2->getClassInfo2(i, &info) != vst3::kResultOk
                || std::strncmp(info.category, vst3::kAudioEffectClass, sizeof(info.category)) != 0)
                continue;
            const std::string vendor = field(info.vendor);
            emit("plugin",
                {field(info.name), vendor.empty() ? factoryVendor : vendor, field(info.version),
                    field(info.subCategories)});
        } else {
            vst3::PClassInfo info{};
            if (factory->getClassInfo(i, &info) != vst3::kResultOk
                || std::strncmp(info.category, vst3::kAudioEffectClass, sizeof(info.category)) != 0)
                continue;
            emit("plugin", {field(info.name), factoryVendor, std::string(), std::string()});
        }
    }
    if (factory2 != nullptr)
        factory2->release();
    factory->release();

#if defined(_WIN32)
    if (auto exit = proc<bool(VLOOM_VST3_API*)()>(module, "ExitDll"))
        exit();
#elif defined(__APPLE__)
    auto leave = proc<bool (*)()>(module, "bundleExit");
    if (leave == nullptr)
        leave = proc<bool (*)()>(module, "BundleExit");
    if (leave != nullptr)
        leave();
#else
    if (auto leave = proc<bool (*)()>(module, "ModuleExit"))
        leave();
#endif
    return 0;
}

int probeClap(const fs::path& plugin)
{
    Module module;
    if (!module.open(moduleFile(plugin)))
        return fail("could not load the module");
    const auto* entry = static_cast<const clap::PluginEntry*>(module.symbol("clap_entry"));
    if (entry == nullptr || entry->init == nullptr || entry->getFactory == nullptr)
        return fail("not a CLAP module (no clap_entry)");
    if (entry->clapVersion.major < 1)
        return fail("unsupported CLAP version");

    const std::string pluginPath = plugin.string();
    if (!entry->init(pluginPath.c_str()))
        return fail("the module refused to initialise");
    const auto* factory = static_cast<const clap::PluginFactory*>(entry->getFactory(clap::kPluginFactoryId));
    if (factory == nullptr || factory->getPluginCount == nullptr || factory->getPluginDescriptor == nullptr) {
        if (entry->deinit != nullptr)
            entry->deinit();
        return fail("the module has no plugin factory");
    }

    emit("probe", {"1"});
    const std::uint32_t count = factory->getPluginCount(factory);
    for (std::uint32_t i = 0; i < count && i < 1024; ++i) {
        const clap::PluginDescriptor* d = factory->getPluginDescriptor(factory, i);
        if (d == nullptr)
            continue;
        std::string tags;
        for (const char* const* f = d->features; f != nullptr && *f != nullptr && tags.size() < 400; ++f) {
            if (!tags.empty())
                tags += '|';
            tags += field(*f, 64);
        }
        emit("plugin", {field(d->name), field(d->vendor), field(d->version), tags});
    }
    if (entry->deinit != nullptr)
        entry->deinit();
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    // No crash or "missing DLL" dialogs: the app reads the exit status.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdout), _O_BINARY); // plain \n line ends
#endif
    if (argc != 3) {
        std::fputs("usage: vloomdaw-plugin-probe --vst3|--clap <path>\n", stderr);
        return 2;
    }
    const std::string_view kind = argv[1];
#if defined(_WIN32)
    // argv is in the ANSI code page: take the path from the wide command line.
    int wideCount = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    const fs::path path = wide != nullptr && wideCount == 3 ? fs::path(wide[2]) : fs::path(argv[2]);
    if (wide != nullptr)
        LocalFree(wide);
#else
    const fs::path path = argv[2];
#endif
    if (kind == "--vst3")
        return probeVst3(path);
    if (kind == "--clap")
        return probeClap(path);
    std::fputs("usage: vloomdaw-plugin-probe --vst3|--clap <path>\n", stderr);
    return 2;
}
