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

; Request application privileges for Windows Vista
RequestExecutionLevel user

SetCompressor zlib

Icon "${NSISDIR}/Contrib/Graphics/Icons/box-install.ico"
XPStyle on

;--------------------------------

; Pages

Page license
Page directory
;Page components
Page instfiles

;--------------------------------

; The stuff to install
Section "" ;No components page, name is not important

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR

  File LEGAL.txt
  File /r out/*

  CreateShortCut "$DESKTOP\${LONGNAME}.lnk" "$INSTDIR\${SHORTNAME}.exe"

SectionEnd ; end the section

