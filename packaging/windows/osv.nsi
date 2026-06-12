; osv.nsi — NSIS installer for Obscura Safe Vault.
; Build (from the repo root, after a Release build):
;   makensis /DVERSION=x.y.z packaging\windows\osv.nsi
; Output: dist\ObscuraSafeVault-<version>-setup.exe
; (NSIS does not create missing output directories — make dist\ first.)

!ifndef VERSION
  !define VERSION "dev"
!endif

Name "Obscura Safe Vault"
OutFile "..\..\dist\ObscuraSafeVault-${VERSION}-setup.exe"
InstallDir "$LOCALAPPDATA\Programs\ObscuraSafeVault"
RequestExecutionLevel user
Unicode true
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
  SetOutPath "$INSTDIR"
  File "..\..\build\bin\Release\osv.exe"
  File /r "..\..\assets"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortcut "$SMPROGRAMS\Obscura Safe Vault.lnk" "$INSTDIR\osv.exe"
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\Obscura Safe Vault.lnk"
  RMDir /r "$INSTDIR\assets"
  Delete "$INSTDIR\osv.exe"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
