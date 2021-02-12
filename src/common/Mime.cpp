/*****************************************************************************/
/* Mime.cpp                               Copyright (c) Ladislav Zezula 2021 */
/*---------------------------------------------------------------------------*/
/* Mime functions for CascLib                                                */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 21.01.21  1.00  Lad  Created                                              */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

//-----------------------------------------------------------------------------
// Local variables

#define BASE64_INVALID_CHAR 0xFF
#define BASE64_WHITESPACE_CHAR 0xFE

static const char * CascBase64Table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static unsigned char CascBase64ToBits[0x80] = {0};

//-----------------------------------------------------------------------------
// The MIME blob class

CASC_MIME_BLOB::CASC_MIME_BLOB(char * mime_ptr, char * mime_end)
{
    ptr = mime_ptr;
    end = mime_end;
}

CASC_MIME_BLOB::~CASC_MIME_BLOB()
{
    ptr = end = NULL;
}

char * CASC_MIME_BLOB::GetNextLine()
{
    char * mime_line = ptr;

    while(ptr < end)
    {
        // Every line, even the last one, must be terminated with 0D 0A
        if(ptr[0] == 0x0D && ptr[1] == 0x0A)
        {
            // If space or tabulator follows, then this is continuation of the line
            if(ptr[2] == 0x09 || ptr[2] == 0x20)
            {
                ptr = ptr + 2;
                continue;
            }

            // Terminate the line and return its begin
            ptr[0] = 0;
            ptr[1] = 0;
            ptr = ptr + 2;
            return mime_line;
        }

        // Move to tne next character
        ptr++;
    }

    // No EOL terminated line found, break the search
    return NULL;
}

//-----------------------------------------------------------------------------
// The MIME element class

CASC_MIME_ELEMENT::CASC_MIME_ELEMENT()
{
    memset(this, 0, sizeof(CASC_MIME_ELEMENT));
}

CASC_MIME_ELEMENT::~CASC_MIME_ELEMENT()
{
    // Free the children and next elements
    if(folder.pChild != NULL)
        delete folder.pChild;
    folder.pChild = NULL;

    if(folder.pNext != NULL)
        delete folder.pNext;
    folder.pNext = NULL;

    // Free the data
    if(data.begin != NULL)
        CASC_FREE(data.begin);
    data.begin = NULL;
}

DWORD CASC_MIME_ELEMENT::Load(char * mime_data_begin, char * mime_data_end, const char * boundary_ptr)
{
    CASC_MIME_ENCODING Encoding = MimeEncodingTextPlain;
    CASC_MIME_BLOB mime_data(mime_data_begin, mime_data_end);
    size_t length_begin;
    size_t length_end;
    char * mime_line;
    char boundary_begin[MAX_LENGTH_BOUNDARY + 2];
    char boundary_end[MAX_LENGTH_BOUNDARY + 4];
    bool mime_version = false;
    DWORD dwErrCode = ERROR_SUCCESS;

    // Reset the boundary
    boundary[0] = 0;

    // Parse line-by-line untile we find the end of data
    while((mime_line = mime_data.GetNextLine()) != NULL)
    {
        // If the line is empty, this means that it's the end of the MIME header and begin of the MIME data
        if(mime_line[0] == 0)
        {
            mime_data.ptr = mime_line + 2;
            break;
        }

        // Root nodes are required to have MIME version
        if(boundary_ptr == NULL && !strcmp(mime_line, "MIME-Version: 1.0"))
        {
            mime_version = true;
            continue;
        }

        // Get the encoding
        if(!strncmp(mime_line, "Content-Transfer-Encoding: ", 27))
        {
            ExtractEncoding(mime_line + 27, Encoding);
            continue;
        }

        // Is there content type?
        if(!strncmp(mime_line, "Content-Type: ", 14))
        {
            ExtractBoundary(mime_line + 14, boundary, sizeof(boundary));
            continue;
        }
    }

    // Keep going only if we have MIME version
    if(boundary_ptr != NULL || mime_version == true)
    {
        // If we have boundary, it means that the element begin
        // and end is marked by the boundary
        if(boundary[0])
        {
            CASC_MIME_ELEMENT * pLast = NULL;
            CASC_MIME_ELEMENT * pChild;
            CASC_MIME_BLOB sub_mime(mime_data.ptr, NULL);

            // Construct the begin of the boundary. Don't include newline there,
            // as it must also match end of the boundary
            length_begin = CascStrPrintf(boundary_begin, _countof(boundary_begin), "--%s", boundary);
            dwErrCode = ERROR_SUCCESS;

            // The current line must point to the begin of the boundary
            // Find the end of the boundary
            if(memcmp(sub_mime.ptr, boundary_begin, length_begin))
                return ERROR_BAD_FORMAT;
            sub_mime.ptr += length_begin;

            // Find the end of the boundary
            length_end = CascStrPrintf(boundary_end, _countof(boundary_end), "--%s--\r\n", boundary);
            sub_mime.end = strstr(sub_mime.ptr, boundary_end);
            if(sub_mime.end == NULL)
                return ERROR_BAD_FORMAT;

            // This is the end of the MIME section. Cut it to blocks
            // and put each into the separate CASC_MIME_ELEMENT
            while(sub_mime.ptr < sub_mime.end)
            {
                char * sub_mime_next;

                // At this point, there must be newline in the current data pointer
                if(sub_mime.ptr[0] != 0x0D || sub_mime.ptr[1] != 0x0A)
                    return ERROR_BAD_FORMAT;
                sub_mime.ptr += 2;

                // Find the next MIME element. This must succeed, as the last element also matches the first element
                sub_mime_next = strstr(sub_mime.ptr, boundary_begin);
                if(sub_mime_next == NULL)
                    return ERROR_BAD_FORMAT;

                // Allocate the element
                pChild = AllocateAndLoadElement(sub_mime.ptr, sub_mime_next - 2, boundary_begin);
                if(pChild == NULL)
                {
                    dwErrCode = GetCascError();
                    break;
                }

                // Link the element
                if(folder.pChild == NULL)
                    folder.pChild = pChild;
                if(pLast != NULL)
                    pLast->folder.pNext = pChild;
                pLast = pChild;

                // Move to the next MIME element. Note that if we are at the ending boundary,
                // this moves past the end, but it's OK, because the while loop will catch that
                sub_mime.ptr = sub_mime_next + length_begin;
            }
        }
        else
        {
            CASC_MIME_BLOB content(mime_data.ptr, NULL);
            unsigned char * data_buffer;
            size_t data_length = 0;
            size_t raw_length;

            // If we have boundary pointer, we need to cut the data up to the boundary end.
            // Otherwise, we decode the data to the end of the document
            if(boundary_ptr != NULL)
            {
                // Find the end of the current data by the boundary. It is 2 characters before the next boundary 
                content.end = strstr(content.ptr, boundary_ptr);
                if(content.end == NULL)
                    return ERROR_BAD_FORMAT;
                if((content.ptr + 2) >= content.end)
                    return ERROR_BAD_FORMAT;
                content.end -= 2;
            }
            else
            {
                content.end = mime_data_end - 2;
                if(content.end[0] != 0x0D || content.end[1] != 0x0A)
                    return ERROR_BAD_FORMAT;
                if((content.ptr + 2) >= content.end)
                    return ERROR_BAD_FORMAT;
            }

            // Allocate buffer for decoded data.
            // Make it the same size like source data plus zero at the end
            raw_length = (content.end - content.ptr);
            data_buffer = CASC_ALLOC<unsigned char>(raw_length + 1);
            if(data_buffer != NULL)
            {
                // Decode the data
                switch(Encoding)
                {
                    case MimeEncodingTextPlain:
                        dwErrCode = DecodeTextPlain(content.ptr, content.end, data_buffer, &data_length);
                        break;

                    case MimeEncodingQuotedPrintable:
                        dwErrCode = DecodeQuotedPrintable(content.ptr, content.end, data_buffer, &data_length);
                        break;

                    case MimeEncodingBase64:
                        dwErrCode = DecodeBase64(content.ptr, content.end, data_buffer, &data_length);
                        break;
                }

                // If failed, free the buffer back
                if(dwErrCode != ERROR_SUCCESS)
                {
                    CASC_FREE(data_buffer);
                    data_buffer = NULL;
                    data_length = 0;
                }
            }
            else
            {
                dwErrCode = ERROR_NOT_ENOUGH_MEMORY;
            }

            // Put the data there, even if they are invalid
            data.begin = data_buffer;
            data.length = data_length;
        }
    }
    else
    {
        dwErrCode = ERROR_NOT_SUPPORTED;
    }

    // Return the result of the decoding
    return dwErrCode;
}

#ifdef _DEBUG
#define MAX_LEVEL 0x10
void CASC_MIME_ELEMENT::Print(size_t nLevel, size_t nIndex)
{
    char Prefix[(MAX_LEVEL * 4) + 0x20 + 1] = {0};
    size_t nSpaces;

    // Fill-in the spaces
    nSpaces = (nLevel < MAX_LEVEL) ? (nLevel * 4) : (MAX_LEVEL * 4);
    memset(Prefix, ' ', nSpaces);

    // Print the spaces and index
    nSpaces = printf("%s* [%u]: ", Prefix, nIndex);
    memset(Prefix, ' ', nSpaces);

    // Is this a folder item?
    if(folder.pChild != NULL)
    {
        printf("Folder item (boundary: %s)\n", boundary);
        folder.pChild->Print(nLevel + 1, 0);
    }
    else
    {
        char data_printable[0x20] = {0};

        for(size_t i = 0; (i < data.length && i < _countof(data_printable) - 1); i++)
        {
            if(0x20 <= data.begin[i] && data.begin[i] <= 0x7F)
                data_printable[i] = data.begin[i];
            else
                data_printable[i] = '.';
        }

        printf("Data item (%u bytes): \"%s\"\n", data.length, data_printable);
    }

    // Do we have a next element?
    if(folder.pNext != NULL)
    {
        folder.pNext->Print(nLevel, nIndex + 1);
    }
}
#endif

CASC_MIME_ELEMENT * CASC_MIME_ELEMENT::AllocateAndLoadElement(char * a_mime_data, char * a_mime_data_end, const char * boundary_begin)
{
    CASC_MIME_ELEMENT * pElement;
    DWORD dwErrCode = ERROR_NOT_ENOUGH_MEMORY;

    // Allocate the element
    if((pElement = new CASC_MIME_ELEMENT()) != NULL)
    {
        // Load the element
        dwErrCode = pElement->Load(a_mime_data, a_mime_data_end, boundary_begin);
        if(dwErrCode == ERROR_SUCCESS)
            return pElement;

        // Free the element on failure
        delete pElement;
    }

    SetCascError(dwErrCode);
    return NULL;
}

bool CASC_MIME_ELEMENT::ExtractEncoding(const char * line, CASC_MIME_ENCODING & Encoding)
{
    if(!_stricmp(line, "base64"))
    {
        Encoding = MimeEncodingBase64;
        return true;
    }

    if(!_stricmp(line, "quoted-printable"))
    {
        Encoding = MimeEncodingQuotedPrintable;
        return true;
    }

    // Unknown encoding
    return false;
}


bool CASC_MIME_ELEMENT::ExtractBoundary(const char * line, char * boundary, size_t length)
{
    const char * begin;
    const char * end;

    // Find the begin of the boundary
    if((begin = strstr(line, "boundary=\"")) != NULL)
    {
        // Set begin of the boundary
        begin = begin + 10;
        
        // Is there also end?
        if((end = strchr(begin, '\"')) != NULL)
        {
            if((size_t)(end - begin) < length)
            {
                memcpy(boundary, begin, end - begin);
                boundary[end - begin] = 0;
                return true;
            }
        }
    }

    return false;
}

DWORD CASC_MIME_ELEMENT::DecodeTextPlain(char * content_begin, char * content_end, unsigned char * data_ptr, size_t * ptr_length)
{
    size_t data_length = (size_t)(content_end - content_begin);

    // Sanity checks
    assert(content_begin && content_end);
    assert(content_end > content_begin);

    // Plain copy
    memcpy(data_ptr, content_begin, data_length);

    // Give the result
    if(ptr_length != NULL)
        ptr_length[0] = data_length;
    return ERROR_SUCCESS;
}


DWORD CASC_MIME_ELEMENT::DecodeQuotedPrintable(char * content_begin, char * content_end, unsigned char * data_ptr, size_t * ptr_length)
{
    unsigned char * save_data_ptr = data_ptr;
    char * content_ptr;
    DWORD dwErrCode;

    // Sanity checks
    assert(content_begin && content_end);
    assert(content_end > content_begin);

    // Decode the data
    for(content_ptr = content_begin; content_ptr < content_end; )
    {
        // If the data begins with '=', there is either newline or 2-char hexa number
        if(content_ptr[0] == '=')
        {
            // Is there a newline after the equal sign?
            if(content_ptr[1] == 0x0D && content_ptr[2] == 0x0A)
            {
                content_ptr += 3;
                continue;
            }

            // Is there hexa number after the equal sign?
            dwErrCode = BinaryFromString(content_ptr + 1, 2, data_ptr);
            if(dwErrCode != ERROR_SUCCESS)
                return dwErrCode;

            content_ptr += 3;
            data_ptr++;
            continue;
        }
        else
        {
            *data_ptr++ = (unsigned char)(*content_ptr++);
        }
    }

    if(ptr_length != NULL)
        ptr_length[0] = (size_t)(data_ptr - save_data_ptr);
    data_ptr[0] = 0;
    return ERROR_SUCCESS;
}

DWORD CASC_MIME_ELEMENT::DecodeBase64(char * content_begin, char * content_end, unsigned char * data_ptr, size_t * ptr_length)
{
    unsigned char * save_data_ptr = data_ptr;
    DWORD BitBuffer = 0;
    DWORD BitCount = 0;
    BYTE OneByte;

    // One time preparation of the conversion table
    if(CascBase64ToBits[0] == 0)
    {
        // Fill the entire table with 0xFF to mark invalid characters
        memset(CascBase64ToBits, BASE64_INVALID_CHAR, sizeof(CascBase64ToBits));

        // Set all whitespace characters
        for(BYTE i = 1; i <= 0x20; i++)
            CascBase64ToBits[i] = BASE64_WHITESPACE_CHAR;

        // Set all valid characters
        for(BYTE i = 0; CascBase64Table[i] != 0; i++)
        {
            OneByte = CascBase64Table[i];
            CascBase64ToBits[OneByte] = i;
        }
    }

    // Do the decoding
    while(content_begin < content_end && content_begin[0] != '=')
    {
        // Check for end of string
        if(content_begin[0] > sizeof(CascBase64ToBits))
            return ERROR_BAD_FORMAT;
        if((OneByte = CascBase64ToBits[*content_begin++]) == BASE64_INVALID_CHAR)
            return ERROR_BAD_FORMAT;
        if(OneByte == BASE64_WHITESPACE_CHAR)
            continue;

        // Put the 6 bits into the bit buffer
        BitBuffer = (BitBuffer << 6) | OneByte;
        BitCount += 6;

        // Flush all values
        while(BitCount >= 8)
        {
            // Decrement the bit count in the bit buffer
            BitCount -= 8;

            // The byte is the upper 8 bits of the bit buffer
            OneByte = (BYTE)(BitBuffer >> BitCount);
            BitBuffer = BitBuffer % (1 << BitCount);

            // Put the byte value. The buffer can not overflow,
            // because it is guaranteed to be equal to the length of the base64 string
            *data_ptr++ = OneByte;
        }
    }

    // Return the decoded length
    if(ptr_length != NULL)
        ptr_length[0] = (data_ptr - save_data_ptr);
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// The MIME class

CASC_MIME::CASC_MIME()
{}

CASC_MIME::~CASC_MIME()
{}

DWORD CASC_MIME::Load(char * data, size_t length)
{
    // Clear the root element
    memset(&root, 0, sizeof(CASC_MIME_ELEMENT));

    // Load the root element
    return root.Load(data, data + length);
}

DWORD CASC_MIME::Load(LPCTSTR szFileName)
{
    char * szFileData;
    DWORD cbFileData = 0;
    DWORD dwErrCode = ERROR_SUCCESS;

    // Note that LoadFileToMemory allocated one byte more and puts zero at the end
    // Thus, we can treat it as zero-terminated string
    szFileData = (char *)LoadFileToMemory(szFileName, &cbFileData);
    if(szFileData != NULL)
    {
        dwErrCode = Load(szFileData, cbFileData);
        CASC_FREE(szFileData);
    }
    else
    {
        dwErrCode = GetCascError();
    }

    return dwErrCode;
}

#ifdef _DEBUG
void CASC_MIME::Print()
{
    root.Print(0, 0);
}
#endif

//-----------------------------------------------------------------------------
// Local functions

enum MIME_PHASE
{
    MimePhaseHeader = 0,
    MimePhaseDataHeader,
    MimePhaseContent,
    MimePhaseDecoding,
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

static LPBYTE mime_to_raw(LPBYTE mime_response, size_t mime_length, size_t * PtrLength)
{
    QUERY_KEY MimeContent;
    QUERY_KEY RawContent;
    CASC_CSV MimeFile(0, false);
    MIME_PHASE nPhase = MimePhaseHeader;
    size_t nBoundaryLength = 0;
    DWORD dwErrCode;
    char szBoundary[0x80];

    FILE * fp = fopen("E:\\ribbit_response.txt", "wb");
    if(fp != NULL)
    {
        fwrite(mime_response, 1, mime_length, fp);
        fclose(fp);
    }

    // Initialize the parser to detect Ribbit newlines
    MimeFile.SetNextLineProc(NextLine_Mime, NextColumn_Mime);
    szBoundary[0] = 0;

    // Set the Ribbit parser
    dwErrCode = MimeFile.Load(mime_response, mime_length);
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

        // 3) Load the content of the MIME response
        if(nPhase == MimePhaseContent)
        {
            if(MimeFile.LoadNextLine())
            {
                // Get the single "line" from the MIME file
                const CASC_CSV_COLUMN & Column = MimeFile[CSV_ZERO][CSV_ZERO];

                // Give the "line" to the caller
                MimeContent.SetData(Column.szValue, Column.nLength);
                nPhase = MimePhaseDecoding;
            }
        }

        // 4) Decode the MIME content
        if(nPhase == MimePhaseDecoding)
        {
            // TODO
        }
    }

    // Give the result to the caller
    if(PtrLength != NULL)
        PtrLength[0] = RawContent.cbData;
    return RawContent.pbData;
}

//-----------------------------------------------------------------------------
// Public functions - sockets

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

// Guarantees that there is zero terminator after the response
char * sockets_read_response(CASC_SOCKET sock, const char * request, size_t request_length, size_t * PtrLength)
{
    char * server_response = NULL;
    size_t bytes_received = 0;
    size_t total_received = 0;
    size_t block_increment = 0x1000;
    size_t buffer_size = block_increment;

    // Pre-set the result length
    if(PtrLength != NULL)
        PtrLength[0] = 0;

    // On Windows, returns SOCKET_ERROR (-1)
    // On Linux, returns -1
    if(send(sock, request, (int)request_length, 0) == -1)
    {
        SetCascError(ERROR_NETWORK_NOT_AVAILABLE);
        return NULL;
    }

    // Allocate buffer for server response. Allocate one extra byte for zero terminator
    if((server_response = CASC_ALLOC<char>(buffer_size + 1)) != NULL)
    {
        for(;;)
        {
            // Reallocate the buffer size, if needed
            if(total_received == buffer_size)
            {
                if((server_response = CASC_REALLOC(char, server_response, buffer_size + block_increment + 1)) == NULL)
                {
                    SetCascError(ERROR_NOT_ENOUGH_MEMORY);
                    return NULL;
                }

                buffer_size += block_increment;
                block_increment *= 2;
            }

            // Receive the next part of the response, up to buffer size
            if((bytes_received = recv(sock, server_response + total_received, (int)(buffer_size - total_received), 0)) <= 0)
                break;
            total_received += bytes_received;
        }
    }

    // Terminate the response with zero. The space for EOS is guaranteed to be there,
    // because we always allocated one byte more than necessary
    if(server_response != NULL)
        server_response[total_received] = 0;

    // Give the result to the caller
    if(PtrLength != NULL)
        PtrLength[0] = total_received;
    return server_response;
}

void sockets_free()
{}

