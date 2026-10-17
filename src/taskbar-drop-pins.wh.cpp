// ==WindhawkMod==
// @id              taskbar-drop-pins
// @name            Taskbar Drop Pins
// @description     Pin files, folders and shortcuts by dropping them among native Windows 11 taskbar icons.
// @version         0.1.0
// @author          Portakal
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lshell32 -lshlwapi -lpropsys -luuid -lcomctl32
// ==/WindhawkMod==
// ==WindhawkModReadme==
/*
# Taskbar Drop Pins

Drag a file, folder or .lnk shortcut to the Windows 11 taskbar to pin it.
Windows handles insertion, reordering, launching and unpinning normally.
Hold Shift BEFORE entering the taskbar to retain ordinary file dragging.

Original files and folders stay in place. Non-application .lnk shortcuts are
copied to permanent mod storage, preserving their Shell launch properties.
The embedded launcher and cached icons must remain available for pinned items.
Disabling this mod stops drag conversion; previously pinned shortcuts still open.
Only local filesystem objects are supported; virtual attachments are passed through.
*/
// ==/WindhawkModReadme==

#include <windhawk_api.h>
#include <windhawk_utils.h>
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <olectl.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "launcher-bytes.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
static fs::path g_storage;
static fs::path g_launcher;
static std::atomic<bool> g_stopping{false};
static std::mutex g_hooksMutex;
static std::vector<HWND> g_subclassed;
static UINT g_discoverMessage;

static void Check(HRESULT result) {
    if (FAILED(result)) throw result;
}

static void WriteBytes(const fs::path& path, const void* bytes, DWORD size) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) Check(HRESULT_FROM_WIN32(GetLastError()));
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes, size, &written, nullptr);
    const DWORD error = ok ? ERROR_WRITE_FAULT : GetLastError();
    CloseHandle(file);
    if (!ok || written != size) Check(HRESULT_FROM_WIN32(error));
}

static bool IsTaskbar(HWND window) {
    if (!window) return false;
    HWND root = GetAncestor(window, GA_ROOT);
    DWORD process = 0;
    GetWindowThreadProcessId(root, &process);
    if (process != GetCurrentProcessId()) return false;
    wchar_t name[80]{};
    GetClassNameW(root, name, ARRAYSIZE(name));
    return wcscmp(name, L"Shell_TrayWnd") == 0 ||
           wcscmp(name, L"Shell_SecondaryTrayWnd") == 0;
}

static std::vector<fs::path> ReadPaths(IDataObject* object) {
    std::vector<fs::path> paths;
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (!object || FAILED(object->GetData(&format, &medium))) return paths;
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal) {
        auto drop = static_cast<HDROP>(medium.hGlobal);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (count <= 256) {
            for (UINT i = 0; i < count; ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(length + 1, L'\0');
                if (!length || !DragQueryFileW(drop, i, path.data(), length + 1)) {
                    paths.clear();
                    break;
                }
                path.resize(length);
                paths.emplace_back(path);
            }
        }
    }
    ReleaseStgMedium(&medium);
    return paths;
}

static std::wstring Lower(std::wstring value) {
    for (auto& ch : value) ch = static_cast<wchar_t>(towlower(ch));
    return value;
}

static std::wstring Identity(const fs::path& path) {
    // A stable path identity also makes repeated drops reuse the same pin.
    unsigned long long hash = 14695981039346656037ULL;
    for (wchar_t ch : Lower(path.lexically_normal().wstring())) {
        hash ^= static_cast<unsigned short>(ch);
        hash *= 1099511628211ULL;
    }
    wchar_t text[24]{};
    swprintf_s(text, L"%016llx", hash);
    return text;
}

static bool IsNativeApplication(const fs::path& path) {
    auto ext = Lower(path.extension().wstring());
    if (ext == L".exe" || ext == L".com") return true;
    if (ext != L".lnk") return false;
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> persist;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link))) ||
        FAILED(link.As(&persist)) || FAILED(persist->Load(path.c_str(), STGM_READ)))
        return false;
    wchar_t target[32768]{};
    if (FAILED(link->GetPath(target, ARRAYSIZE(target), nullptr, SLGP_RAWPATH)))
        return false;
    auto targetExt = Lower(fs::path(target).extension().wstring());
    return targetExt == L".exe" || targetExt == L".com";
}

static void SaveIcon(const fs::path& source, const fs::path& destination) {
    SHFILEINFOW info{};
    if (!SHGetFileInfoW(source.c_str(), 0, &info, sizeof(info),
                       SHGFI_ICON | SHGFI_LARGEICON)) return;
    PICTDESC description{};
    description.cbSizeofstruct = sizeof(description);
    description.picType = PICTYPE_ICON;
    description.icon.hicon = info.hIcon;
    ComPtr<IPicture> picture;
    const HRESULT created = OleCreatePictureIndirect(&description, IID_IPicture,
                                                      TRUE, &picture);
    if (FAILED(created)) { DestroyIcon(info.hIcon); return; }
    ComPtr<IStream> stream;
    if (FAILED(SHCreateStreamOnFileEx(destination.c_str(),
        STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, FILE_ATTRIBUTE_NORMAL,
        TRUE, nullptr, &stream))) return;
    LONG written = 0;
    const HRESULT result = picture->SaveAsFile(stream.Get(), TRUE, &written);
    if (FAILED(result)) Wh_Log(L"Icon extraction failed: %08X", result);
}

static fs::path PrepareShortcut(const fs::path& source) {
    const auto id = Identity(source);
    auto label = source.filename().wstring();
    if (label.empty()) label = source.root_name().wstring();
    if (Lower(source.extension().wstring()) == L".lnk")
        label = source.stem().wstring();
    for (auto& ch : label) {
        if (ch < 32 || wcschr(L"<>:\"/\\|?*", ch)) ch = L'_';
    }
    if (label.size() > 100) label.resize(100);
    auto destination = g_storage / L"links" / (label + L"-" + id + L".lnk");
    const auto icon = g_storage / L"icons" / (id + L".ico");
    fs::path launchTarget = source;
    if (Lower(source.extension().wstring()) == L".lnk") {
        launchTarget = g_storage / L"targets" / (id + L".lnk");
        if (!CopyFileW(source.c_str(), launchTarget.c_str(), FALSE))
            Check(HRESULT_FROM_WIN32(GetLastError()));
    }
    if (!fs::exists(icon)) SaveIcon(source, icon);
    ComPtr<IShellLinkW> link;
    Check(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&link)));
    Check(link->SetPath(g_launcher.c_str()));
    const auto arguments = L"\"" + launchTarget.wstring() + L"\"";
    Check(link->SetArguments(arguments.c_str()));
    Check(link->SetDescription(label.c_str()));
    if (fs::exists(icon)) Check(link->SetIconLocation(icon.c_str(), 0));
    ComPtr<IPropertyStore> properties;
    Check(link.As(&properties));
    PROPVARIANT value{};
    Check(InitPropVariantFromString((L"TaskbarDropPins." + id).c_str(), &value));
    const HRESULT propertyResult = properties->SetValue(PKEY_AppUserModel_ID, value);
    PropVariantClear(&value);
    Check(propertyResult);
    Check(properties->Commit());
    ComPtr<IPersistFile> persist;
    Check(link.As(&persist));
    Check(persist->Save(destination.c_str(), TRUE));
    return destination;
}

static ComPtr<IDataObject> PrepareDataObject(IDataObject* source) {
    const auto paths = ReadPaths(source);
    if (paths.empty()) return {};
    std::vector<fs::path> converted;
    bool changed = false;
    const auto storagePrefix = Lower(g_storage.wstring() + L"\\");
    for (const auto& path : paths) {
        if (!path.is_absolute() || !fs::exists(path)) return {};
        if (Lower(path.wstring()).starts_with(storagePrefix) || IsNativeApplication(path)) {
            converted.push_back(path);
        } else {
            converted.push_back(PrepareShortcut(path));
            changed = true;
        }
    }
    if (!changed) return {};
    std::vector<PIDLIST_ABSOLUTE> ids;
    ComPtr<IDataObject> object;
    try {
        for (const auto& path : converted) {
            PIDLIST_ABSOLUTE id = nullptr;
            Check(SHParseDisplayName(path.c_str(), nullptr, &id, 0, nullptr));
            ids.push_back(id);
        }
        ComPtr<IShellItemArray> items;
        Check(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.size()),
             const_cast<PCIDLIST_ABSOLUTE*>(ids.data()), &items));
        Check(items->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&object)));
    } catch (...) {
        for (auto id : ids) CoTaskMemFree(id);
        throw;
    }
    for (auto id : ids) CoTaskMemFree(id);
    return object;
}

struct DragState {
    ComPtr<IDataObject> object;
    DWORD allowed;
};
static std::mutex g_dragsMutex;
static std::unordered_map<IDropTarget*, std::shared_ptr<DragState>> g_drags;

static std::shared_ptr<DragState> TakeState(IDropTarget* target) {
    std::shared_ptr<DragState> state;
    {
        std::lock_guard lock(g_dragsMutex);
        auto it = g_drags.find(target);
        if (it != g_drags.end()) { state = std::move(it->second); g_drags.erase(it); }
    }
    return state;
}

using EnterFunction = HRESULT(STDMETHODCALLTYPE*)(IDropTarget*, IDataObject*, DWORD, POINTL, DWORD*);
using OverFunction = HRESULT(STDMETHODCALLTYPE*)(IDropTarget*, DWORD, POINTL, DWORD*);
using LeaveFunction = HRESULT(STDMETHODCALLTYPE*)(IDropTarget*);
static constexpr size_t kSlots = 12;
static std::array<EnterFunction, kSlots> g_enter{};
static std::array<OverFunction, kSlots> g_over{};
static std::array<LeaveFunction, kSlots> g_leave{};
static std::array<EnterFunction, kSlots> g_drop{};
static std::array<void*, kSlots> g_enterAddresses{}, g_overAddresses{},
                                  g_leaveAddresses{}, g_dropAddresses{};

static void FinishEffect(DWORD* effect, const std::shared_ptr<DragState>& state) {
    if (state && effect && *effect != DROPEFFECT_NONE)
        *effect = (state->allowed & DROPEFFECT_LINK) ? DROPEFFECT_LINK : DROPEFFECT_COPY;
}

template<size_t Slot>
static HRESULT STDMETHODCALLTYPE EnterHook(IDropTarget* target, IDataObject* data,
                                          DWORD keys, POINTL point, DWORD* effect) {
    auto previous = TakeState(target);
    previous.reset();
    std::shared_ptr<DragState> state;
    if (!g_stopping && effect && (*effect & (DROPEFFECT_LINK | DROPEFFECT_COPY)) &&
        !(keys & (MK_SHIFT | MK_RBUTTON)) &&
        IsTaskbar(WindowFromPoint({point.x, point.y}))) {
        try {
            auto replacement = PrepareDataObject(data);
            if (replacement) {
                state = std::make_shared<DragState>();
                state->object = std::move(replacement);
                state->allowed = *effect;
                std::lock_guard lock(g_dragsMutex);
                g_drags[target] = state;
                Wh_Log(L"Converted filesystem drag for native taskbar pinning");
            }
        } catch (HRESULT result) {
            Wh_Log(L"Shortcut preparation failed: %08X", result);
        } catch (const std::exception& error) {
            Wh_Log(L"Shortcut preparation failed: %S", error.what());
        } catch (...) {
            Wh_Log(L"Shortcut preparation failed with an unexpected exception");
        }
    }
    const HRESULT result = g_enter[Slot](target, state ? state->object.Get() : data,
                                         keys, point, effect);
    FinishEffect(effect, state);
    return result;
}

template<size_t Slot>
static HRESULT STDMETHODCALLTYPE OverHook(IDropTarget* target, DWORD keys,
                                         POINTL point, DWORD* effect) {
    std::shared_ptr<DragState> state;
    { std::lock_guard lock(g_dragsMutex);
      auto it = g_drags.find(target); if (it != g_drags.end()) state = it->second; }
    const HRESULT result = g_over[Slot](target, keys, point, effect);
    FinishEffect(effect, state);
    return result;
}

template<size_t Slot>
static HRESULT STDMETHODCALLTYPE LeaveHook(IDropTarget* target) {
    auto state = TakeState(target);
    return g_leave[Slot](target);
}

template<size_t Slot>
static HRESULT STDMETHODCALLTYPE DropHook(IDropTarget* target, IDataObject* data,
                                         DWORD keys, POINTL point, DWORD* effect) {
    auto state = TakeState(target);
    const HRESULT result = g_drop[Slot](target, state ? state->object.Get() : data,
                                        keys, point, effect);
    FinishEffect(effect, state);
    if (state) {
        Wh_SetIntValue(L"lastDropResult", result);
        Wh_SetIntValue(L"lastDropEffect", effect ? *effect : 0);
        Wh_Log(L"Native taskbar drop: HRESULT=%08X effect=%u", result, effect ? *effect : 0);
    }
    return result;
}

template<size_t... Slots>
static auto EnterHooks(std::index_sequence<Slots...>) {
    return std::array<EnterFunction, kSlots>{EnterHook<Slots>...};
}
template<size_t... Slots>
static auto OverHooks(std::index_sequence<Slots...>) {
    return std::array<OverFunction, kSlots>{OverHook<Slots>...};
}
template<size_t... Slots>
static auto LeaveHooks(std::index_sequence<Slots...>) {
    return std::array<LeaveFunction, kSlots>{LeaveHook<Slots>...};
}
template<size_t... Slots>
static auto DropHooks(std::index_sequence<Slots...>) {
    return std::array<EnterFunction, kSlots>{DropHook<Slots>...};
}

template<typename Function>
static bool HookMethod(void* address, std::array<void*, kSlots>& addresses,
                       std::array<Function, kSlots>& originals,
                       const std::array<Function, kSlots>& hooks) {
    for (size_t i = 0; i < kSlots; ++i) {
        if (addresses[i] == address) return true;
        if (!addresses[i]) {
            if (!Wh_SetFunctionHook(address, reinterpret_cast<void*>(hooks[i]),
                                     reinterpret_cast<void**>(&originals[i]))) return false;
            addresses[i] = address;
            return true;
        }
    }
    return false;
}

static bool DiscoverTarget(IDropTarget* target) {
    if (!target || g_stopping) return false;
    std::lock_guard lock(g_hooksMutex);
    auto table = *reinterpret_cast<void***>(target);
    bool result = HookMethod(table[3], g_enterAddresses, g_enter,
                              EnterHooks(std::make_index_sequence<kSlots>{}));
    result &= HookMethod(table[4], g_overAddresses, g_over,
                           OverHooks(std::make_index_sequence<kSlots>{}));
    result &= HookMethod(table[5], g_leaveAddresses, g_leave,
                           LeaveHooks(std::make_index_sequence<kSlots>{}));
    result &= HookMethod(table[6], g_dropAddresses, g_drop,
                           DropHooks(std::make_index_sequence<kSlots>{}));
    unsigned count = 0;
    for (auto address : g_enterAddresses) if (address) ++count;
    Wh_SetIntValue(L"hookedDropHandlers", static_cast<int>(count));
    Wh_Log(L"Taskbar drop target=%p, handlers=%u, installed=%d", target, count, result);
    return result;
}

using RegisterFunction = HRESULT(WINAPI*)(HWND, IDropTarget*);
static RegisterFunction g_register;
static HRESULT WINAPI RegisterHook(HWND window, IDropTarget* target) {
    const HRESULT result = g_register(window, target);
    if (SUCCEEDED(result) && !g_stopping && IsTaskbar(window)) {
        DiscoverTarget(target);
        Wh_ApplyHookOperations();
    }
    return result;
}

static LRESULT CALLBACK WindowSubclass(HWND window, UINT message, WPARAM wp,
                                      LPARAM lp, DWORD_PTR) {
    if (message == g_discoverMessage && !g_stopping) {
        auto target = reinterpret_cast<IDropTarget*>(GetPropW(window, L"OleDropTargetInterface"));
        return target && DiscoverTarget(target);
    }
    return DefSubclassProc(window, message, wp, lp);
}

static BOOL CALLBACK DiscoverWindow(HWND window, LPARAM) {
    if (!IsTaskbar(window)) return TRUE;
    if (GetPropW(window, L"OleDropTargetInterface") &&
        WindhawkUtils::SetWindowSubclassFromAnyThread(window, WindowSubclass, 0)) {
        g_subclassed.push_back(window);
        SendMessageW(window, g_discoverMessage, 0, 0);
    }
    return TRUE;
}

static BOOL CALLBACK DiscoverTaskbar(HWND window, LPARAM) {
    if (IsTaskbar(window) && GetAncestor(window, GA_ROOT) == window) {
        DiscoverWindow(window, 0);
        EnumChildWindows(window, DiscoverWindow, 0);
    }
    return TRUE;
}

static void InitializeStorage() {
    wchar_t path[32768]{};
    if (!Wh_GetModStoragePath(path, ARRAYSIZE(path))) Check(E_FAIL);
    g_storage = path;
    fs::create_directories(g_storage / L"links");
    fs::create_directories(g_storage / L"icons");
    fs::create_directories(g_storage / L"targets");
    g_launcher = g_storage / L"TaskbarDropPins.Launcher-v1.exe";
    if (!fs::exists(g_launcher))
        WriteBytes(g_launcher, kLauncherBytes, static_cast<DWORD>(sizeof(kLauncherBytes)));
}

BOOL Wh_ModInit() {
    if (!FindWindowW(L"Shell_TrayWnd", nullptr)) return FALSE;
    try { InitializeStorage(); }
    catch (...) { Wh_Log(L"Cannot initialize permanent mod storage"); return FALSE; }
    g_discoverMessage = RegisterWindowMessageW(L"TaskbarDropPins.Discover.v1");
    Wh_SetIntValue(L"hookedDropHandlers", 0);
    const auto ole = GetModuleHandleW(L"ole32.dll");
    if (!ole || !Wh_SetFunctionHook(reinterpret_cast<void*>(GetProcAddress(ole, "RegisterDragDrop")),
                                    reinterpret_cast<void*>(RegisterHook),
                                    reinterpret_cast<void**>(&g_register))) return FALSE;
    EnumWindows(DiscoverTaskbar, 0);
    return TRUE;
}

void Wh_ModAfterInit() {
    Wh_Log(L"Taskbar Drop Pins initialized");
}

void Wh_ModBeforeUninit() {
    g_stopping = true;
    for (auto window : g_subclassed)
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(window, WindowSubclass);
    g_subclassed.clear();
}

void Wh_ModUninit() {
    std::unordered_map<IDropTarget*, std::shared_ptr<DragState>> states;
    { std::lock_guard lock(g_dragsMutex); states.swap(g_drags); }
}
