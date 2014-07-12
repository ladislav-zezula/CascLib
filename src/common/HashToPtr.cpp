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
    DWORD dwHash = pbHash[0];

    // Construct the hash from the first 4 digits
    dwHash = (dwHash << 0x08) | pbHash[1];
    dwHash = (dwHash << 0x08) | pbHash[2];
    dwHash = (dwHash << 0x08) | pbHash[3];

    return (dwHash >> pMap->HashShift);
}

static PMAP_SUBTABLE CreateHashSubtable(PMAP_SUBTABLE pOldSubTable)
{
    PMAP_SUBTABLE pNewSubTable;
    size_t nItemCountMax = 0x10;               // Default number of items
    size_t cbToAllocate;

    // If we have an old table, take the number of items from there
    if(pOldSubTable != NULL)
        nItemCountMax = pOldSubTable->ItemCountMax * 2;

    // Allocate new subtable
    cbToAllocate = sizeof(MAP_SUBTABLE) + (nItemCountMax * sizeof(LPBYTE));
    pNewSubTable = (PMAP_SUBTABLE)CASC_ALLOC(LPBYTE, cbToAllocate);
    if(pNewSubTable != NULL)
    {
        // Initialize the new subtable
        pNewSubTable->ItemCountMax = nItemCountMax;
        pNewSubTable->ItemCount = 0;

        // If we had an old subtable, copy it to the new one
        if(pOldSubTable != NULL)
        {
            memcpy(pNewSubTable + 1, pOldSubTable + 1, (pOldSubTable->ItemCount * sizeof(LPBYTE)));
            pNewSubTable->ItemCount = pOldSubTable->ItemCount;
            CASC_FREE(pOldSubTable);
        }
    }

    return pNewSubTable;
}

static bool SubTable_InsertObject(PMAP_HASH_TO_PTR pMap, PMAP_SUBTABLE pSubTable, LPBYTE pbNewHash)
{
    LPBYTE * ItemArray = (LPBYTE *)(pSubTable + 1);
    int nResult;

    // It is guaranteed that there is space in the subtable
    assert(pSubTable->ItemCount < pSubTable->ItemCountMax);

    // Check if the hash is already there
    for(size_t i = 0; i < pSubTable->ItemCount; i++)
    {
        // Compare the hash being inserted with the hash at the index
        // If the hashes are equal, do not insert the item and exit directly
        nResult = memcmp(ItemArray[i], pbNewHash, pMap->KeyLength);
        if(nResult == 0)
            return false;

        // If the hash is less than the item at the entry,
        // we will move all next items by one position and insert that hash
        if(nResult > 0)
        {
            memmove(ItemArray + i + 1, ItemArray + i, (pSubTable->ItemCount - i) * sizeof(LPBYTE));
            ItemArray[i] = pbNewHash;
            pSubTable->ItemCount++;
            return true;
        }
    }

    // If we compared the entire array, then it means that
    // the inserted hash is greater than the greatest existing hash
    ItemArray[pSubTable->ItemCount++] = pbNewHash;
    return true;
}

static void SubTable_EnumObjects(PMAP_HASH_TO_PTR pMap, PMAP_SUBTABLE pSubTable, void ** ppvArray, size_t nIndex)
{
    LPBYTE * ItemArray = (LPBYTE *)(pSubTable + 1);

    // Move to that array entry
    ppvArray += nIndex;

    // Copy all pointers (adjust them to object pointer)
    for(size_t i = 0; i < pSubTable->ItemCount; i++)
        ppvArray[i] = ItemArray[i] - pMap->MemberOffset;
}

static LPBYTE SubTable_FindObject(PMAP_HASH_TO_PTR pMap, PMAP_SUBTABLE pSubTable, LPBYTE pbHash)
{
    LPBYTE * ItemArray = (LPBYTE *)(pSubTable + 1);

    // Check if the hash is already there
    for(size_t i = 0; i < pSubTable->ItemCount; i++)
    {
        if(!memcmp(ItemArray[i], pbHash, pMap->KeyLength))
            return ItemArray[i];
    }

    return NULL;
}

//-----------------------------------------------------------------------------
// Public functions

PMAP_HASH_TO_PTR MapHashToPtr_Create(DWORD dwMaxItems, DWORD dwKeyLength, DWORD dwMemberOffset)
{
    PMAP_HASH_TO_PTR pMap;
    size_t cbToAllocate;
    DWORD dwHashShift = 0x08;
    DWORD dwHashMask = 0x00FFFFFF;

    // Sanity check
    assert(dwMaxItems <= dwHashMask);

    // Get the hash table size
    while((dwHashMask >> 1) > dwMaxItems)
    {
        dwHashMask = dwHashMask >> 1;
        dwHashShift++;
    }

    // Allocate new map for the objects
    cbToAllocate = sizeof(MAP_HASH_TO_PTR) + ((dwHashMask + 1) * sizeof(void *));
    pMap = (PMAP_HASH_TO_PTR)CASC_ALLOC(LPBYTE, cbToAllocate);
    if(pMap != NULL)
    {
        memset(pMap, 0, cbToAllocate);
        pMap->KeyLength = dwKeyLength;
        pMap->HashShift = dwHashShift;
        pMap->TableSize = dwHashMask + 1;
        pMap->MemberOffset = dwMemberOffset;
    }

    // Return the allocated map
    return pMap;
}

size_t MapHashToPtr_EnumObjects(PMAP_HASH_TO_PTR pMap, void **ppvArray)
{
    PMAP_SUBTABLE pSubTable;
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
                // If that item is a subtable, we need to enumerate the subtable
                if(IS_SUBTABLE_POINTER(pMap->HashTable[i]))
                {
                    pSubTable = PTR_FROM_TABLE_ITEM(pMap->HashTable[i]);
                    SubTable_EnumObjects(pMap, pSubTable, ppvArray, nIndex);
                    nIndex += pSubTable->ItemCount;
                }
                else
                {
                    ppvArray[nIndex++] = (LPBYTE)pMap->HashTable[i] - pMap->MemberOffset;
                }
            }
        }
    }

    return pMap->ItemCount;
}

void * MapHashToPtr_FindObject(PMAP_HASH_TO_PTR pMap, LPBYTE pbHash)
{
    PMAP_SUBTABLE pSubTable;
    LPBYTE pbFoundHash = NULL;
    DWORD HashIndex;
    void * pvPointer;

    // Verify pointer to the map
    if(pMap != NULL)
    {
        // Construct the main index
        HashIndex = CalcHashIndex(pMap, pbHash);
        pvPointer = pMap->HashTable[HashIndex];

        // Is the item at the index valid?
        if(pvPointer != NULL)
        {
            // Is it a subtable?
            if(IS_SUBTABLE_POINTER(pvPointer))
            {
                pSubTable = PTR_FROM_TABLE_ITEM(pvPointer);
                pbFoundHash = SubTable_FindObject(pMap, pSubTable, pbHash);
            }
            else
            {
                // Compare the hash
                if(!memcmp(pvPointer, pbHash, pMap->KeyLength))
                    pbFoundHash = (LPBYTE)pvPointer;
            }
        }

        // If we found a hash in the table, subtract the member offset
        if(pbFoundHash != NULL)
            pbFoundHash -= pMap->MemberOffset;
    }

    // Not found, sorry
    return pbFoundHash;
}

DWORD dwCollisions = 0;

bool MapHashToPtr_InsertObject(PMAP_HASH_TO_PTR pMap, LPBYTE pbNewHash)
{
    PMAP_SUBTABLE pSubTable;
    LPBYTE pbOldHash;
    DWORD HashIndex;
    bool bResult;

    // Verify pointer to the map
    if(pMap != NULL)
    {
        // Construct the hash index
        HashIndex = CalcHashIndex(pMap, pbNewHash);

        // If that item is empty, we can insert the new hash
        if(pMap->HashTable[HashIndex] != NULL)
        {
            if(IS_SUBTABLE_POINTER(pMap->HashTable[HashIndex]))
            {
                // Retrieve the pointer to the subtable
                pSubTable = PTR_FROM_TABLE_ITEM(pMap->HashTable[HashIndex]);

                // Check if there is enough space in the subtable
                if(pSubTable->ItemCount >= pSubTable->ItemCountMax)
                {
                    // Create new subtable with the increased space
                    pSubTable = CreateHashSubtable(pSubTable);
                    if(pSubTable == NULL)
                        return false;

                    // Set the subtable to the main table
                    pMap->HashTable[HashIndex] = TABLE_ITEM_FROM_PTR(pSubTable);
                }
            }
            else
            {
                // Remember the old hash
                pbOldHash = (LPBYTE)pMap->HashTable[HashIndex];

                // Check if hash being inserted conflicts with an existing hash
                if(!memcmp(pbNewHash, pbOldHash, pMap->KeyLength))
                    return false;

                // Create new subtable at the position
                pSubTable = CreateHashSubtable(NULL);
                if(pSubTable == NULL)
                    return false;

                // Insert the subtable to the main table
                pMap->HashTable[HashIndex] = TABLE_ITEM_FROM_PTR(pSubTable);
                dwCollisions++;

                // Insert the old hash to the subtable
                SubTable_InsertObject(pMap, pSubTable, pbOldHash);
            }

            // Insert the new hash to the subtable
            if((bResult = SubTable_InsertObject(pMap, pSubTable, pbNewHash)) == true)
                pMap->ItemCount++;
            return bResult;                
        }
        else
        {
            pMap->HashTable[HashIndex] = pbNewHash;
            pMap->ItemCount++;
            return true;
        }
    }

    // No free space
    return false;
}

void MapHashToPtr_Free(PMAP_HASH_TO_PTR pMap)
{
    if(pMap != NULL)
    {
        // Free all subtables in the main table
        for(size_t i = 0; i < pMap->TableSize; i++)
        {
            if(IS_SUBTABLE_POINTER(pMap->HashTable[i]))
            {
                CASC_FREE(PTR_FROM_TABLE_ITEM(pMap->HashTable[i]));
            }
        }

        // Free the table item itself
        CASC_FREE(pMap);
    }
}
