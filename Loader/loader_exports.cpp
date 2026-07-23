// ============================================================
//  loader_exports.cpp — version.dll proxy export'ları
//
//  Loader version.dll olarak çıkıyor.
//  Spotify gerçek version.dll fonksiyonlarını çağırırsa
//  bu export'lar sistem version.dll'e yönlendirir.
//  Spotify normal çalışmaya devam eder.
// ============================================================

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
