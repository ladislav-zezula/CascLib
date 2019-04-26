/*****************************************************************************/
/* RootHandler.h                          Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Interface for root handlers                                               */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 09.03.15  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef __ROOT_HANDLER_H__
#define __ROOT_HANDLER_H__

//-----------------------------------------------------------------------------
// Defines

#define CASC_MNDX_ROOT_SIGNATURE        0x58444E4D  // 'MNDX'
#define CASC_TVFS_ROOT_SIGNATURE        0x53465654  // 'TVFS'
#define CASC_DIABLO3_ROOT_SIGNATURE     0x8007D0C4
#define CASC_WOW82_ROOT_SIGNATURE       0x4D465354  // 'TSFM', WoW since 8.2

#define ROOT_FLAG_NAME_HASHES           0x00000001  // The root file contains name hashes only; no file names present
#define ROOT_FLAG_FILE_DATA_IDS         0x00000002  // The root file contains file data ids only; no file names present
#define ROOT_FLAG_USES_EKEY             0x00000004  // ROOT_SEARCH and ROOT_GETKEY returns EKey instead of CKey
#define ROOT_FLAG_DONT_SEARCH_CKEY      0x00000008  // Disable searching the files by CKey

#define DUMP_LEVEL_ROOT_FILE                     1  // Dump root file
#define DUMP_LEVEL_ENCODING_FILE                 2  // Dump root file + encoding file
#define DUMP_LEVEL_INDEX_ENTRIES                 3  // Dump root file + encoding file + index entries

//-----------------------------------------------------------------------------
// Class for generic root handler

struct TRootHandler
{
    public:

    TRootHandler();
    virtual ~TRootHandler();

    // Inserts new file name to the root handler
    virtual int Insert(
        const char * szFileName,                    // Pointer to the file name
        struct _CASC_CKEY_ENTRY * pCKeyEntry        // Pointer to the CASC_CKEY_ENTRY for the file
        );

    // Performs find-next-file operation. Only returns known names
    virtual LPBYTE Search(
        struct _TCascSearch * pSearch               // Pointer to the initialized search structure
        );

    // Cleanup handler. Called when the search is complete.
    virtual void EndSearch(
        struct _TCascSearch * pSearch               // Pointer to the initialized search structure
        );

    // Retrieves CKey/EKey for a given file name.
    virtual LPBYTE GetKey(
        const char * szFileName,                    // Pointer to the name of a file
        DWORD FileDataId,                           // FileDataId. Use CASC_INVALID_ID if the file data ID is not supported
        PDWORD PtrFileSize                          // The root handler may maintain file size
        );

    DWORD GetFlags()
    {
        return dwRootFlags;
    }

    protected:

    DWORD dwRootFlags;                              // Root flags - see the ROOT_FLAG_XXX
};

//-----------------------------------------------------------------------------
// Class for root handler that has basic mapping of FileName -> CASC_FILE_NODE

struct TFileTreeRoot : public TRootHandler
{
    TFileTreeRoot(DWORD FileTreeFlags);
    virtual ~TFileTreeRoot();

    int    Insert(const char * szFileName, struct _CASC_CKEY_ENTRY * pCKeyEntry);
    LPBYTE Search(struct _TCascSearch * pSearch);
    LPBYTE GetKey(const char * szFileName, DWORD FileDataId, PDWORD PtrFileSize);
    DWORD  GetFileDataId(const char * szFileName);

    protected:

    CASC_FILE_TREE FileTree;
};

#endif  // __ROOT_HANDLER_H__
