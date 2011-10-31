;
; distclient.nsi - Installer for Windows build of Distributed Computing Client
;
; Built with NSIS.
;

!define LONGNAME "Thesis@home Distributed Computing Client"
!define SHORTNAME "distclient"

; The name of the installer
Name "${LONGNAME}"

; The file to write
OutFile "${SHORTNAME}.exe"

LicenseData "LEGAL.txt"
; LicenseForceSelection checkbox
BrandingText " "

; The default installation directory
InstallDir "$LOCALAPPDATA\${SHORTNAME}"

; Request application privileges for Windows 7 / Vista
RequestExecutionLevel user

SetCompressor /SOLID lzma

Icon "${NSISDIR}/Contrib/Graphics/Icons/box-install.ico"
InstProgressFlags smooth
XPStyle on

;--------------------------------

; Pages

Page license
;Page directory
;Page components
Page instfiles

;--------------------------------

; The stuff to install
Section "" ;No components page, name is not important
    DetailPrint "Installing..."
    SetDetailsPrint listonly
    ; TODO: Use FindWindow and SendMessage to quit existing instance

    ; Set output path to the installation directory.
    SetOutPath $INSTDIR

    File LEGAL.txt
    File /r out-MSWin32/*

    CreateShortCut "$DESKTOP\${LONGNAME}.lnk" "$INSTDIR\${SHORTNAME}.exe"

    SetDetailsPrint both
SectionEnd ; end the section

