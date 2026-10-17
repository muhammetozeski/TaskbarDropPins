// Explorer's classic file dragging starts at DoDragDrop even when the target
// uses a different desktop/XAML bridge. Keep the original source everywhere
// except an explicit file drop on this Explorer process's taskbar.
struct PinRequest {
    std::vector<fs::path> paths;
    HWND window;
    POINT point;
};

static DWORD WINAPI PinRequestThread(void* parameter) {
    std::unique_ptr<PinRequest> request(static_cast<PinRequest*>(parameter));
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        if (!g_stopping) {
            const auto count = PinFiles(request->paths, request->window, request->point);
            Wh_SetIntValue(L"sourcePinnedCount", static_cast<int>(count));
        }
    } catch (HRESULT error) { Wh_SetIntValue(L"lastPinResult", error); }
    catch (...) { Wh_SetIntValue(L"lastPinResult", E_FAIL); }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return 0;
}

struct TaskbarDropSource final : IDropSource {
    std::atomic<ULONG> references{1};
    ComPtr<IDropSource> original;
    ComPtr<IDataObject> data;
    ComPtr<IDropTargetHelper> feedback;
    bool showing = false;
    bool dropped = false;
    HWND window = nullptr;
    POINT point{};

    TaskbarDropSource(IDropSource* source, IDataObject* object) : original(source), data(object) {
        CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&feedback));
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (!object) return E_POINTER;
        if (id == IID_IUnknown || id == IID_IDropSource) {
            *object = static_cast<IDropSource*>(this); AddRef(); return S_OK;
        }
        return original->QueryInterface(id, object);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references;
        if (!count) delete this;
        return count;
    }
    HWND CurrentTaskbar() {
        if (g_stopping || (GetKeyState(VK_SHIFT) & 0x8000)) return nullptr;
        GetCursorPos(&point);
        return TaskbarAtPoint(point);
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        const HRESULT result = original->QueryContinueDrag(escape, keys);
        if (!escape && result == DRAGDROP_S_DROP && (window = CurrentTaskbar())) {
            dropped = true;
            if (feedback && showing) { feedback->DragLeave(); showing = false; }
            // Cancel the original file transfer so Explorer cannot move/delete
            // the source. Pinning is a separate non-destructive Shell action.
            return DRAGDROP_S_CANCEL;
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD effect) override {
        if (auto bar = CurrentTaskbar()) {
            if (feedback) {
                if (!showing) { feedback->DragEnter(bar, data.Get(), &point, DROPEFFECT_LINK); showing = true; }
                else feedback->DragOver(&point, DROPEFFECT_LINK);
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            Wh_SetIntValue(L"sourceOverTaskbar", 1);
            return S_OK;
        }
        if (feedback && showing) { feedback->DragLeave(); showing = false; }
        return original->GiveFeedback(effect);
    }
};

using DoDragDropFunction = HRESULT(WINAPI*)(IDataObject*, IDropSource*, DWORD, DWORD*);
static DoDragDropFunction g_doDragDrop;
static thread_local bool g_sourceDragActive = false;
static HRESULT WINAPI DoDragDropHook(IDataObject* data, IDropSource* source,
                                    DWORD allowed, DWORD* effect) {
    if (g_stopping || !source || g_sourceDragActive)
        return g_doDragDrop(data, source, allowed, effect);
    auto paths = ReadPaths(data);
    if (paths.empty()) return g_doDragDrop(data, source, allowed, effect);
    bool needsConversion = false;
    for (const auto& path : paths) {
        if (!IsNativeApplication(path)) { needsConversion = true; break; }
    }
    if (!needsConversion) return g_doDragDrop(data, source, allowed, effect);
    Wh_SetIntValue(L"sourceDragCount", Wh_GetIntValue(L"sourceDragCount", 0) + 1);
    Wh_SetIntValue(L"sourceOverTaskbar", 0);
    auto wrapper = new TaskbarDropSource(source, data);
    g_sourceDragActive = true;
    const HRESULT result = g_doDragDrop(data, wrapper, allowed, effect);
    g_sourceDragActive = false;
    if (wrapper->feedback && wrapper->showing) wrapper->feedback->DragLeave();
    if (wrapper->dropped && !g_stopping) {
        auto request = std::make_unique<PinRequest>();
        request->paths = std::move(paths);
        request->window = wrapper->window;
        request->point = wrapper->point;
        HANDLE worker = CreateThread(nullptr, 0, PinRequestThread, request.get(), 0, nullptr);
        if (worker) { request.release(); CloseHandle(worker); }
        else Wh_SetIntValue(L"lastPinResult", HRESULT_FROM_WIN32(GetLastError()));
        if (effect) *effect = DROPEFFECT_NONE;
    }
    wrapper->Release();
    return result;
}

static bool InitializeSourceHook() {
    const auto ole = GetModuleHandleW(L"ole32.dll");
    const auto address = ole ? GetProcAddress(ole, "DoDragDrop") : nullptr;
    Wh_SetIntValue(L"sourceExportFound", address != nullptr);
    const bool installed = address && Wh_SetFunctionHook(reinterpret_cast<void*>(address),
                                    reinterpret_cast<void*>(DoDragDropHook),
                                    reinterpret_cast<void**>(&g_doDragDrop));
    Wh_SetIntValue(L"sourceHookInstalled", installed);
    if (installed) {
        HMODULE lifetime = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&InitializeSourceHook), &lifetime);
    }
    return installed;
}
