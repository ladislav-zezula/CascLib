/*****************************************************************************/
/* Sockets.h                              Copyright (c) Ladislav Zezula 2021 */
/*---------------------------------------------------------------------------*/
/* Socket functions for CascLib                                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 21.01.21  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef __SOCKETS_H__
#define __SOCKETS_H__

//-----------------------------------------------------------------------------
// Enums and structures

typedef enum _CASC_RESPONSE_TYPE
{
    ResponseRaw = 0,
    ResponseRibbit = 1,
    ResponseHttp = 2
} CASC_RESPONSE_TYPE;

typedef struct _CASC_REMOTE_INFO
{
    struct addrinfo * remoteList;                   // List of the remote host informations
    struct addrinfo * remoteHost;                   // The particular host picked during the last connection attempt

} CASC_REMOTE_INFO, *PCASC_REMOTE_INFO;

// Structure for downloading a file using the Ribbit protocol
// https://wowdev.wiki/Ribbit
typedef struct _CASC_RIBBIT_DOWNLOAD
{
    LPCSTR szHostName;                              // Address of the remote CDN server. ("us.version.battle.net" or "eu.version.battle.net")
    LPCSTR szProduct;                               // Product codename
    LPCSTR szFile;                                  // File name to download ("versions" or "cdns")
    LPBYTE pbFileData;                              // [out] downloaded file
    size_t cbFileData;                              // [out] length of the downloaded file

} CASC_RIBBIT_DOWNLOAD, *PCASC_RIBBIT_DOWNLOAD;

//-----------------------------------------------------------------------------
// Scanning a directory

// Initialize sockets library
DWORD sockets_initialize();
CASC_SOCKET sockets_remote_connect(CASC_REMOTE_INFO & RemoteInfo, const char * szHostName, const char * szHostPort);
DWORD sockets_read_response(CASC_SOCKET sock, CASC_RESPONSE_TYPE ResponseType, QUERY_KEY & FileData);
void  sockets_free();

#endif // __SOCKETS_H__
