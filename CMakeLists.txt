cmake_minimum_required(VERSION 3.20)
project(WinscreW LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}")

# ============================================================
#  Libraries  —  qwe projesindeki hazır kütüphaneler
# ============================================================
set(LIBS_DIR "C:/Users/tron/Desktop/qwe/Libraries")

# ============================================================
#  TARGET 1: SpCrashReport.dll  —  Gerçek Hile
#  VDS sunucusuna yüklenir, loader tarafından indirilir.
#  ProxyExports YOK — bu artık version.dll değil.
# ============================================================
set(CHEAT_SOURCES
    Cheat/Source.cpp
    Cheat/Assets.cpp
)

add_library(CheatPayload SHARED ${CHEAT_SOURCES})
set_target_properties(CheatPayload PROPERTIES
    OUTPUT_NAME  "SpCrashReport"   # → bin/Release/SpCrashReport.dll → VDS'e yükle
    PREFIX       ""
)

target_include_directories(CheatPayload PRIVATE
    "${CMAKE_SOURCE_DIR}/Cheat"
    "${LIBS_DIR}/mjlib/include"
    "${LIBS_DIR}/xorstr/include"
    "${LIBS_DIR}/libcurl/include"
    "${LIBS_DIR}/openssl/include"
    "${LIBS_DIR}/brotli/include"
    "${LIBS_DIR}/zlib/include"
    "${LIBS_DIR}/jsoncpp/include"
)

target_compile_definitions(CheatPayload PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _MBCS
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:Debug>:_DEBUG>
)

if(MSVC)
    target_compile_options(CheatPayload PRIVATE
        /W3 /MP /EHsc /utf-8
        $<$<CONFIG:Release>:/O2 /GL /Gy>
        $<$<CONFIG:Debug>:/Od /Zi /RTC1>
    )
    set_property(TARGET CheatPayload PROPERTY
        MSVC_RUNTIME_LIBRARY
        "$<$<CONFIG:Release>:MultiThreaded>$<$<CONFIG:Debug>:MultiThreadedDebug>")
    set_property(TARGET CheatPayload PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

target_link_libraries(CheatPayload PRIVATE
    dwmapi.lib
    msimg32.lib
    gdi32.lib
    Psapi.lib
    kernel32.lib
    user32.lib
    shcore.lib
    ole32.lib
    windowscodecs.lib
    shlwapi.lib
    winhttp.lib
    "${LIBS_DIR}/libcurl/lib/x64/libcurl.lib"
    "${LIBS_DIR}/openssl/lib/x64/libssl.lib"
    "${LIBS_DIR}/openssl/lib/x64/libcrypto.lib"
    "${LIBS_DIR}/brotli/lib/x64/brotlicommon.lib"
    "${LIBS_DIR}/brotli/lib/x64/brotlidec.lib"
    "${LIBS_DIR}/brotli/lib/x64/brotlienc.lib"
    "${LIBS_DIR}/zlib/lib/x64/zlib.lib"
    "${LIBS_DIR}/jsoncpp/lib/x64/jsoncpp.lib"
)

# ============================================================
#  TARGET 2: version.dll  —  Loader
#  Chrome klasörüne konur (%LOCALAPPDATA%\Google\Chrome\Application\).
#  Chrome açılınca otomatik yüklenir, VDS'den hileyi indirir.
#  Hile Chrome içinde çalışır → ReadProcessMemory → FiveM bağlantısı.
# ============================================================
add_library(Loader SHARED
    Loader/loader.cpp
    Loader/loader_exports.cpp
)

set_target_properties(Loader PROPERTIES
    OUTPUT_NAME         "version"
    ARCHIVE_OUTPUT_NAME "loader_import"
    PREFIX              ""
)

target_include_directories(Loader PRIVATE
    "${LIBS_DIR}/xorstr/include"
)

target_compile_definitions(Loader PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:Debug>:_DEBUG>
)

if(MSVC)
    target_compile_options(Loader PRIVATE
        /W3 /MP /EHsc /utf-8
        $<$<CONFIG:Release>:/O2 /GL /Gy>
        $<$<CONFIG:Debug>:/Od /Zi /RTC1>
    )
    set_property(TARGET Loader PROPERTY
        MSVC_RUNTIME_LIBRARY
        "$<$<CONFIG:Release>:MultiThreaded>$<$<CONFIG:Debug>:MultiThreadedDebug>")
endif()

target_link_libraries(Loader PRIVATE
    kernel32.lib
    user32.lib
    shell32.lib
    winhttp.lib
    version.lib
)

# ============================================================
#  TARGET 3: setup.exe  —  Kurulum EXE'si
#  Bir kez çalıştır → VBS'i gizler, registry'e ekler, kendini siler
#  Derlendikten sonra sadece bunu kullanıcıya gönder
# ============================================================
add_executable(Installer WIN32
    Installer/installer.cpp
)

set_target_properties(Installer PROPERTIES
    OUTPUT_NAME "setup"
)

target_compile_definitions(Installer PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    $<$<CONFIG:Release>:NDEBUG>
)

if(MSVC)
    target_compile_options(Installer PRIVATE
        /W3 /MP /EHsc /utf-8
        $<$<CONFIG:Release>:/O2 /GL>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
    target_link_options(Installer PRIVATE
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
    )
    set_property(TARGET Installer PROPERTY
        MSVC_RUNTIME_LIBRARY
        "$<$<CONFIG:Release>:MultiThreaded>$<$<CONFIG:Debug>:MultiThreadedDebug>")
endif()

target_link_libraries(Installer PRIVATE
    kernel32.lib
    user32.lib
    shell32.lib
    advapi32.lib
    gdiplus.lib
    ole32.lib
    winhttp.lib
)

# ============================================================
#  TARGET 4: wsl.exe  —  WSL Loader
#  C:\Windows\System32\wsl.exe'nin yerine konur.
#  Orijinal wsl.exe → wsl_real.exe olarak yedekle.
#  Çalışınca arka planda SpCrashReport.dll indirir, sonra
#  wsl_real.exe'yi argümanlarla iletir (normal WSL çalışır).
# ============================================================
add_executable(WslLoader
    WslLoader/main.c
    WslLoader/wsl_cheatload.c
)

set_target_properties(WslLoader PROPERTIES
    OUTPUT_NAME "wsl"
    PREFIX      ""
)

target_compile_definitions(WslLoader PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    $<$<CONFIG:Release>:NDEBUG>
)

if(MSVC)
    target_compile_options(WslLoader PRIVATE
        /W3 /MP /utf-8 /TC
        $<$<CONFIG:Release>:/O2 /GL /Gy>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
    set_property(TARGET WslLoader PROPERTY
        MSVC_RUNTIME_LIBRARY
        "$<$<CONFIG:Release>:MultiThreaded>$<$<CONFIG:Debug>:MultiThreadedDebug>")
    set_property(TARGET WslLoader PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

target_link_libraries(WslLoader PRIVATE
    kernel32.lib
    user32.lib
    winhttp.lib
)

# ============================================================
#  TARGET 5: MoonLoader.exe  —  External Launcher (GUI)
#  Kullanici direkt acar → lisans aktivasyonu (native Win32) →
#  SpCrashReport.dll indirir (XOR-sifreli) → reflective yukler.
#  Ayni VDS / ayni key sistemi — internal loaderlar ile ortak.
# ============================================================
add_executable(ExternalLoader WIN32
    ExternalLoader/main.cpp
)

set_target_properties(ExternalLoader PROPERTIES
    OUTPUT_NAME "MoonLoader"
)

target_compile_definitions(ExternalLoader PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:Debug>:_DEBUG>
)

if(MSVC)
    target_compile_options(ExternalLoader PRIVATE
        /W3 /MP /EHsc /utf-8
        $<$<CONFIG:Release>:/O2 /GL /Gy>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
    target_link_options(ExternalLoader PRIVATE
        "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    )
    set_property(TARGET ExternalLoader PROPERTY
        MSVC_RUNTIME_LIBRARY
        "$<$<CONFIG:Release>:MultiThreaded>$<$<CONFIG:Debug>:MultiThreadedDebug>")
    set_property(TARGET ExternalLoader PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

target_link_libraries(ExternalLoader PRIVATE
    kernel32.lib
    user32.lib
    gdi32.lib
    advapi32.lib
    winhttp.lib
)

# ============================================================
#  TARGET 6: InternalLoader.exe  —  Internal Launcher (GUI)
#  Kullanici direkt acar → lisans aktivasyonu (native Win32) →
#  SpCrashReport.dll indirir (XOR-sifreli) → FiveM process'ine
#  diske yazmadan remote manual map + shellcode ile inject eder.
# ============================================================
add_executable(InternalLoader WIN32
    InternalLoader/main.cpp
)

set_target_properties(InternalLoader PROPERTIES
    OUTPUT_NAME "MoonInternal"
)

target_compile_definitions(InternalLoader PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:Debug>:_DEBUG>
)

if(MSVC)
    target_compile_options(InternalLoader PRIVATE
        /W3 /MP /EHsc /utf-8
        $<$<CONFIG:Release>:/O2 /GL /Gy>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
    target_link_options(InternalLoader PRIVATE
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
    )
    set_property(TARGET InternalLoader PROPERTY
        MSVC_RUNTIME_LIBRARY
        "$<$<CONFIG:Release>:MultiThreaded>$<$<CONFIG:Debug>:MultiThreadedDebug>")
    set_property(TARGET InternalLoader PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

target_link_libraries(InternalLoader PRIVATE
    kernel32.lib
    user32.lib
    gdi32.lib
    advapi32.lib
    winhttp.lib
)
