; Radiant Core Windows Installer Script
; Requires NSIS (https://nsis.sourceforge.io/)

!define APPNAME "Radiant Core"
!define VERSION "2.0.0"
!define PUBLISHER "Radiant Core Developers"
!define URL "https://radiantblockchain.org"
!define EXECUTABLE "radiantd.exe"

; Include modern UI
!include "MUI2.nsh"

; General settings
Name "${APPNAME}"
OutFile "${APPNAME}-${VERSION}-win64-setup.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "Software\${APPNAME}" "InstallPath"
RequestExecutionLevel admin

; Interface settings
!define MUI_ABORTWARNING
!define MUI_ICON "contrib\icons\radiant.ico"
!define MUI_UNICON "contrib\icons\radiant.ico"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "COPYING"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; Languages
!insertmacro MUI_LANGUAGE "English"

; Installer sections
Section "Core Files" SecCore
    SectionIn RO
    
    SetOutPath "$INSTDIR"
    
    ; Main executables
    File "dist\radiantd.exe"
    File "dist\radiant-cli.exe"
    File "dist\radiant-tx.exe"
    
    ; Required DLLs
    File /r "dist\dlls\*.dll"
    
    ; Configuration files
    File "dist\radiant.conf"
    File "dist\README.txt"
    File "COPYING"
    
    ; Create data directory
    CreateDirectory "$APPDATA\Radiant"
    
    ; Create start menu shortcuts
    CreateDirectory "$SMPROGRAMS\${APPNAME}"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\radiantd.exe" "" "$INSTDIR\radiantd.exe" 0
    CreateShortCut "$SMPROGRAMS\${APPNAME}\Radiant CLI.lnk" "$INSTDIR\radiant-cli.exe" "" "$INSTDIR\radiant-cli.exe" 0
    CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
    
    ; Write uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    
    ; Write registry keys
    WriteRegStr HKLM "Software\${APPNAME}" "InstallPath" "$INSTDIR"
    WriteRegStr HKLM "Software\${APPNAME}" "Version" "${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayName" "${APPNAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "URLInfoAbout" "${URL}"
SectionEnd

Section "Start with Windows" SecAutoStart
    CreateShortCut "$SMSTARTUP\${APPNAME}.lnk" "$INSTDIR\radiantd.exe" "-daemon"
SectionEnd

Section "Add to PATH" SecPath
    ; Add to system PATH for command-line usage
    ${EnvVarUpdate} $0 "PATH" "A" "HKLM" "$INSTDIR"
SectionEnd

; Uninstaller section
Section "Uninstall"
    ; Remove files
    Delete "$INSTDIR\radiantd.exe"
    Delete "$INSTDIR\radiant-cli.exe"
    Delete "$INSTDIR\radiant-tx.exe"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\radiant.conf"
    Delete "$INSTDIR\README.txt"
    Delete "$INSTDIR\COPYING"
    Delete "$INSTDIR\Uninstall.exe"
    
    ; Remove directories
    RMDir "$INSTDIR"
    RMDir /r "$INSTDIR\dlls"
    
    ; Remove shortcuts
    Delete "$SMPROGRAMS\${APPNAME}\*.lnk"
    RMDir "$SMPROGRAMS\${APPNAME}"
    Delete "$SMSTARTUP\${APPNAME}.lnk"
    
    ; Remove registry entries
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
    DeleteRegKey HKLM "Software\${APPNAME}"
    
    ; Remove from PATH
    ${un.EnvVarUpdate} $0 "PATH" "R" "HKLM" "$INSTDIR"
SectionEnd

; Component descriptions
LangString DESC_SecCore ${LANG_ENGLISH} "Core Radiant files including daemon, CLI, and required libraries."
LangString DESC_SecAutoStart ${LANG_ENGLISH} "Start Radiant Core automatically when Windows starts."
LangString DESC_SecPath ${LANG_ENGLISH} "Add Radiant Core executables to system PATH for command-line usage."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} $(DESC_SecCore)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecAutoStart} $(DESC_SecAutoStart)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecPath} $(DESC_SecPath)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; Functions
Function .onInit
    ; Check if already installed
    ReadRegStr $R0 HKLM "Software\${APPNAME}" "InstallPath"
    StrCmp $R0 "" done
    
    MessageBox MB_YESNO "${APPNAME} is already installed. $\n$\nWould you like to uninstall the previous version?" IDYES uninst
    Abort
    
uninst:
    ClearErrors
    ExecWait '$R0\Uninstall.exe _?=$INSTDIR'
    
    IfErrors uninst_error done
    Goto done
    
uninst_error:
    MessageBox MB_OK "Error uninstalling previous version. Please uninstall manually and try again."
    Abort
    
done:
FunctionEnd

Function un.onInit
    MessageBox MB_YESNO "Are you sure you want to completely remove ${APPNAME}?" IDYES +2
    Abort
FunctionEnd
