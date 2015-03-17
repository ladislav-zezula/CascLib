/*****************************************************************************/
/* RootFile.h                             Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Interface for TRootFile class                                             */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 09.03.15  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef __ROOT_FILE_H__
#define __ROOT_FILE_H__

//-----------------------------------------------------------------------------
// Defines

#define CASC_DIABLO3_ROOT_SIGNATURE     0x8007D0C4
#define CASC_MNDX_ROOT_SIGNATURE        0x58444E4D  // 'MNDX'

#define ROOT_FLAG_HAS_NAMES             0x00000001  // The root file contains file names

#define DUMP_LEVEL_ROOT_FILE                    1   // Dump root file
#define DUMP_LEVEL_ENCODING_FILE                2   // Dump root file + encoding file
#define DUMP_LEVEL_INDEX_ENTRIES                3   // Dump root file + encoding file + index entries

//-----------------------------------------------------------------------------
// Root file function prototypes

typedef LPBYTE (*ROOT_SEARCH)(
    struct TRootFile * pRootFile,                   // Pointer to an initialized root handler
    struct _TCascSearch * pSearch,                  // Pointer to the initialized search structure
    PDWORD PtrFileSize,                             // Pointer to receive file size (optional)
    PDWORD PtrLocaleFlags                           // Pointer to receive locale flags (optional)
    );

typedef void (*ROOT_ENDSEARCH)(
    struct TRootFile * pRootFile,                   // Pointer to an initialized root handler
    struct _TCascSearch * pSearch                   // Pointer to the initialized search structure
    );

typedef LPBYTE (*ROOT_GETKEY)(
    struct TRootFile * pRootFile,                   // Pointer to an initialized root handler
    const char * szFileName                         // Pointer to the name of a file
    );

typedef void (*ROOT_DUMP)(
    struct _TCascStorage * hs,                      // Pointer to the open storage
    TDumpContext * dc,                              // Opened dump context
    LPBYTE pbRootFile,                              // Pointer to the loaded ROOT file
    DWORD cbRootFile,                               // Length of the loaded ROOT file
    const TCHAR * szListFile,
    int nDumpLevel
    );

typedef void (*ROOT_CLOSE)(
    struct TRootFile * pRootFile                    // Pointer to an initialized root handler
    );

struct TRootFile
{
    ROOT_SEARCH    Search;                          // Performs the root file search
    ROOT_ENDSEARCH EndSearch;                       // Performs cleanup after searching
    ROOT_GETKEY    GetKey;                          // Retrieves encoding key for a file name
    ROOT_DUMP      Dump;
    ROOT_CLOSE     Close;                           // Closing the root file

    DWORD dwRootFlags;                              // Root flags - see the ROOT_FLAG_XXX
};

//-----------------------------------------------------------------------------
// Public functions

LPBYTE RootFile_Search(TRootFile * pRootFile, struct _TCascSearch * pSearch, PDWORD PtrFileSize, PDWORD PtrLocaleFlags);
void   RootFile_EndSearch(TRootFile * pRootFile, struct _TCascSearch * pSearch);
LPBYTE RootFile_GetKey(TRootFile * pRootFile, const char * szFileName);
void   RootFile_Dump(struct _TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile, const TCHAR * szNameFormat, const TCHAR * szListFile, int nDumpLevel);
void   RootFile_Close(TRootFile * pRootFile);

#endif  // __ROOT_FILE_H__
