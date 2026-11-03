#include "dxc_compiler.h"

#ifdef FUTARI_RENUT_IN_EXE
// futari_recomp (2026-11-03): inside futari.exe the DirectX Shader Compiler is loaded when the tool runs, never at the
// game's start (the game must boot without it). FUTARI_RENUT_DXCOMPILER names the DLL (the tool mode sets it to
// renut_tools/dxcompiler.dll beside the exe); otherwise the loader's own search for dxcompiler.dll.
#include <mutex>
static DxcCreateInstanceProc futariDxcCreateInstance()
{
    static DxcCreateInstanceProc proc = nullptr;
    static std::once_flag once;
    std::call_once(once, []
    {
        const char* path = std::getenv("FUTARI_RENUT_DXCOMPILER");
        HMODULE module = nullptr;
        if (path && *path)
        {
            const int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
            std::wstring wide(n > 0 ? n - 1 : 0, L'\0');
            if (n > 0)
                MultiByteToWideChar(CP_UTF8, 0, path, -1, wide.data(), n);
            module = LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
        else
        {
            // No path named: the loader's search. Never a fallback for a named path that failed, which could pick
            // up a different DXC from the PATH (a Vulkan or Windows SDK) and translate differently.
            module = LoadLibraryW(L"dxcompiler.dll");
        }
        if (module == nullptr)
        {
            fmt::println(stderr, "XenosRecomp: cannot load the DirectX Shader Compiler ({}): error {}",
                         path && *path ? path : "dxcompiler.dll", GetLastError());
            return;
        }
        proc = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module, "DxcCreateInstance"));
        if (proc == nullptr)
            fmt::println(stderr, "XenosRecomp: the DirectX Shader Compiler has no DxcCreateInstance");
    });
    return proc;
}
#endif

DxcCompiler::DxcCompiler()
{
#ifdef FUTARI_RENUT_IN_EXE
    DxcCreateInstanceProc create = futariDxcCreateInstance();
    if (create == nullptr)
        return;  // compile() reports every shader as failed
    HRESULT hr = create(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
#else
    HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
#endif
    assert(SUCCEEDED(hr));
}

DxcCompiler::~DxcCompiler()
{
    if (dxcCompiler != nullptr)
        dxcCompiler->Release();
}

IDxcBlob* DxcCompiler::compile(const std::string& shaderSource, bool compilePixelShader, bool compileLibrary, bool compileSpirv,
                               bool deko3d)
{
    if (dxcCompiler == nullptr)
        return nullptr;

    DxcBuffer source{};
    source.Ptr = shaderSource.c_str();
    source.Size = shaderSource.size();

    const wchar_t* args[32]{};
    uint32_t argCount = 0;

    const wchar_t* target = nullptr;
    if (compileLibrary)
    {
        assert(!compileSpirv);
        target = L"-T lib_6_3";
    }
    else
    {
        if (compilePixelShader)
            target = L"-T ps_6_0";
        else
            target = L"-T vs_6_0";
    }

    args[argCount++] = target;
    args[argCount++] = L"-HV 2021";
    args[argCount++] = L"-all-resources-bound";

    if (compileSpirv)
    {
        args[argCount++] = L"-spirv";
        args[argCount++] = L"-fvk-use-dx-layout";

        // banjo-nx: deko3d 0.5.0 with its upper-left origin maps D3D's clip space (y up) to the top row as it is.
        if (!compilePixelShader && !deko3d)
            args[argCount++] = L"-fvk-invert-y";
        if (deko3d)
            args[argCount++] = L"-DBANJO_DEKO3D";
    }
    else
    {
        args[argCount++] = L"-Wno-ignored-attributes";
        args[argCount++] = L"-Qstrip_reflect";
    }

    args[argCount++] = L"-Qstrip_debug";

    // Keep the HLSL preprocessor in step with how this tool was built so
    // shader_common.h selects the matching constant layout.
#ifdef REBLUE_RECOMP
    args[argCount++] = L"-DREBLUE_RECOMP";
#elif defined(RENUT_RECOMP)
    args[argCount++] = L"-DRENUT_RECOMP";
#elif defined(UNLEASHED_RECOMP)
    args[argCount++] = L"-DUNLEASHED_RECOMP";
#endif

    IDxcResult* result = nullptr;
    HRESULT hr = dxcCompiler->Compile(&source, args, argCount, nullptr, IID_PPV_ARGS(&result));

    IDxcBlob* object = nullptr;
    if (SUCCEEDED(hr))
    {
        assert(result != nullptr);

        HRESULT status;
        hr = result->GetStatus(&status);
        assert(SUCCEEDED(hr));

        if (FAILED(status))
        {
            if (result->HasOutput(DXC_OUT_ERRORS))
            {
                IDxcBlobUtf8* errors = nullptr;
                hr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
                assert(SUCCEEDED(hr) && errors != nullptr);

                fputs(errors->GetStringPointer(), stderr);

                errors->Release();
            }
        }
        else
        {
            hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
            assert(SUCCEEDED(hr) && object != nullptr);
        }

        result->Release();
    }
    else
    {
        assert(result == nullptr);
    }

    return object;
}
