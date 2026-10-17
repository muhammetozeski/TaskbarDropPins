#define WH_EDITING
#include "../dist/taskbar-drop-pins.wh.cpp"
#include <iostream>

static void Require(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}

static ComPtr<IDataObject> Files(const std::vector<fs::path>& paths) {
    std::vector<PIDLIST_ABSOLUTE> ids;
    for (const auto& path : paths) {
        PIDLIST_ABSOLUTE id = nullptr;
        Check(SHParseDisplayName(path.c_str(), nullptr, &id, 0, nullptr));
        ids.push_back(id);
    }
    ComPtr<IShellItemArray> array;
    const auto result = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.size()),
        const_cast<PCIDLIST_ABSOLUTE*>(ids.data()), &array);
    for (auto id : ids) CoTaskMemFree(id);
    Check(result);
    ComPtr<IDataObject> object;
    Check(array->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&object)));
    return object;
}

static ComPtr<IShellLinkW> LoadLink(const fs::path& path) {
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    Check(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)));
    Check(link.As(&file));
    Check(file->Load(path.c_str(), STGM_READ));
    return link;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 3;
    int exitCode = 0;
    try {
        if (wcscmp(argv[1], L"--layout") == 0) {
            const auto buttons = TaskbarButtons(FindWindowW(L"Shell_TrayWnd", nullptr));
            for (const auto& button : buttons)
                std::wcout << L"BUTTON left=" << button.rectangle.left
                           << L" right=" << button.rectangle.right << L" id=" << button.appId << L'\n';
            Require(!buttons.empty(), "No native taskbar buttons were found");
            CoUninitialize();
            return 0;
        }
        const fs::path root = argv[1];
        const auto folder = root / L"Klasör ve boşluk";
        fs::create_directories(folder);
        const auto text = folder / L"Örnek belge.txt";
        const char content[] = "original contents";
        WriteBytes(text, content, sizeof(content));
        g_storage = root / L"permanent-storage";
        for (auto name : {L"links", L"icons", L"targets"}) fs::create_directories(g_storage / name);
        g_launcher = g_storage / L"TaskbarDropPins.Launcher-v1.exe";
        WriteBytes(g_launcher, kLauncherBytes, sizeof(kLauncherBytes));

        auto object = Files({text, folder});
        Require(ReadPaths(object.Get()).size() == 2, "Shell file extraction failed");
        auto converted = PrepareDataObject(object.Get());
        Require(static_cast<bool>(converted), "File and folder conversion failed");
        auto paths = ReadPaths(converted.Get());
        Require(paths.size() == 2, "Converted Shell data has wrong file count");
        Require(paths[0] != paths[1], "Distinct targets merged");
        for (const auto& path : paths) {
            auto wrapper = LoadLink(path);
            wchar_t target[32768]{}, args[32768]{};
            Check(wrapper->GetPath(target, ARRAYSIZE(target), nullptr, SLGP_RAWPATH));
            Check(wrapper->GetArguments(args, ARRAYSIZE(args)));
            Require(fs::path(target) == g_launcher, "Wrapper launcher path is incorrect");
            Require(args[0] == L'"', "Target argument is not quoted");
            ComPtr<IPropertyStore> store;
            Check(wrapper.As(&store));
            PROPVARIANT id{};
            Check(store->GetValue(PKEY_AppUserModel_ID, &id));
            Require(id.vt == VT_LPWSTR && std::wstring(id.pwszVal).starts_with(L"TaskbarDropPins."),
                    "Distinct native taskbar application identity is missing");
            PropVariantClear(&id);
        }
        Require(fs::file_size(text) == sizeof(content), "Original file was modified");
        auto again = PrepareDataObject(object.Get());
        Require(ReadPaths(again.Get()) == paths, "Repeated drag creates duplicate identities");
        Require(!PrepareDataObject(converted.Get()), "Native wrapper drag must pass through for reordering");
        std::cout << "PASS file/folder Shell conversion, Unicode paths, distinct identities, duplicate reuse, native reorder passthrough\n";

        const auto folderIcon = g_storage / L"icons" / (Identity(folder) + L"-v2.ico");
        ComPtr<IWICImagingFactory> factory;
        Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
        ComPtr<IWICBitmapDecoder> decoder;
        Check(factory->CreateDecoderFromFilename(folderIcon.c_str(), nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnLoad, &decoder));
        ComPtr<IWICBitmapFrameDecode> decoded;
        Check(decoder->GetFrame(0, &decoded));
        UINT iconWidth = 0, iconHeight = 0;
        Check(decoded->GetSize(&iconWidth, &iconHeight));
        ComPtr<IWICFormatConverter> iconPixels;
        Check(factory->CreateFormatConverter(&iconPixels));
        Check(iconPixels->Initialize(decoded.Get(), GUID_WICPixelFormat32bppBGRA,
              WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
        std::vector<BYTE> rendered(iconWidth * iconHeight * 4);
        Check(iconPixels->CopyPixels(nullptr, iconWidth * 4, static_cast<UINT>(rendered.size()), rendered.data()));
        SHFILEINFOW originalIcon{};
        Require(SHGetFileInfoW(folder.c_str(), 0, &originalIcon, sizeof(originalIcon),
                              SHGFI_ICON | SHGFI_LARGEICON) != 0, "Cannot obtain original Shell icon");
        ComPtr<IWICBitmap> originalBitmap;
        const auto bitmapResult = factory->CreateBitmapFromHICON(originalIcon.hIcon, &originalBitmap);
        DestroyIcon(originalIcon.hIcon);
        Check(bitmapResult);
        ComPtr<IWICFormatConverter> originalPixels;
        Check(factory->CreateFormatConverter(&originalPixels));
        Check(originalPixels->Initialize(originalBitmap.Get(), GUID_WICPixelFormat32bppBGRA,
              WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
        std::vector<BYTE> expected(rendered.size());
        Check(originalPixels->CopyPixels(nullptr, iconWidth * 4, static_cast<UINT>(expected.size()), expected.data()));
        Require(rendered == expected, "Saved icon pixels differ from the original Shell icon");
        std::cout << "PASS full-color icon roundtrip preserves every BGRA pixel and transparency\n";

        const auto originalLink = root / L"Belge kısayolu.lnk";
        ComPtr<IShellLinkW> shortcut;
        ComPtr<IPersistFile> persist;
        Check(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shortcut)));
        Check(shortcut->SetPath(text.c_str()));
        Check(shortcut->SetArguments(L"--original-argument"));
        Check(shortcut->SetWorkingDirectory(folder.c_str()));
        Check(shortcut->SetShowCmd(SW_SHOWMINIMIZED));
        Check(shortcut.As(&persist));
        Check(persist->Save(originalLink.c_str(), TRUE));
        auto linkObject = Files({originalLink});
        auto convertedLink = PrepareDataObject(linkObject.Get());
        Require(static_cast<bool>(convertedLink), "Document .lnk conversion failed");
        const auto backup = g_storage / L"targets" / (Identity(originalLink) + L".lnk");
        auto preserved = LoadLink(backup);
        wchar_t arguments[256]{}, directory[32768]{};
        int show = 0, originalShow = 0;
        Check(preserved->GetArguments(arguments, ARRAYSIZE(arguments)));
        Check(preserved->GetWorkingDirectory(directory, ARRAYSIZE(directory)));
        Check(preserved->GetShowCmd(&show));
        Check(LoadLink(originalLink)->GetShowCmd(&originalShow));
        Require(wcscmp(arguments, L"--original-argument") == 0, "Shortcut arguments were lost");
        Require(fs::path(directory) == folder, "Shortcut working directory was lost");
        Require(show == originalShow, "Shortcut show mode was lost");
        Require(fs::exists(originalLink), "Original shortcut was moved");
        std::cout << "PASS .lnk backup preserves target arguments, working directory and show mode\n";

        wchar_t system[32768]{};
        GetSystemDirectoryW(system, ARRAYSIZE(system));
        const auto exe = fs::path(system) / L"cmd.exe";
        Check(shortcut->SetPath(exe.c_str()));
        const auto appLink = root / L"Application.lnk";
        Check(persist->Save(appLink.c_str(), TRUE));
        auto appData = Files({appLink});
        auto appConverted = PrepareDataObject(appData.Get());
        Require(static_cast<bool>(appConverted), "Application .lnk requires a distinct shortcut identity");
        const auto secondAppLink = root / L"Same application different arguments.lnk";
        Check(shortcut->SetArguments(L"--different-argument"));
        Check(persist->Save(secondAppLink.c_str(), TRUE));
        auto twoAppLinks = Files({appLink, secondAppLink});
        auto twoAppConverted = PrepareDataObject(twoAppLinks.Get());
        auto twoAppPaths = ReadPaths(twoAppConverted.Get());
        Require(twoAppPaths.size() == 2 && twoAppPaths[0] != twoAppPaths[1],
                "Shortcuts to the same executable were merged");
        auto mixed = Files({text, appLink});
        auto mixedConverted = PrepareDataObject(mixed.Get());
        Require(ReadPaths(mixedConverted.Get()).size() == 2, "Mixed drag lost a shortcut");
        std::cout << "PASS distinct .lnk identities for the same EXE and mixed multi-item drag\n";
    } catch (HRESULT result) {
        std::cerr << "FAIL HRESULT=0x" << std::hex << static_cast<unsigned>(result) << '\n';
        exitCode = 1;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        exitCode = 1;
    }
    CoUninitialize();
    return exitCode;
}
