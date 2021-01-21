/*****************************************************************************/
/* Sockets.cpp                            Copyright (c) Ladislav Zezula 2021 */
/*---------------------------------------------------------------------------*/
/* Socket functions for CascLib                                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 21.01.21  1.00  Lad  Created                                              */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

//-----------------------------------------------------------------------------
// Local functions

DWORD ribbit_to_raw(LPBYTE server_response, size_t total_received, QUERY_KEY & FileData)
{
    FILE * fp = fopen("E:\\ribbit_response.txt", "wt");

    fwrite(server_response, 1, total_received, fp);
    fclose(fp);
    return ERROR_NETWORK_NOT_AVAILABLE;
}

//-----------------------------------------------------------------------------
// Public functions

DWORD sockets_initialize()
{
#ifdef PLATFORM_WINDOWS
    // Windows-specific initialize function
    WSADATA wsd;
    if(WSAStartup(MAKEWORD(2, 2), &wsd) != 0)
        return ERROR_CAN_NOT_COMPLETE;
#endif

    return ERROR_SUCCESS;
}

CASC_SOCKET sockets_remote_connect(CASC_REMOTE_INFO & RemoteInfo, const char * szHostName, const char * szHostPort)
{
    // Do we already have a connection?
    if(RemoteInfo.remoteList == NULL)
    {
        addrinfo hints = {0};

        // Retrieve the incormation about the remote host
        // This will fail immediately if there is no connection to the internet
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if(getaddrinfo(szHostName, szHostPort, &hints, &RemoteInfo.remoteList) != 0)
            return ERROR_NETWORK_NOT_AVAILABLE;
    }

    // If we already have remote entry, then we use that entry.
    // Otherwise, we try any item from the entry list until we succeed
    if(RemoteInfo.remoteHost == NULL)
    {
        addrinfo * remoteHost;
        SOCKET sock;

        // Try to connect to any address provided by the getaddrinfo()
        for(remoteHost = RemoteInfo.remoteList; remoteHost != NULL; remoteHost = remoteHost->ai_next)
        {
            // Windows: returns INVALID_SOCKET (0) on error
            // Linux: return -1 on error
            if((sock = socket(remoteHost->ai_family, remoteHost->ai_socktype, remoteHost->ai_protocol)) > 0)
            {
                // Windows: Returns 0 on success, SOCKET_ERROR (-1) on failure
                // Linux: Returns 0 on success, (-1) on failure
                if(connect(sock, remoteHost->ai_addr, remoteHost->ai_addrlen) == 0)
                {
                    // Remember the remote host entry for the next connection
                    RemoteInfo.remoteHost = remoteHost;
                    return sock;
                }
                
                // Close the socket and try again
                closesocket(sock);
            }
        }
    }
    else
    {
        // Reuse already known host info
        addrinfo * remoteHost = RemoteInfo.remoteHost;
        SOCKET sock;

        // Create a socked to a known host
        if((sock = socket(remoteHost->ai_family, remoteHost->ai_socktype, remoteHost->ai_protocol)) > 0)
        {
            if(connect(sock, remoteHost->ai_addr, remoteHost->ai_addrlen) == 0)
            {
                return sock;
            }
        }
    }

    return ERROR_NETWORK_NOT_AVAILABLE;
}

DWORD sockets_read_response(CASC_SOCKET sock, CASC_RESPONSE_TYPE ResponseType, QUERY_KEY & FileData)
{
    LPBYTE server_response = NULL;
    size_t bytes_received = 0;
    size_t total_received = 0;
    size_t block_increment = 0x1000;
    size_t buffer_size = block_increment;
    DWORD dwErrCode;

    // Allocate buffer for server response
    if((server_response = CASC_ALLOC<BYTE>(buffer_size)) != NULL)
    {
        for(;;)
        {
            // Reallocate the buffer size, if needed
            if(total_received == buffer_size)
            {
                if((server_response = CASC_REALLOC(BYTE, server_response, buffer_size + block_increment)) == NULL)
                    return NULL;
                buffer_size += block_increment;
                block_increment *= 2;
            }

            // Receive the next part of the response, up to buffer size
            if((bytes_received = recv(sock, (char *)(server_response + total_received), (int)(buffer_size - total_received), 0)) <= 0)
                break;
            total_received += bytes_received;
        }
    }

    // Parse the response and convert it to plain data
    if(server_response && total_received)
    {
        switch(ResponseType)
        {
            case ResponseRaw:
                FileData.pbData = server_response;
                FileData.cbData = total_received;
                return ERROR_SUCCESS;

            case ResponseRibbit:
                dwErrCode = ribbit_to_raw(server_response, total_received, FileData);
                CASC_FREE(server_response);
                return dwErrCode;

            case ResponseHttp:
                assert(false);
                break;
        }
    }

    return ERROR_BAD_FORMAT;
}

void sockets_free()
{}


