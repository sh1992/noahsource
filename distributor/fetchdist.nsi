;
; fetchdist.nsi - Download and run Windows distclient Installer
;
; Built with NSIS.
;

!define LONGNAME "Thesis@home Distributed Computing Client"
!define SHORTNAME "fetchdist"
!define HOSTNAME "ncf1333.network.ncf.edu"

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
    Delete "$OUTDIR\distclient.exe"
    ClearErrors
    NSISdl::download http://${HOSTNAME}:9990/spec/distclient.exe "$OUTDIR\distclient.exe"

    SetDetailsPrint both
    DetailPrint "Installing..."
    SetDetailsPrint listonly
    ExecWait '"$OUTDIR\distclient.exe"' $0

    IfErrors errornotify
    SetAutoClose true
    Goto noerror
errornotify:
    SetDetailsPrint both
    DetailPrint "An error occurred."
    SetDetailsPrint listonly

noerror:
    Delete "$OUTDIR\distclient.exe"
SectionEnd ; end the section

