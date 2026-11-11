# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The Windows setup .exe of Yantra (NFR-09), made on Linux with makensis
# by packaging/windows/package_windows.py from the same staged folder as
# the portable zip. Per-user install (no administrator rights; Yantra's
# settings and data are per user too, %APPDATA%/yantra): the program in
# %LOCALAPPDATA%\Programs\Yantra, a Start menu entry, the entry in
# Settings > Apps with its uninstaller. Silent: setup.exe /S [/D=folder];
# uninstall.exe /S.
#
# Defines from package_windows.py: VERSION, VERSION4 (a.b.c.0), STAGE (the
# staged package folder), OUTFILE, ICON, SOURCE_URL, FILES_NSH (the
# uninstaller's file list), SIZE_KB.

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user
ManifestDPIAware true

!define APP_NAME "Yantra"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Yantra"
!define PUBLISHER "Curtat BV"
!define HOMEPAGE "https://curtat.be"

Name "${APP_NAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\Yantra"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
BrandingText "${APP_NAME} ${VERSION}"

VIProductVersion "${VERSION4}"
VIFileVersion "${VERSION4}"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APP_NAME} ${VERSION} setup"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "The Yantra contributors, GPL-3.0-or-later"
VIAddVersionKey "Comments" "${HOMEPAGE}, source: ${SOURCE_URL}"

!include "MUI2.nsh"
!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\yantra.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start Yantra"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}/LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Yantra" SecMain
  SectionIn RO
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /r "${STAGE}/*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortcut "$SMPROGRAMS\Yantra.lnk" "$INSTDIR\bin\yantra.exe" "" "$INSTDIR\bin\yantra.exe" 0
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\bin\yantra.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Contact" "info@curtat.be"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "${HOMEPAGE}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "HelpLink" "${HOMEPAGE}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Comments" "Source: ${SOURCE_URL}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" ${SIZE_KB}
SectionEnd

# Removes exactly the installed files (FILES_NSH), then the folders when
# empty: never a recursive delete of the install folder, which the user
# may have chosen with own files in it.
Section "Uninstall"
  SetShellVarContext current
  Delete "$SMPROGRAMS\Yantra.lnk"
  !include "${FILES_NSH}"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
SectionEnd
