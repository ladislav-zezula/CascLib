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

#define PTR_FROM_TABLE_ITEM(ptr) ((PPOINTER_TABLE)((size_t)ptr & ~(size_t)0x01))
#define TABLE_ITEM_FROM_PTR(ptr) ((void *)((size_t)ptr | (size_t)0x01))
#define IS_SUBTABLE_POINTER(ptr) ((size_t)ptr & 0x01)

//-----------------------------------------------------------------------------
// Local structures

//-----------------------------------------------------------------------------
// Local functions

static DWORD CalcHashIndex(PMAP_HASH_TO_PTR pMap, LPBYTE pbHash)
{
    DWORD dwHash = 0;

    // Construct the hash from the first 4 digits
    dwHash = (dwHash << 0x08) | pbHash[0];
    dwHash = (dwHash << 0x08) | pbHash[1];
    dwHash = (dwHash << 0x08) | pbHash[2];
    dwHash = (dwHash << 0x08) | pbHash[3];

    return (dwHash & pMap->HashMask);
}

//-----------------------------------------------------------------------------
// Public functions

PMAP_HASH_TO_PTR MapHashToPtr_Create(DWORD dwMaxItems, DWORD dwKeyLength)
{
    PMAP_HASH_TO_PTR pMap;
    size_t nToAllocate;
    DWORD dwHashTableSize = 0x00800000;

    // Sanity check
    assert((dwMaxItems + dwMaxItems) <= dwHashTableSize);
    dwMaxItems = (dwMaxItems + dwMaxItems);

    // Get the hash table size
    while(dwHashTableSize > 0x40)
    {
        if((dwHashTableSize / 2) < dwMaxItems)
            break;
        dwHashTableSize = (dwHashTableSize >> 0x01);
    }

    // Allocate new map for the objects
    nToAllocate = sizeof(MAP_HASH_TO_PTR) + dwHashTableSize * sizeof(void *);
    pMap = (PMAP_HASH_TO_PTR)CASC_ALLOC(LPBYTE, nToAllocate);
    if(pMap != NULL)
    {
        memset(pMap, 0, nToAllocate);
        pMap->KeyLength = dwKeyLength;
        pMap->HashMask = dwHashTableSize - 1;
    }

    // Return the allocated map
    return pMap;
}

void * MapHashToPtr_FindObject(PMAP_HASH_TO_PTR pMap, LPBYTE pbHash)
{
    DWORD StartIndex;
    DWORD HashIndex;

    // Verify pointer to the map
    if(pMap != NULL)
    {
        // Construct the main index
        StartIndex = HashIndex = CalcHashIndex(pMap, pbHash);

        // Search the array
        do
        {
            // Is that item empty?
            if(pMap->HashTable[HashIndex] == NULL)
                return NULL;

            // Is that item occupied?
            if(!memcmp(pMap->HashTable[HashIndex], pbHash, pMap->KeyLength))
                return (LPBYTE)pMap->HashTable[HashIndex];

            // Move to the next item
            HashIndex = (HashIndex + 1) & pMap->HashMask;
        }
        while(HashIndex != StartIndex);
    }

    // Not found, sorry
    return NULL;
}

void MapHashToPtr_EnumObjects(PMAP_HASH_TO_PTR pMap, PFN_ENUM_MAP_PROC pfnEnumProc, void * pvContext)
{
    // Verify pointer to the map
    if(pMap != NULL)
    {
        for(size_t i = 0; i < pMap->HashMask + 1; i++)
        {
            if(pMap->HashTable[i] != NULL)
            {
                if(!pfnEnumProc(pMap, pvContext, (LPBYTE)pMap->HashTable[i]))
                    return;
            }
        }
    }
}

DWORD dwConflicts = 0;

bool MapHashToPtr_InsertObject(PMAP_HASH_TO_PTR pMap, LPBYTE pbHash)
{
    DWORD StartIndex;
    DWORD HashIndex;

    // Verify pointer to the map
    if(pMap != NULL)
    {
        // Construct the hash index
        StartIndex = HashIndex = CalcHashIndex(pMap, pbHash);

        // Parse the item array
        do
        {
            // If that item is empty, we can insert the new hash
            if(pMap->HashTable[HashIndex] == NULL)
            {
                pMap->HashTable[HashIndex] = pbHash;
                pMap->ObjectCount++;
                return true;
            }

            dwConflicts++;
            
            // Check if the existing hash matches
            if(!memcmp(pMap->HashTable[HashIndex], pbHash, pMap->KeyLength))
                return false;

            // Move to the next item
            HashIndex = (HashIndex + 1) & pMap->HashMask;
        }
        while(HashIndex != StartIndex);
    }

    // No free space
    return false;
}

void MapHashToPtr_Free(PMAP_HASH_TO_PTR pMap)
{
    if(pMap != NULL)
    {
        CASC_FREE(pMap);
    }
}
