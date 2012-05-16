;
; distclient.nsi - Installer for Windows build of Distributed Computing Client
;
; Built with NSIS.
;

;!define LONGNAME "${DISTCLIENT_SERVERNAME} Distributed Computing Client"
!define LONGNAME "Distributed Computing Client"

; The name of the installer
Name "${LONGNAME}"

; The file to write
OutFile "${SHORTNAME}.exe"

; Customizing the license page
LicenseText "Please review the license agreement before you continue.$\r$\n\
             If you accept all terms of the agreement, click I Agree."
LicenseData "LEGAL.txt"
; LicenseForceSelection checkbox
BrandingText "v${VERSION}"

; The default installation directory
InstallDir "$LOCALAPPDATA\${SHORTNAME}"

; Request application privileges for Windows 7 / Vista
RequestExecutionLevel user

SetCompressor /SOLID lzma

; Visual appearance (icons, progress bar style, etc.)
Icon "${NSISDIR}/Contrib/Graphics/Icons/box-install.ico"
UninstallIcon "${NSISDIR}/Contrib/Graphics/Icons/box-uninstall.ico"
InstProgressFlags smooth
XPStyle on

; Pages
Page license
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

; Install the distributed computing client
Section "${LONGNAME}"
    SetDetailsPrint both
    DetailPrint "Checking for installed components..."
    SetDetailsPrint listonly

    ; Use a new copy of the wrapper (in case we're upgrading an old client)
    InitPluginsDir
    File "/oname=$PLUGINSDIR\${SHORTNAME}.exe" "out-MSWin32/${SHORTNAME}.exe"
    ; Use the wrapper to quit the client
    ExecWait '"$PLUGINSDIR\${SHORTNAME}.exe" /Q' $0
    IntCmp $0 0 +1 +5 +5
    ; 4 Instructions
    SetDetailsPrint both
    DetailPrint "Preparing to install..."
    SetDetailsPrint listonly
    Sleep 2000

    ; Begin installing
    SetDetailsPrint both
    DetailPrint "Installing..."
    SetDetailsPrint listonly

    ; Set output path to the installation directory.
    SetOutPath $INSTDIR

    File LEGAL.txt
    File /r out-MSWin32/*

    WriteUninstaller "uninstall.exe"

    CreateShortCut "$DESKTOP\${LONGNAME}.lnk" "$INSTDIR\${SHORTNAME}.exe"

    ; Autostart the client
    SetDetailsPrint both
    DetailPrint "Starting ${LONGNAME}..."
    SetDetailsPrint listonly
    IfSilent +1 +3
    ExecShell "open" "$INSTDIR\${SHORTNAME}.exe" "" SW_SHOWMINIMIZED ; Silent
    Goto +2
    ExecShell "open" "$INSTDIR\${SHORTNAME}.exe" "" SW_SHOWDEFAULT ; Not silent
    SetAutoClose true
    Sleep 2000

    SetDetailsPrint both
SectionEnd ; end the section

; Stop disclient then remove it, its shortcut, and its temporary files.
Section "Uninstall"
    SetDetailsPrint both
    DetailPrint "Checking for installed components..."
    SetDetailsPrint listonly

    ; Use the wrapper to quit the client
    ExecWait '"$INSTDIR\${SHORTNAME}.exe" /Q' $0
    IntCmp $0 0 +1 +5 +5
    ; 4 Instructions
    SetDetailsPrint both
    DetailPrint "Preparing to uninstall..."
    SetDetailsPrint listonly
    Sleep 2000

    SetDetailsPrint both
    DetailPrint "Uninstalling..."
    SetDetailsPrint listonly

    DetailPrint "Uninstalling from $INSTDIR"
    ; Remove files
    RMDir /r "$INSTDIR\lib" ; RMDir is unsafe in general
    Delete "$INSTDIR\*.*"   ; Also pretty unsafe
    SetOutPath "$DESKTOP"
    RMDir "$INSTDIR"
    Delete "$DESKTOP\${LONGNAME}.lnk"

    SetDetailsPrint both
    DetailPrint "Removing temporary files..."
    SetDetailsPrint listonly

    DetailPrint "Removing temporary files from $TEMP"
    FindFirst $0 $1 "$TEMP\${SHORTNAME}-temp-*"
temploop:
    StrCmp $1 "" temploopdone
    IfFileExists "$TEMP\$1\*.*" +1 temploopnext ; Make sure it's a directory
    DetailPrint "Remove folder recursively: $TEMP\$1"
    SetDetailsPrint none ; Folders can be very big for malfunctioning clients
    RMDir /r "$TEMP\$1"
    SetDetailsPrint listonly
temploopnext:
    FindNext $0 $1
    Goto temploop
temploopdone:
    FindClose $0
    SetDetailsPrint both
SectionEnd

