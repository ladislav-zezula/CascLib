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

enum MIME_PHASE
{
    MimePhaseHeader = 0,
    MimePhaseDataHeader,
    MimePhaseContent
};

enum BOUNDARY_PHASE
{
    BoundaryNone = 0,
    BoundaryBegin,
    BoundaryEnd
};

// Check whether the next line in the MIME file is a boundary
static BOUNDARY_PHASE CheckForBoundary(const char * szColumn, const char * szBoundary, size_t nBoundaryLength)
{
    if(szColumn[0] == '-' && szColumn[1] == '-')
    {
        if(!strncmp(szColumn + 2, szBoundary, nBoundaryLength))
        {
            // Check for end of the boundary
            if(szColumn[2 + nBoundaryLength] == '-' && szColumn[2 + nBoundaryLength + 1] == '-')
                return BoundaryEnd;
            else
                return BoundaryBegin;
        }
    }

    return BoundaryNone;
}

static BOUNDARY_PHASE CheckForBoundary(CASC_CSV & MimeFile, const char * szBoundary, size_t nBoundaryLength)
{
    BOUNDARY_PHASE BoundaryPhase = BoundaryNone;

    // Load one line of the MIME file
    if(MimeFile.LoadNextLine())
    {
        const CASC_CSV_COLUMN & Column = MimeFile[CSV_ZERO][CSV_ZERO];
        const char * szColumn = Column.szValue;

        BoundaryPhase = CheckForBoundary(szColumn, szBoundary, nBoundaryLength);
    }

    return BoundaryPhase;
}

// End-of_line finder for MIME data. We are looking strictly for \x0D\x0A
static char * NextLine_Mime(void * pvUserData, char * szLine)
{
    const char * szBoundary = (const char *)pvUserData;

    // Do we have the end of the boundary?
    if(szBoundary == NULL)
    {
        // Keep processing until the end
        while(szLine[0] != 0)
        {
            // Strictly check for MIME new line
            if(szLine[0] == 0x0D && szLine[1] == 0x0A)
            {
                szLine[0] = 0;
                szLine[1] = 0;
                return szLine + 2;
            }

            // Next character, please
            szLine++;
        }
    }
    else
    {
        // Calculate the length of the boundary
        BOUNDARY_PHASE BPhase;
        size_t nBoundaryLength = strlen(szBoundary);
        size_t nFillLength;

        // Keep processing until we find the sequence of EOL+Boundary
        while(szLine[0] != 0)
        {
            // Strictly check for MIME new line
            if(szLine[0] == 0x0D && szLine[1] == 0x0A && (BPhase = CheckForBoundary(szLine + 2, szBoundary, nBoundaryLength)) != BoundaryNone)
            {
                nFillLength = 4 + nBoundaryLength + ((BPhase == BoundaryEnd) ? 4 : 2);

                memset(szLine, 0, nFillLength);
                return szLine + nFillLength;
            }

            // Next character, please
            szLine++;
        }
    }

    return NULL;
}

// There is no next column for Ribbit files. The entire line is a single column
static char * NextColumn_Mime(void * /* pvUserData */, char * /* szColumn */)
{
    return NULL;
}

static size_t ExtractBoundary(char * szBuffer, size_t cchBuffer, const char * szBoundaryPtr)
{
    const char * szBoundaryEnd = strchr(szBoundaryPtr, '\"');

    // Find the end of the boundary text
    if((szBoundaryEnd = strchr(szBoundaryPtr, '\"')) == NULL)
        return 0;
    
    // Check buffer size
    if((size_t)(szBoundaryEnd - szBoundaryPtr) >= cchBuffer)
        return 0;

    // Copy the boundary
    memcpy(szBuffer, szBoundaryPtr, szBoundaryEnd - szBoundaryPtr);
    szBuffer[szBoundaryEnd - szBoundaryPtr] = 0;
    return (szBoundaryEnd - szBoundaryPtr);
}

static DWORD mime_to_raw(LPBYTE server_response, size_t total_received, QUERY_KEY & FileData)
{
    CASC_CSV MimeFile(0, false);
    MIME_PHASE nPhase = MimePhaseHeader;
    size_t nBoundaryLength = 0;
    DWORD dwErrCode;
    char szBoundary[0x80];

    //if((fp = fopen("E:\\ribbit_response.txt", "wb")) != NULL)
    //{
    //    fwrite(server_response, 1, total_received, fp);
    //    fclose(fp);
    //}

    // Initialize the parser to detect Ribbit newlines
    MimeFile.SetNextLineProc(NextLine_Mime, NextColumn_Mime);
    szBoundary[0] = 0;

    // Set the Ribbit parser
    dwErrCode = MimeFile.Load(server_response, total_received);
    if(dwErrCode == ERROR_SUCCESS)
    {
        // 1) Keep loading lines until we find boundary
        if(nPhase == MimePhaseHeader)
        {
            while(MimeFile.LoadNextLine())
            {
                const CASC_CSV_COLUMN & Column = MimeFile[CSV_ZERO][CSV_ZERO];
                const char * szBoundaryPtr;

                // Get the boundary
                if(szBoundary[0] == 0 && (szBoundaryPtr = strstr(Column.szValue, "boundary=\"")) != NULL)
                {
                    nBoundaryLength = ExtractBoundary(szBoundary, _countof(szBoundary), szBoundaryPtr + 10);
                    continue;
                }

                // Is the line the boundary one?
                if(Column.nLength == 0 && CheckForBoundary(MimeFile, szBoundary, nBoundaryLength) == BoundaryBegin)
                {
                    nPhase = MimePhaseDataHeader;
                    break;
                }
            }
        }

        // 2) If we are at the boundary pointer, keep reading until we find an empty line
        if(nPhase == MimePhaseDataHeader)
        {
            while(MimeFile.LoadNextLine())
            {
                const CASC_CSV_COLUMN & Column = MimeFile[CSV_ZERO][CSV_ZERO];

                // Is that an empty line?
                if(Column.nLength == 0)
                {
                    MimeFile.SetNextLineProc(NextLine_Mime, NextColumn_Mime, szBoundary);
                    nPhase = MimePhaseContent;
                    break;
                }
            }
        }

        // 3) If we are loading the MIME content, 
        if(nPhase == MimePhaseContent)
        {
            if(MimeFile.LoadNextLine())
            {
                // Get the single "line" from the MIME file
                const CASC_CSV_COLUMN & Column = MimeFile[CSV_ZERO][CSV_ZERO];

                // Give the "line" to the caller
                return FileData.SetData(Column.szValue, Column.nLength);
            }
        }

        // The format of the file was not recognized
        dwErrCode = ERROR_BAD_FORMAT;
    }

    return dwErrCode;
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
        CASC_SOCKET sock;

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
        CASC_SOCKET sock;

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

    // Allocate buffer for server response. Allocate one extra byte for zero terminator
    if((server_response = CASC_ALLOC<BYTE>(buffer_size)) != NULL)
    {
        for(;;)
        {
            // Reallocate the buffer size, if needed
            if(total_received == buffer_size)
            {
                if((server_response = CASC_REALLOC(BYTE, server_response, buffer_size + block_increment)) == NULL)
                    return ERROR_NOT_ENOUGH_MEMORY;
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
        // Convert the response into raw data based on response type
        switch(ResponseType)
        {
            case ResponseRaw:
                FileData.pbData = server_response;
                FileData.cbData = total_received;
                return ERROR_SUCCESS;

            case ResponseRibbit:
                dwErrCode = mime_to_raw(server_response, total_received, FileData);
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


