/*****************************************************************************/
/* CascMndxRoot.h                         Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Interface file for MNDX structures                                        */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 15.05.14  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef __CASC_MNDX_ROOT__
#define __CASC_MNDX_ROOT__

class TFileNameDatabase;

#define CASC_MAX_ENTRIES(type) (0xFFFFFFFF / sizeof(type))

typedef struct _TRIPLET
{
    DWORD Value1;
    DWORD Value2;
    DWORD Value3;
} TRIPLET, *PTRIPLET;

typedef struct _FRAGMENT_INFO
{
    DWORD ExpectedHashModifier;                     // Expected value of the hash seed for the current search.
                                                    // Zero means that the entry's is a starting entry for a name search
    DWORD NextHashModifier;                         // Hash seed for the next search
    DWORD FragmentOffset;                           // Higher 24 bits are zero --> Offset of the name fragment to be compared and skipped
                                                    // Higher 24 bits are set  --> A single matching character
} FRAGMENT_INFO, *PFRAGMENT_INFO;

typedef struct _STRUCT14
{
    DWORD HashModifier;
    DWORD field_4;
    DWORD field_8;
    DWORD field_C;
    DWORD field_10;
} STRUCT14, *PSTRUCT14;


class TByteStream
{
    public:

    TByteStream();

    void ExchangeWith(TByteStream & Target);
    int SetByteBuffer(LPBYTE pbNewMarData, DWORD cbNewMarData);
    int GetBytes(DWORD cbByteCount, LPBYTE * ppbBuffer);
    int SkipBytes(DWORD cbByteCount);
    int GetArray_DWORDs(LPDWORD * PtrItemArray, DWORD ItemCount);
    int GetArray_Triplets(PTRIPLET * PtrItemArray, DWORD ItemCount);
    int GetArray_FragmentInfos(PFRAGMENT_INFO * PtrItemArray, DWORD ItemCount);
    int GetArray_BYTES(LPBYTE * PtrItemArray, DWORD ItemCount);

    LPBYTE pbByteData;
    void * pvMappedFile;
    DWORD cbByteData;
    DWORD field_C;
    HANDLE hFile;
    HANDLE hMap;
};

class TGenericArray 
{
    public:

    TGenericArray();
    ~TGenericArray();

    DWORD IsBitSet(DWORD dwBitIndex)
    {
        return (u.DWORDS.ItemArray[dwBitIndex >> 0x05] & (1 << (dwBitIndex & 0x1F)));
    }

    int SetArrayValid();

    void ExchangeWith(TGenericArray & Target);
    void CopyFrom(TGenericArray & Source);

    void SetMaxItems_CHARS(DWORD NewMaxItemCount);
    void SetMaxItems_STRUCT14(DWORD NewMaxItemCount);

    void InsertOneItem_CHAR(char OneChar);
    void InsertOneItem_STRUCT14(PSTRUCT14 pNewItem);

    void sub_19583A0(DWORD NewItemCount);

    int LoadDwordsArray(TByteStream & InStream);
    int LoadTripletsArray(TByteStream & InStream);
    int LoadByteArray(TByteStream & InStream);
    int LoadFragmentInfos(TByteStream & InStream);
    int LoadStrings(TByteStream & InStream);

    int LoadDwordsArrayWithCopy(TByteStream & InStream);
    int LoadTripletsArray_Copy(TByteStream & InStream);
    int LoadBytes_Copy(TByteStream & InStream);
    int LoadFragmentInfos_Copy(TByteStream & InStream);
    int LoadStringsWithCopy(TByteStream & InStream);

    void * DataBuffer;
    void * field_4;

    union
    {
        struct
        {
            LPBYTE ItemArray;
            DWORD ItemCount;
        } BYTES;

        struct
        {
            char * ItemArray;
            DWORD ItemCount;
        } CHARS;

        struct
        {
            LPDWORD ItemArray;
            DWORD ItemCount;
        } DWORDS;

        struct
        {
            PTRIPLET ItemArray;
            DWORD ItemCount;
        } TRIPLETS;

        struct
        {
            PFRAGMENT_INFO ItemArray;
            DWORD ItemCount;
        } FRAGMENT_INFOS;

        struct
        {
            PSTRUCT14 ItemArray;
            DWORD ItemCount;
        } STRUCT14;

    } u;

    DWORD MaxItemCount;                 // Number of items (item != byte) in DataBuffer
    bool  bIsValidArray;
};

class TBitEntryArray : public TGenericArray 
{
    public:

    TBitEntryArray();
    ~TBitEntryArray();

    DWORD GetBitEntry(DWORD EntryIndex)
    {
        DWORD dwItemIndex = (EntryIndex * BitsPerEntry) >> 0x05;
        DWORD dwStartBit = (EntryIndex * BitsPerEntry) & 0x1F;
        DWORD dwEndBit = dwStartBit + BitsPerEntry;
        DWORD dwResult;

        // If the end bit index is greater than 32,
        // we also need to load from the next 32-bit item
        if(dwEndBit > 0x20)
        {
            dwResult = (u.DWORDS.ItemArray[dwItemIndex + 1] << (0x20 - dwStartBit)) | (u.DWORDS.ItemArray[dwItemIndex] >> dwStartBit);
        }
        else
        {
            dwResult = u.DWORDS.ItemArray[dwItemIndex] >> dwStartBit;
        }

        // Now we also need to mask the result by the bit mask
        return dwResult & EntryBitMask;
    }

    void ExchangeWith(TBitEntryArray & Target);
    int LoadFromStream(TByteStream & InStream);
    int LoadFromStream_Exchange(TByteStream & InStream);

    DWORD BitsPerEntry;
    DWORD EntryBitMask;
    DWORD TotalEntries;
};

class TStruct40
{
    public:

    TStruct40();

    void sub_19586B0();

    TGenericArray array_00;
    TGenericArray array_18;
    DWORD HashModifier;
    DWORD CharIndex;
    DWORD ItemCount;
    DWORD field_3C;
};

class TMndxFindResult
{
    public:

    TMndxFindResult();
    ~TMndxFindResult();

    int CreateStruct40();
    void FreeStruct40();

    int SetPath(const char * szPathName, size_t cchPathName);

    const char * szPathName;
    size_t cchPathName;
    DWORD field_8;
    const char * szFoundPathName;
    size_t cchFoundPathName;
    DWORD MndxIndex;                                        // Index to the array of MNDX WSentries
    TStruct40 * pStruct40;
};

class TStruct68
{
    public:

    TStruct68();

    void ExchangeWith(TStruct68 & TargetObject);
    int LoadFromStream(TByteStream & InStream);
    int LoadFromStream_Exchange(TByteStream & InStream);

    DWORD sub_1959B60(DWORD arg_0);

    TGenericArray BitArray_0;
    DWORD field_18;
    DWORD field_1C;
    TGenericArray ArrayTriplets_20;
    TGenericArray ArrayDwords_38;
    TGenericArray ArrayDwords_50;
};

class TNameIndexStruct
{
    public:

    TNameIndexStruct();
    ~TNameIndexStruct();

    bool CompareAndSkipNameFragment(TMndxFindResult * pStruct1C, DWORD dwDistance);
    bool sub_195A570(TMndxFindResult * pStruct1C, DWORD dwDistance);
    void CopyNameFragment(TMndxFindResult * pStruct1C, DWORD dwDistance);

    void ExchangeWith(TNameIndexStruct & Target);
    int LoadFromStream(TByteStream & InStream);
    int LoadFromStream_Exchange(TByteStream & InStream);

    TGenericArray NameFragments;  
    TStruct68 Struct68;  
};

class TStruct10
{
    public:
    
    TStruct10();
    
    void CopyFrom(TStruct10 & Target);
    int sub_1956FD0(DWORD dwBitMask);
    int sub_1957050(DWORD dwBitMask);
    int sub_19572E0(DWORD dwBitMask);
    int sub_1957800(DWORD dwBitMask);

    DWORD field_0;
    DWORD field_4;
    DWORD field_8;
    DWORD field_C;
};

class TFileNameDatabasePtr
{
    public:

    TFileNameDatabasePtr();
    ~TFileNameDatabasePtr();

    int FindFileInDatabase(TMndxFindResult * pStruct1C);
    int sub_1956CE0(TMndxFindResult * pStruct1C, bool * pbFindResult);

    int GetStruct68_68_Field1C(LPDWORD ptr_var_C);
    int CreateDatabase(LPBYTE pbMarData, DWORD cbMarData);
    int SetDatabase(TFileNameDatabase * pNewDB);

    TFileNameDatabase * pDB;
};

class TFileNameDatabase
{
    public:
    
    TFileNameDatabase();

    void ExchangeWith(TFileNameDatabase & Target);
    int LoadFromStream(TByteStream & InStream);
    int LoadFromStream_Exchange(TByteStream & InStream);

    DWORD sub_1959CB0(DWORD dwKey);
    DWORD sub_1959F50(DWORD arg_0);

    DWORD sub_1957350(DWORD arg_0);
    DWORD sub_19573D0(DWORD arg_0, DWORD arg_4);

    bool sub_1957B80(TMndxFindResult * pStruct1C, DWORD dwKey);
    bool sub_1957970(TMndxFindResult * pStruct1C);
    bool FindFileInDatabase(TMndxFindResult * pStruct1C);

    void sub_1958D70(TMndxFindResult * pStruct1C, DWORD arg_4);
    bool sub_1959010(TMndxFindResult * pStruct1C, DWORD arg_4);
    bool sub_1958B00(TMndxFindResult * pStruct1C);
    bool sub_1959460(TMndxFindResult * pStruct1C);

    TStruct68 Struct68_00;
    TStruct68 Struct68_68;
    TStruct68 Struct68_D0;

    TGenericArray bytes_138;
    TBitEntryArray BitArray_150;

    TNameIndexStruct IndexStruct_174;
    TFileNameDatabasePtr NextDB;

    TGenericArray triplets_1F8;

    DWORD dwKeyMask;
    DWORD field_214;
    TStruct10 Struct10;
    TByteStream MarStream;
};

typedef struct _MAR_FILE
{
    TFileNameDatabasePtr * pDatabasePtr;
    LPBYTE pbMarData;
    DWORD cbMarData;
} MAR_FILE, *PMAR_FILE;

//-----------------------------------------------------------------------------
// CASC functions related to MNDX

int  LoadMndxRootFile(TCascStorage * hs, LPBYTE pbRootFile, DWORD cbRootFile);
int  FindMndxPackageNumber(TCascStorage * hs, const char * szFileName, LPDWORD pdwPackage);
int  SearchMndxInfo(PCASC_MNDX_INFO pMndxInfo, const char * szFileName, DWORD dwDataAsset, PCASC_ROOT_KEY_INFO pFoundInfo);
bool DoStorageSearch_MNDX(TCascSearch * pSearch, PCASC_FIND_DATA pFindData);
void FreeMndxInfo(PCASC_MNDX_INFO pMndxInfo);

#endif  // __CASC_MNDX_ROOT__
