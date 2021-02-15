/*****************************************************************************/
/* Socket.h                               Copyright (c) Ladislav Zezula 2021 */
/*---------------------------------------------------------------------------*/
/* MIME parsing functions for CascLib                                        */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 13.02.21  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef __SOCKET_H__
#define __SOCKET_H__

//-----------------------------------------------------------------------------
// Defines

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (SOCKET)(-1)
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR   (-1)
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define CASC_PORT_HTTP      80
#define CASC_PORT_RIBBIT    1119

//-----------------------------------------------------------------------------
// The CASC_SOCKET class

typedef class CASC_SOCKET_CACHE * PCASC_SOCKET_CACHE;
typedef class CASC_SOCKET * PCASC_SOCKET;
typedef struct addrinfo * PADDRINFO;

class CASC_SOCKET
{
    public:

    char * ReadResponse(const char * request, size_t request_length = 0, size_t * PtrLength = NULL);
    DWORD AddRef();
    void Release();

    private:

    // Constructor and destructor
    static PCASC_SOCKET New(addrinfo * remoteList, addrinfo * remoteItem, const char * hostName, unsigned portNum, SOCKET sock);
    static PCASC_SOCKET Connect(const char * hostName, unsigned portNum);
    static SOCKET CreateAndConnect(SOCKET old_sock, addrinfo * remoteItem);
    static DWORD GetAddrInfo(const char * hostName, unsigned portNum, PADDRINFO hints, PADDRINFO * ppResult);
    static bool NeedReconnect();

    // Frees all resources and deletes the socket
    size_t DecodeValueInt32(const char * string, const char * string_end);
    bool IsHttpResponseComplete(const char * response, size_t response_length);
    void Delete();

    // Entities allowed to manipulate with the class
    friend CASC_SOCKET * sockets_connect(const char * hostName, unsigned portNum);
    friend char * sockets_read_response(PCASC_SOCKET pSocket, const char * request, size_t request_length, size_t * PtrLength);
    friend class CASC_SOCKET_CACHE;

    PCASC_SOCKET_CACHE pCache;          // Pointer to the cache. If NULL, the socket is not cached
    PCASC_SOCKET pPrev;                 // Pointer to the prev socket in the list
    PCASC_SOCKET pNext;                 // Pointer to the next socket in the list
    PADDRINFO remoteList;               // List of the remote host informations
    PADDRINFO remoteItem;               // The particular host picked during the last connection attempt
    CASC_LOCK Lock;                     // Lock for single threaded access
    SOCKET sock;                        // Opened and connected socket
    DWORD dwRefCount;                   // Number of references
    DWORD portNum;                      // Port number
    char hostName[1];                   // Buffer for storing remote host (variable length)
};

//-----------------------------------------------------------------------------
// Socket cache class

class CASC_SOCKET_CACHE
{
    public:

    CASC_SOCKET_CACHE();
    ~CASC_SOCKET_CACHE();

    PCASC_SOCKET Find(const char * hostName, unsigned portNum);
    PCASC_SOCKET InsertSocket(PCASC_SOCKET pSocket);
    void UnlinkSocket(PCASC_SOCKET pSocket);
    
    void SetCaching(bool bAddRef);
    void PurgeAll();

    private:

    PCASC_SOCKET pFirst;
    PCASC_SOCKET pLast;
    DWORD dwRefCount;
};

//-----------------------------------------------------------------------------
// Public functions

PCASC_SOCKET sockets_connect(const char * hostName, unsigned portNum);
void sockets_set_caching(bool caching);

#endif  // __SOCKET_H__
