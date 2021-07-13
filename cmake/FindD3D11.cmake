# Autodetect if D3D11, DXGI 1.5, d3dcompiler and WRL can be used.

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "")

check_cxx_source_compiles("
    #undef NTDDI_VERSION
    #undef _WIN32_WINNT

    #define NTDDI_VERSION    NTDDI_VISTA
    #define _WIN32_WINNT     _WIN32_WINNT_VISTA

    #include <wrl/client.h>
    #include <d3d11.h>
    #include <dxgi1_5.h>
    #include <d3dcompiler.h>
    int main() { return 0; }"
    D3D11_FOUND
)
