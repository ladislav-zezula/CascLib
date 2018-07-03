/*****************************************************************************/
/* Csv.h                                  Copyright (c) Ladislav Zezula 2018 */
/*---------------------------------------------------------------------------*/
/* Interface for the CSV handler class                                       */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 30.06.18  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef __CSV_H__
#define __CSV_H__

//-----------------------------------------------------------------------------
// Defines

#define MAX_CSV_ELEMENTS   0x10

//-----------------------------------------------------------------------------
// CSV class

typedef struct _CSV_ELEMENT
{
    const char * szString;
    size_t nLength;
} CSV_ELEMENT, *PCSV_ELEMENT;

class CASC_CSV
{
    public:

    CASC_CSV();
    ~CASC_CSV();

    size_t LoadElements(PCSV_ELEMENT Headers, size_t nMaxElements, const char * szLinePtr, const char * szLineEnd);
    size_t LoadHeader(const char * szLinePtr, const char * szLineEnd);
    size_t LoadHeader(void * pvListFile);
    bool GetColumnIndices(size_t * Indices, ...);

    size_t LoadNextLine(const char * szLinePtr, const char * szLineEnd);
    size_t LoadNextLine(void * pvListFile);
    size_t GetColumnCount() { return nColumns; }
    int GetString(char * szBuffer, size_t nMaxChars, size_t Index);
    TCHAR * GetString(size_t Index);
    int GetBinary(LPBYTE pbBuffer, size_t nMaxBytes, size_t Index);
    int GetData(QUERY_KEY & Key, size_t Index, bool bHexaValue);

    protected:

    CSV_ELEMENT Headers[MAX_CSV_ELEMENTS];
    CSV_ELEMENT Columns[MAX_CSV_ELEMENTS];
    size_t nColumns;
};

#endif  // __CSV_H__
