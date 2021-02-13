/*****************************************************************************/
/* Mime.h                                 Copyright (c) Ladislav Zezula 2021 */
/*---------------------------------------------------------------------------*/
/* MIME parsing functions for CascLib                                        */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 21.01.21  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef __MIME_H__
#define __MIME_H__

//-----------------------------------------------------------------------------
// MIME constants

#define MAX_LENGTH_BOUNDARY 128

enum CASC_MIME_ENCODING
{
    MimeEncodingTextPlain,
    MimeEncodingBase64,
    MimeEncodingQuotedPrintable,
    MimeEncodingMax
};

//-----------------------------------------------------------------------------
// MIME blob class

struct CASC_MIME_BLOB
{
    CASC_MIME_BLOB(char * mime_ptr, char * mime_end);
    ~CASC_MIME_BLOB();

    char * GetNextLine();

    char * ptr;
    char * end;
};

//-----------------------------------------------------------------------------
// MIME class

class CASC_MIME_ELEMENT
{
    public:

    CASC_MIME_ELEMENT();
    ~CASC_MIME_ELEMENT();

    unsigned char * GiveAway(size_t * ptr_data_length);

    DWORD Load(char * mime_data_begin, char * mime_data_end, const char * boundary_ptr = NULL);

    CASC_MIME_ELEMENT * GetChild()  { return folder.pChild; }

#ifdef _DEBUG
    void Print(size_t nLevel, size_t nIndex);
#endif

    protected:

    CASC_MIME_ELEMENT * AllocateAndLoadElement(char * a_mime_data, char * a_mime_data_end, const char * boundary_begin);
    bool   ExtractEncoding(const char * line, CASC_MIME_ENCODING & Encoding);
    bool   ExtractBoundary(const char * line, char * boundary, size_t length);

    DWORD DecodeTextPlain(char * content_begin, char * content_end, unsigned char * data_ptr, size_t * ptr_length);
    DWORD DecodeQuotedPrintable(char * content_begin, char * content_end, unsigned char * data_ptr, size_t * ptr_length);
    DWORD DecodeBase64(char * content_begin, char * content_end, unsigned char * data_ptr, size_t * ptr_length);

    struct
    {
        CASC_MIME_ELEMENT * pChild;     // Pointer to the first child
        CASC_MIME_ELEMENT * pNext;      // Pointer to the next-in-folder element
    } folder;

    struct
    {
        unsigned char * begin;
        size_t length;
    } data;

    char boundary[MAX_LENGTH_BOUNDARY];
};

class CASC_MIME
{
    public:

    CASC_MIME();
    ~CASC_MIME();

    unsigned char * GiveAway(size_t * ptr_data_length);

    DWORD Load(char * data, size_t length);
    DWORD Load(LPCTSTR fileName);

#ifdef _DEBUG
    void Print();
#endif

    protected:

    CASC_MIME_ELEMENT root;
};

#endif // __MIME_H__
