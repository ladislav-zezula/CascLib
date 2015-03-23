/*****************************************************************************/
/* Map.cpp                                Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Implementation of map for CascLib                                         */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 10.06.14  1.00  Lad  The first version of Map.cpp                         */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

//-----------------------------------------------------------------------------
// Local functions

static DWORD CalcHashIndex(PCASC_MAP pMap, void * pvKey)
{
    LPBYTE pbKey = (LPBYTE)pvKey;
    DWORD dwHash = 0x7EEE7EEE;

    // Is it a string table?
    if(pMap->KeyLength == KEY_LENGTH_STRING)
    {
        for(size_t i = 0; pbKey[i] != 0; i++)
            dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ pbKey[i];
    }
    else
    {
        // Construct the hash from the first 8 digits
        dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ pbKey[0];
        dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ pbKey[1];
        dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ pbKey[2];
        dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ pbKey[3];
        dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ pbKey[4];
        dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ pbKey[5];
        dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ pbKey[6];
        dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ pbKey[7];
    }

    // Return the hash limited by the table size
    return (dwHash % pMap->TableSize);
}

static bool CompareIdentifier(PCASC_MAP pMap, void * pvObject, void * pvKey)
{
    // Is it a string table?
    if(pMap->KeyLength == KEY_LENGTH_STRING)
    {
        char * szObjectKey = (char *)pvObject + pMap->KeyOffset;
        char * szKey = (char *)pvKey;

        return (strcmp(szObjectKey, szKey) == 0);
    }
    else
    {
        LPBYTE pbObjectKey = (LPBYTE)pvObject + pMap->KeyOffset;

        return (memcmp(pbObjectKey, pvKey, pMap->KeyLength) == 0);
    }
}

//-----------------------------------------------------------------------------
// Public functions

PCASC_MAP Map_Create(DWORD dwMaxItems, DWORD dwKeyLength, DWORD dwKeyOffset)
{
    PCASC_MAP pMap;
    size_t cbToAllocate;
    size_t dwTableSize;

    // Calculate the size of the table
    dwTableSize = (dwMaxItems * 3 / 2) | 0x01;

    // Allocate new map for the objects
    cbToAllocate = sizeof(CASC_MAP) + (dwTableSize * sizeof(void *));
    pMap = (PCASC_MAP)CASC_ALLOC(LPBYTE, cbToAllocate);
    if(pMap != NULL)
    {
        memset(pMap, 0, cbToAllocate);
        pMap->KeyLength = dwKeyLength;
        pMap->TableSize = dwTableSize;
        pMap->KeyOffset = dwKeyOffset;
    }

    // Return the allocated map
    return pMap;
}

size_t Map_EnumObjects(PCASC_MAP pMap, void **ppvArray)
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

void * Map_FindObject2(PCASC_MAP pMap, MAP_COMPARE pfnCompare, void * pvKey, PDWORD PtrIndex)
{
    void * pvObject;
    DWORD dwHashIndex;

    // Verify pointer to the map
    if(pMap != NULL)
    {
        // Construct the main index
        dwHashIndex = CalcHashIndex(pMap, pvKey);
        while(pMap->HashTable[dwHashIndex] != NULL)
        {
            // Get the pointer at that position
            pvObject = pMap->HashTable[dwHashIndex];

            // Compare the hash
            if(pfnCompare(pMap, pvObject, pvKey))
            {
                if(PtrIndex != NULL)
                    PtrIndex[0] = dwHashIndex;
                return pvObject;
            }

            // Move to the next entry
            dwHashIndex = (dwHashIndex + 1) % pMap->TableSize;
        }
    }

    // Not found, sorry
    return NULL;
}

void * Map_FindObject(PCASC_MAP pMap, void * pvKey, PDWORD PtrIndex)
{
    return Map_FindObject2(pMap, CompareIdentifier, pvKey, PtrIndex);
}

bool Map_InsertObject(PCASC_MAP pMap, void * pvNewObject, void * pvKey)
{
    void * pvExistingObject;
    DWORD dwHashIndex;

    // Verify pointer to the map
    if(pMap != NULL)
    {
        // Limit check
        if((pMap->ItemCount + 1) >= pMap->TableSize)
            return false;

        // Construct the hash index
        dwHashIndex = CalcHashIndex(pMap, pvKey);
        while(pMap->HashTable[dwHashIndex] != NULL)
        {
            // Get the pointer at that position
            pvExistingObject = pMap->HashTable[dwHashIndex];

            // Check if hash being inserted conflicts with an existing hash
            if(CompareIdentifier(pMap, pvExistingObject, pvKey))
                return false;

            // Move to the next entry
            dwHashIndex = (dwHashIndex + 1) % pMap->TableSize;
        }

        // Insert at that position
        pMap->HashTable[dwHashIndex] = pvNewObject;
        pMap->ItemCount++;
        return true;
    }

    // Failed
    return false;
}

void Map_Free(PCASC_MAP pMap)
{
    if(pMap != NULL)
    {
        CASC_FREE(pMap);
    }
}
