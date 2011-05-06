/*
 * wrapper.c - C wrapper around perl to prevent Win32 console subprocesses
 *             (like ga-spectroscopy-client and SPCAT) from showing windows.
 */

#include <windows.h>

#define CMDLINE "\".\\perl.exe\" \".\\distclientwx.pl\""

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  TCHAR szPath[MAX_PATH], *buf;
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  int rc = GetModuleFileName(NULL, szPath, MAX_PATH), error = 0;
  if ( rc == 0 || rc >= MAX_PATH ) error = 1;
  if ( !error ) buf = strrchr(szPath, '\\');
  if ( buf == NULL ) error = 1;
  if ( !error ) buf[0] = 0;
  if ( error ) {
    MessageBox(NULL, "Cannot locate executable.", "", MB_ICONSTOP|MB_OK);
    exit(1);
  }
  if ( chdir(szPath) ) {
    MessageBox(NULL, "Cannot set working directory.", "", MB_ICONSTOP|MB_OK);
    exit(1);
  }

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));
  // Start the child process.
  rc = CreateProcess(NULL,    // No module name (use command line)
                     CMDLINE, // Command line
                     NULL,    // Process handle not inheritable
                     NULL,    // Thread handle not inheritable
                     FALSE,   // Set handle inheritance to FALSE
                     CREATE_NO_WINDOW,
                     NULL,    // Use parent's environment block
                     NULL,    // Use parent's starting directory
                     &si,     // Pointer to STARTUPINFO structure
                     &pi);    // Pointer to PROCESS_INFORMATION structure
  if ( !rc ) {
    MessageBox(NULL, "Cannot create process.", "", MB_ICONSTOP|MB_OK);
    exit(1);
  }
  // Close process and thread handles. 
  CloseHandle( pi.hProcess );
  CloseHandle( pi.hThread );
  return 0;
}
