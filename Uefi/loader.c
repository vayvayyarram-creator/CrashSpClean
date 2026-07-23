// ============================================================
//  loader.efi  —  Gerçek UEFI Boot Application
//
//  Yaptığı tek şey:
//    1. NVRAM'a "CheatRdy = 1" yazar (EFI_VARIABLE_RUNTIME_ACCESS)
//       Bu variable Windows çalışırken de okunabilir.
//    2. \EFI\Microsoft\Boot\bootmgfw.efi'ye chain eder.
//       Windows normalde açılır, hiçbir fark yokmuş gibi.
//
//  Windows tarafında installer.exe (SYSTEM hakkı ile çalışır)
//  bu variable'ı okur → CheatRdy==1 → FiveM inject → var=0
//  FiveM kapanırsa var=0 olduğu için re-inject yok → restart gerekir.
//
//  Build: EDK2 toolchain (aşağıda build.txt'e bakın)
//  GUID: {DEADBEEF-1234-5678-ABCD-EF0123456789}  (rastgele seçildi)
// ============================================================

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DevicePath.h>

// ── NVRAM variable tanımları ──────────────────────────────────────────────────
// Bu GUID Windows tarafında GetFirmwareEnvironmentVariableExW için kullanılır.
// Format (Windows): {DEADBEEF-1234-5678-ABCD-EF0123456789}
static EFI_GUID gCheatGuid = {
    0xDEADBEEF,
    0x1234,
    0x5678,
    { 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89 }
};

// Variable ismi — Windows tarafıyla eşleşmeli
static CHAR16 gVarName[] = L"CheatRdy";

// ── Attribute bitmask ─────────────────────────────────────────────────────────
// NON_VOLATILE : reboot'lar arasında kalıcı (NVRAM'da)
// BOOTSERVICE_ACCESS: EFI boot services aşamasında okunabilir
// RUNTIME_ACCESS    : OS çalışırken (Windows) de okunabilir — kritik!
#define CHEAT_ATTRS  (EFI_VARIABLE_NON_VOLATILE          | \
                      EFI_VARIABLE_BOOTSERVICE_ACCESS     | \
                      EFI_VARIABLE_RUNTIME_ACCESS)

// ── Windows bootmgr'ye chain ──────────────────────────────────────────────────
// loader.efi çalıştıktan sonra Windows'u normal şekilde başlatır.
// Kullanıcı hiçbir şey fark etmez.
static EFI_STATUS ChainToWindows(EFI_HANDLE ImageHandle) {
    EFI_STATUS Status;

    // Kendi device path'imizi al (hangi diskten yüklendik?)
    EFI_LOADED_IMAGE_PROTOCOL* Li = NULL;
    Status = gBS->HandleProtocol(ImageHandle,
                                 &gEfiLoadedImageProtocolGuid,
                                 (VOID**)&Li);
    if (EFI_ERROR(Status)) return Status;

    // Aynı diskte \EFI\Microsoft\Boot\bootmgfw.efi yolunu inşa et
    static CHAR16 BmPath[] = L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
    EFI_DEVICE_PATH_PROTOCOL* DevPath = FileDevicePath(Li->DeviceHandle, BmPath);
    if (DevPath == NULL) return EFI_NOT_FOUND;

    // bootmgfw.efi'yi yükle
    EFI_HANDLE NewHandle = NULL;
    Status = gBS->LoadImage(FALSE, ImageHandle, DevPath, NULL, 0, &NewHandle);
    FreePool(DevPath);
    if (EFI_ERROR(Status)) return Status;

    // Başlat — bu fonksiyon Windows açılana kadar (ya da hata olana kadar) dönmez
    return gBS->StartImage(NewHandle, NULL, NULL);
}

// ── Ana giriş noktası ─────────────────────────────────────────────────────────
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    // ── Adım 1: Önceki boot'tan kalan stale variable'ı temizle ──────────────
    // SetVariable(size=0) = delete.
    // Hata yoksay — zaten yoksa EFI_NOT_FOUND döner.
    gRT->SetVariable(gVarName, &gCheatGuid, CHEAT_ATTRS, 0, NULL);

    // ── Adım 2: "CheatRdy = 1" yaz ──────────────────────────────────────────
    // Windows tarafı bu değeri okur → 1 ise inject eder.
    // Inject sonrası Windows tarafı bunu 0'a çeker → one-shot garanti.
    UINT8 Ready = 1;
    EFI_STATUS St = gRT->SetVariable(
        gVarName,
        &gCheatGuid,
        CHEAT_ATTRS,
        sizeof(UINT8),
        &Ready
    );
    // SetVariable başarısız olsa bile chain'e devam et —
    // injection olmaz ama Windows normal açılır (kullanıcı fark etmez).
    (VOID)St;

    // ── Adım 3: Windows'u başlat ─────────────────────────────────────────────
    St = ChainToWindows(ImageHandle);

    // Chain başarısız oldu (nadiren) → EFI fallback boot entry devreye girer
    return St;
}
