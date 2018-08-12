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

#define ROOT_FLAG_HAS_NAMES             0x00000001  // The root file contains file names
#define ROOT_FLAG_USES_EKEY             0x00000002  // ROOT_SEARCH and ROOT_GETKEY returns EKey instead of CKey
#define ROOT_FLAG_DONT_SEARCH_CKEY      0x00000004  // Disable searching the files by CKey

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
        PDWORD PtrFileSize                          // The root handler may maintain file size
        );

    // Returns FileDataId for a given name. Only if the FileDataId is supported.
    virtual DWORD GetFileId(
        const char * szFileName                     // Pointer to the name of a file
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
    LPBYTE GetKey(const char * szFileName, PDWORD PtrFileSize);
    DWORD  GetFileId(const char * szFileName);

    protected:

    CASC_FILE_TREE FileTree;
};

#endif  // __ROOT_HANDLER_H__
