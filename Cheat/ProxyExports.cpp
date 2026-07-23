// ============================================================
//  ProxyExports.cpp — version.dll re-export
//
//  version.lib zaten link ediliyor (CMakeLists.txt).
//  Burasi sadece o sembolleri DLL'imizden dis dunyaya aciyor.
//  Hicbir fonksiyon yeniden tanimlanmiyor — cakisma yok.
// ============================================================

// version.lib'deki sembolleri DLL export tablosuna ekle
#pragma comment(linker, "/export:GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoW")
#pragma comment(linker, "/export:GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW")
#pragma comment(linker, "/export:VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW")
#pragma comment(linker, "/export:VerFindFileA")
#pragma comment(linker, "/export:VerFindFileW")
#pragma comment(linker, "/export:VerInstallFileA")
#pragma comment(linker, "/export:VerInstallFileW")
#pragma comment(linker, "/export:VerLanguageNameA")
#pragma comment(linker, "/export:VerLanguageNameW")
