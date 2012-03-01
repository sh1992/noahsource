/*
 * wrapper.c - C wrapper around perl to prevent Win32 console subprocesses
 *             (like ga-spectroscopy-client and SPCAT) from showing windows.
 */

#define WINVER 0x0501
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unistd.h>
#include <ctype.h>

#define CMDLINE "\".\\perl.exe\" \"-I.\" \".\\distclientwx.pl\""
#define DISTCLIENT_PORT "29482"

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  TCHAR szPath[MAX_PATH], *buf;
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  char remotecmd = 0;
  int rc = GetModuleFileName(NULL, szPath, MAX_PATH), error = 0;
  if ( rc == 0 || rc >= MAX_PATH ) error = 1;
  if ( !error ) buf = strrchr(szPath, '\\');
  if ( buf == NULL ) error = 1;
  if ( !error ) buf[0] = 0;
  if ( error ) {
    MessageBox(NULL, "Cannot locate executable.", "", MB_ICONSTOP|MB_OK);
    return 1;
  }
  if ( chdir(szPath) ) {
    MessageBox(NULL, "Cannot set working directory.", "", MB_ICONSTOP|MB_OK);
    return 1;
  }

  // Communicate with a running distclient.
  if ( lpCmdLine[0] == '/' && isalpha(lpCmdLine[1]) )
    remotecmd = toupper(lpCmdLine[1]);
  {
    WSADATA wsaData;
    struct addrinfo *result = NULL, *rp = NULL, hints;
    SOCKET ConnectSocket = INVALID_SOCKET;

    // Initialize Winsock
    rc = WSAStartup(MAKEWORD(2,2), &wsaData);
    if ( rc != 0 ) {
      MessageBox(NULL, "WSAStartup failed.", "", MB_ICONSTOP|MB_OK);
      return 1;
    }

    // Prepare to connect to localhost
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    // Resolve the server address and port
    rc = getaddrinfo("localhost", DISTCLIENT_PORT, &hints, &result);
    if ( rc != 0 ) {
      MessageBox(NULL, "getaddrinfo failed.", "", MB_ICONSTOP|MB_OK);
      WSACleanup();
      return 1;
    }
    // Attempt to connect to each address returned by getaddrinfo
    for ( rp=result; rp != NULL; rp = rp->ai_next ) {
      // Create a SOCKET for connecting to server
      ConnectSocket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (ConnectSocket == INVALID_SOCKET) {
        continue;
      }
      // Connect to server.
      rc = connect(ConnectSocket, rp->ai_addr, (int)rp->ai_addrlen);
      if (rc != SOCKET_ERROR) break; // Connected successfully
      closesocket(ConnectSocket);
      ConnectSocket = INVALID_SOCKET;
    }
    freeaddrinfo(result);
    if ( ConnectSocket != INVALID_SOCKET ) {
      // We were able to connect to one of the addresses.
      // Try sending a message.
      char sendbuf[] = {remotecmd ? remotecmd : 'S', '\n', 0};
      rc = send(ConnectSocket, sendbuf, (int) strlen(sendbuf), 0);
      if ( rc != SOCKET_ERROR ) {
        /* We successfully sent a message */
        if ( remotecmd == 'Q' ) {
          /* Wait for the client to finish exiting */
          while ( recv(ConnectSocket, sendbuf, 1, 0) > 0 );
        }
        closesocket(ConnectSocket);
        WSACleanup();
        return 0;
      }
      // Send failed
    }
    // Cleanup WinSock
    WSACleanup();
  }
  if ( remotecmd ) return 1;

  // Start the process
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  /* Pass along our show command, so that shortcut settings work properly. */
  si.dwFlags |= STARTF_USESHOWWINDOW;
  si.wShowWindow = nCmdShow;
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
    return 1;
  }
  // Close process and thread handles. 
  CloseHandle( pi.hProcess );
  CloseHandle( pi.hThread );
  return 0;
}
