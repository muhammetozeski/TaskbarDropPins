#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.DragDrop.Core.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <uiautomation.h>
#include <algorithm>

namespace transfer = winrt::Windows::ApplicationModel::DataTransfer;
namespace core = transfer::DragDrop::Core;
namespace foundation = winrt::Windows::Foundation;

// Private Shell PinManager ABI. Interface identifiers and method order are
// exposed by the current Windows Shell; only this isolated adapter uses it.
static constexpr CLSID kPinManagerClass =
    {0xA5C8D635,0xB4ED,0x452B,{0x81,0x09,0x95,0x01,0x78,0x10,0x96,0xD1}};
static constexpr IID kPinManager2Id =
    {0x87D9E034,0x56D0,0x4F8C,{0xBE,0x59,0x99,0x7B,0x01,0x75,0x47,0x10}};
static constexpr IID kPinManager3Id =
    {0x27216D91,0x78B8,0x406D,{0x85,0xC4,0x5F,0xDB,0xF5,0x1C,0xD1,0x6B}};
struct ShellPinManager2 : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE PinShim(PCUIDLIST_ABSOLUTE, int) = 0;
    virtual HRESULT STDMETHODCALLTYPE PinTrusted(PCUIDLIST_ABSOLUTE, int) = 0;
    virtual HRESULT STDMETHODCALLTYPE PrependDefaults() = 0;
    virtual HRESULT STDMETHODCALLTYPE AppendDefaults() = 0;
    virtual HRESULT STDMETHODCALLTYPE InPlaceLayout(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReorderLayout(int, int) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unpin(PCUIDLIST_ABSOLUTE, int) = 0;
    virtual HRESULT STDMETHODCALLTYPE Update(PCUIDLIST_ABSOLUTE, PCUIDLIST_ABSOLUTE, int) = 0;
};
struct ShellPinManager3 : ShellPinManager2 {
    virtual HRESULT STDMETHODCALLTYPE Move(PCUIDLIST_ABSOLUTE, int, int) = 0;
};

static HWND TaskbarAtPoint(POINT point) {
    struct Search { POINT point; HWND result = nullptr; } search{point};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(parameter);
        RECT rect{};
        if (IsTaskbar(window) && GetAncestor(window, GA_ROOT) == window &&
            IsWindowVisible(window) && GetWindowRect(window, &rect) &&
            PtInRect(&rect, search.point)) { search.result = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

struct TaskbarButton {
    RECT rectangle;
    std::wstring appId;
};

static std::vector<TaskbarButton> TaskbarButtons(HWND window) {
    ComPtr<IUIAutomation> automation;
    Check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&automation)));
    ComPtr<IUIAutomationElement> root;
    Check(automation->ElementFromHandle(window, &root));
    ComPtr<IUIAutomationCondition> condition;
    Check(automation->CreateTrueCondition(&condition));
    ComPtr<IUIAutomationElementArray> elements;
    Check(root->FindAll(TreeScope_Descendants, condition.Get(), &elements));
    int count = 0;
    Check(elements->get_Length(&count));
    std::vector<TaskbarButton> buttons;
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(elements->GetElement(i, &element))) continue;
        BSTR id = nullptr;
        if (FAILED(element->get_CurrentAutomationId(&id))) continue;
        const std::wstring value = id ? id : L"";
        SysFreeString(id);
        if (!value.starts_with(L"Appid:")) continue;
        RECT rectangle{};
        if (SUCCEEDED(element->get_CurrentBoundingRectangle(&rectangle)) &&
            rectangle.right > rectangle.left && rectangle.bottom > rectangle.top)
            buttons.push_back({rectangle, value.substr(6)});
    }
    std::sort(buttons.begin(), buttons.end(), [](const auto& a, const auto& b) {
        return a.rectangle.left < b.rectangle.left;
    });
    return buttons;
}

static int InsertionIndex(HWND window, POINT point) {
    const auto buttons = TaskbarButtons(window);
    if (buttons.empty()) return -1;
    int index = 0;
    for (const auto& button : buttons) {
        if (point.x < (button.rectangle.left + button.rectangle.right) / 2) break;
        ++index;
    }
    return index;
}

static unsigned PinFiles(const std::vector<fs::path>& paths, HWND window, POINT point) {
    ComPtr<ShellPinManager2> pins;
    Check(CoCreateInstance(kPinManagerClass, nullptr, CLSCTX_INPROC_SERVER,
                           kPinManager2Id, reinterpret_cast<void**>(pins.GetAddressOf())));
    ComPtr<ShellPinManager3> movable;
    pins->QueryInterface(kPinManager3Id, reinterpret_cast<void**>(movable.GetAddressOf()));
    int index = -1;
    try { index = InsertionIndex(window, point); }
    catch (HRESULT error) { Wh_Log(L"Taskbar layout query failed: %08X", error); }
    Wh_SetIntValue(L"lastInsertionIndex", index);
    unsigned count = 0;
    for (const auto& path : paths) {
        const auto shortcut = IsNativeApplication(path) ? path : PrepareShortcut(path);
        PIDLIST_ABSOLUTE id = nullptr;
        Check(SHParseDisplayName(shortcut.c_str(), nullptr, &id, 0, nullptr));
        const HRESULT pinned = pins->PinTrusted(id, 25);
        Wh_SetIntValue(L"lastPinResult", pinned);
        if (SUCCEEDED(pinned)) {
            ++count;
            if (movable && index >= 0) {
                const HRESULT moved = movable->Move(id, index++, 27);
                Wh_SetIntValue(L"lastMoveResult", moved);
            }
        }
        CoTaskMemFree(id);
        Check(pinned);
    }
    return count;
}

struct ModernDropTarget : winrt::implements<ModernDropTarget, core::ICoreDropOperationTarget> {
    core::ICoreDropOperationTarget original{nullptr};
    std::vector<fs::path> paths;
    HWND window = nullptr;
    explicit ModernDropTarget(core::ICoreDropOperationTarget fallback) : original(std::move(fallback)) {}

    foundation::IAsyncOperation<transfer::DataPackageOperation> EnterAsync(
        core::CoreDragInfo info, core::CoreDragUIOverride feedback) {
        auto lifetime = get_strong();
        paths.clear();
        POINT point{};
        GetCursorPos(&point);
        window = TaskbarAtPoint(point);
        if (!g_stopping && window && !(GetKeyState(VK_SHIFT) & 0x8000) &&
            info.Data().Contains(transfer::StandardDataFormats::StorageItems())) {
            try {
                auto items = co_await info.Data().GetStorageItemsAsync();
                for (const auto& item : items) {
                    auto path = fs::path(item.Path().c_str());
                    if (!path.is_absolute() || !fs::exists(path)) { paths.clear(); break; }
                    paths.push_back(std::move(path));
                }
            } catch (const winrt::hresult_error& error) {
                Wh_SetIntValue(L"lastStorageReadError", error.code());
                paths.clear();
            } catch (...) { paths.clear(); }
        }
        Wh_SetIntValue(L"modernEnterCount", Wh_GetIntValue(L"modernEnterCount", 0) + 1);
        Wh_SetIntValue(L"modernFileCount", static_cast<int>(paths.size()));
        if (!paths.empty()) {
            feedback.Caption(L"Gorev cubuguna sabitle");
            feedback.IsCaptionVisible(true);
            feedback.IsGlyphVisible(true);
            co_return transfer::DataPackageOperation::Link;
        }
        if (original) co_return co_await original.EnterAsync(info, feedback);
        co_return transfer::DataPackageOperation::None;
    }

    foundation::IAsyncOperation<transfer::DataPackageOperation> OverAsync(
        core::CoreDragInfo info, core::CoreDragUIOverride feedback) {
        auto lifetime = get_strong();
        if (!g_stopping && !paths.empty()) co_return transfer::DataPackageOperation::Link;
        if (original) co_return co_await original.OverAsync(info, feedback);
        co_return transfer::DataPackageOperation::None;
    }

    foundation::IAsyncAction LeaveAsync(core::CoreDragInfo info) {
        auto lifetime = get_strong();
        const bool handled = !paths.empty();
        paths.clear();
        if (!handled && original) co_await original.LeaveAsync(info);
        co_return;
    }

    foundation::IAsyncOperation<transfer::DataPackageOperation> DropAsync(core::CoreDragInfo info) {
        auto lifetime = get_strong();
        if (!g_stopping && !paths.empty()) {
            POINT point{};
            GetCursorPos(&point);
            auto files = std::move(paths);
            auto targetWindow = window;
            co_await winrt::resume_background();
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            unsigned pinned = 0;
            try { if (!g_stopping) pinned = PinFiles(files, targetWindow, point); }
            catch (HRESULT error) { Wh_SetIntValue(L"lastPinResult", error); }
            catch (const winrt::hresult_error& error) { Wh_SetIntValue(L"lastPinResult", error.code()); }
            catch (...) { Wh_SetIntValue(L"lastPinResult", E_FAIL); }
            if (SUCCEEDED(initialized)) CoUninitialize();
            Wh_SetIntValue(L"modernPinnedCount", static_cast<int>(pinned));
            co_return pinned ? transfer::DataPackageOperation::Link : transfer::DataPackageOperation::None;
        }
        if (original) co_return co_await original.DropAsync(info);
        co_return transfer::DataPackageOperation::None;
    }
};

using SetTargetFunction = HRESULT(STDMETHODCALLTYPE*)(void*, void*);
static SetTargetFunction g_setModernTarget;
static void* g_modernSetterAddress = nullptr;
static thread_local bool g_settingOwnTarget = false;
static thread_local void* g_lastTargetArgs = nullptr;

static HRESULT STDMETHODCALLTYPE SetModernTargetHook(void* args, void* target) {
    if (!g_stopping && !g_settingOwnTarget && target) {
        try {
            core::ICoreDropOperationTarget original{nullptr};
            winrt::copy_from_abi(original, target);
            const auto wrapper = winrt::make<ModernDropTarget>(std::move(original));
            g_lastTargetArgs = args;
            return g_setModernTarget(args, winrt::get_abi(wrapper));
        } catch (...) { Wh_Log(L"Cannot wrap native modern drag target"); }
    }
    return g_setModernTarget(args, target);
}

struct CoreSubscription {
    core::CoreDragDropManager manager{nullptr};
    winrt::event_token token{};
};
static std::unordered_map<HWND, CoreSubscription> g_coreViews;
static UINT g_unsubscribeMessage;

static void SubscribeModern(HWND window) {
    try {
        if (g_coreViews.contains(window)) return;
        auto manager = core::CoreDragDropManager::GetForCurrentView();
        if (!manager) return;
        auto token = manager.TargetRequested([](const auto&, const core::CoreDropOperationTargetRequestedEventArgs& args) {
            if (g_stopping) return;
            try {
                auto raw = winrt::get_abi(args);
                auto table = *reinterpret_cast<void***>(raw);
                {
                    std::lock_guard lock(g_hooksMutex);
                    if (!g_modernSetterAddress && Wh_SetFunctionHook(table[6],
                        reinterpret_cast<void*>(SetModernTargetHook),
                        reinterpret_cast<void**>(&g_setModernTarget))) {
                        g_modernSetterAddress = table[6];
                        Wh_ApplyHookOperations();
                    }
                }
                if (g_lastTargetArgs != raw) {
                    g_settingOwnTarget = true;
                    args.SetTarget(winrt::make<ModernDropTarget>(nullptr));
                    g_settingOwnTarget = false;
                }
                g_lastTargetArgs = nullptr;
                Wh_SetIntValue(L"modernRequestCount", Wh_GetIntValue(L"modernRequestCount", 0) + 1);
            } catch (const winrt::hresult_error& error) {
                g_settingOwnTarget = false;
                Wh_SetIntValue(L"modernRequestError", error.code());
            }
        });
        g_coreViews.emplace(window, CoreSubscription{manager, token});
        Wh_SetIntValue(L"modernViews", static_cast<int>(g_coreViews.size()));
        // Native drag operations can retain the wrapper across a mod disable.
        // Keep its code mapped; callbacks pass through once g_stopping is set.
        HMODULE lifetime = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&SubscribeModern), &lifetime);
    } catch (const winrt::hresult_error& error) {
        Wh_SetIntValue(L"modernSubscribeError", error.code());
    }
}

static void UnsubscribeModern(HWND window) {
    auto it = g_coreViews.find(window);
    if (it != g_coreViews.end()) {
        it->second.manager.TargetRequested(it->second.token);
        g_coreViews.erase(it);
    }
}
