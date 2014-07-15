/*****************************************************************************/
/* HashToPtr.cpp                          Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Implementation of hash-to-ptr map for CascLib                             */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 10.06.14  1.00  Lad  The first version of HashToPtr.cpp                   */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

//-----------------------------------------------------------------------------
// Local defines

#define PTR_FROM_TABLE_ITEM(ptr) ((PMAP_SUBTABLE)((size_t)ptr & ~(size_t)0x01))
#define TABLE_ITEM_FROM_PTR(ptr) ((void *)((size_t)ptr | (size_t)0x01))
#define IS_SUBTABLE_POINTER(ptr) ((size_t)ptr & 0x01)

//-----------------------------------------------------------------------------
// Local functions

static DWORD CalcHashIndex(PMAP_HASH_TO_PTR pMap, LPBYTE pbHash)
{
    DWORD dwHash = 0x7EEE7EEE;

    // Construct the hash from the first 4 digits
    for(size_t i = 0; i < pMap->KeyLength; i++)
        dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ pbHash[i];
    return (dwHash % pMap->TableSize);
}

//-----------------------------------------------------------------------------
// Public functions

PMAP_HASH_TO_PTR MapHashToPtr_Create(DWORD dwMaxItems, DWORD dwKeyLength, DWORD dwMemberOffset)
{
    PMAP_HASH_TO_PTR pMap;
    size_t cbToAllocate;
    size_t dwTableSize;

    // Calculate the size of the table
    dwTableSize = (dwMaxItems * 3 / 2) | 0x01;

    // Allocate new map for the objects
    cbToAllocate = sizeof(MAP_HASH_TO_PTR) + (dwTableSize * sizeof(void *));
    pMap = (PMAP_HASH_TO_PTR)CASC_ALLOC(LPBYTE, cbToAllocate);
    if(pMap != NULL)
    {
        memset(pMap, 0, cbToAllocate);
        pMap->KeyLength = dwKeyLength;
        pMap->TableSize = dwTableSize;
        pMap->MemberOffset = dwMemberOffset;
    }

    // Return the allocated map
    return pMap;
}

size_t MapHashToPtr_EnumObjects(PMAP_HASH_TO_PTR pMap, void **ppvArray)
{
    size_t nIndex = 0;

    // Verify pointer to the map
    if(pMap != NULL && ppvArray != NULL)
    {
        // Enumerate all items in main table
        for(size_t i = 0; i < pMap->TableSize; i++)
        {
            // Is that cell valid?
            if(pMap->HashTable[i] != NULL)
            {
                ppvArray[nIndex++] = pMap->HashTable[i];
            }
        }
    }

    return pMap->ItemCount;
}

void * MapHashToPtr_FindObject(PMAP_HASH_TO_PTR pMap, LPBYTE pbHash)
{
    LPBYTE pbTableEntry;
    DWORD dwHashIndex;

    // Verify pointer to the map
    if(pMap != NULL)
    {
        // Construct the main index
        dwHashIndex = CalcHashIndex(pMap, pbHash);
        while(pMap->HashTable[dwHashIndex] != NULL)
        {
            // Get the pointer at that position
            pbTableEntry = (LPBYTE)pMap->HashTable[dwHashIndex];

            // Compare the hash
            if(!memcmp(pbTableEntry, pbHash, pMap->KeyLength))
                return (pbTableEntry - pMap->MemberOffset);

            // Move to the next entry
            dwHashIndex = (dwHashIndex + 1) % pMap->TableSize;
        }
    }

    // Not found, sorry
    return NULL;
}

bool MapHashToPtr_InsertObject(PMAP_HASH_TO_PTR pMap, LPBYTE pbNewHash)
{
    LPBYTE pbTableEntry;
    DWORD dwHashIndex;

    // Verify pointer to the map
    if(pMap != NULL)
    {
        // Limit check
        if((pMap->ItemCount + 1) >= pMap->TableSize)
            return false;

        // Construct the hash index
        dwHashIndex = CalcHashIndex(pMap, pbNewHash);
        while(pMap->HashTable[dwHashIndex] != NULL)
        {
            // Get the pointer at that position
            pbTableEntry = (LPBYTE)pMap->HashTable[dwHashIndex];

            // Check if hash being inserted conflicts with an existing hash
            if(!memcmp(pbNewHash, pbTableEntry, pMap->KeyLength))
                return false;

            // Move to the next entry
            dwHashIndex = (dwHashIndex + 1) % pMap->TableSize;
        }

        // Insert at that position
        pMap->HashTable[dwHashIndex] = pbNewHash;
        pMap->ItemCount++;
        return true;
    }

    // Failed
    return false;
}

void MapHashToPtr_Free(PMAP_HASH_TO_PTR pMap)
{
    if(pMap != NULL)
    {
        CASC_FREE(pMap);
    }
}
