#include <uiautomation.h>
#include <algorithm>

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

