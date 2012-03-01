;
; fetchdist.nsi - Download and run Windows distclient Installer
;
; Built with NSIS.
;

!define LONGNAME "${DISTCLIENT_SERVERNAME} Distributed Computing Client"

; The name of the installer
Name "${LONGNAME}"

; The file to write
OutFile "${SHORTNAME}.exe"

BrandingText " "

; The default installation directory
InstallDir "$LOCALAPPDATA\distclient"

; Request application privileges for Windows 7 / Vista
RequestExecutionLevel user

SetCompressor /SOLID lzma

Icon "${NSISDIR}/Contrib/Graphics/Icons/box-install.ico"
InstProgressFlags smooth
XPStyle on
Page instfiles

; The stuff to install
Section "" ;No components page, name is not important
    DetailPrint "Downloading..."
    SetDetailsPrint listonly
    ; TODO: Use FindWindow and SendMessage to quit existing instance

    ; Set output path to the installation directory.
    InitPluginsDir
    SetOutPath $PLUGINSDIR
    Delete "$OUTDIR\setup.exe"
    ClearErrors
    StrCpy $0 "http://${DISTCLIENT_HOST}:9990/spec/distclient.exe"
    DetailPrint "Using $0"
    NSISdl::download /TRANSLATE "Downloading..." "Connecting..." "second" \
        "minute" "hour" "s" "%dkB (%d%%) of %ukB @ %d.%01dkB/s" \
        " (%d %s%s remaining)" "$0" "$OUTDIR\setup.exe" 
    Pop $1
    StrCmp $1 "cancel" +1 +3
    SetAutoClose true   ; If cancel
    Goto noerror
    StrCmp $1 "success" +3 +1
    DetailPrint "Download error: $1"
    Goto errornotify

    SetDetailsPrint both
    DetailPrint "Installing..."
    SetDetailsPrint listonly
    ExecWait '"$OUTDIR\setup.exe"' $0

    IfErrors errornotify
    SetAutoClose true
    Goto noerror
errornotify:
    SetDetailsPrint both
    DetailPrint "An error occurred."
    SetDetailsPrint listonly

noerror:
    Delete "$OUTDIR\setup.exe"
SectionEnd ; end the section

