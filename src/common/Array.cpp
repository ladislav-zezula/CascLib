/*****************************************************************************/
/* DynamicArray.cpp                       Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Description:                                                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 30.10.15  1.00  Lad  The first version of DynamicArray.cpp                */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

//-----------------------------------------------------------------------------
// Local functions

static bool EnlargeArray(PCASC_ARRAY pArray, size_t NewItemCount)
{
    char * NewItemArray;
    size_t ItemCountMax;

    // We expect the array to be already allocated
    assert(pArray->ItemArray != NULL);
    assert(pArray->ItemCountMax != 0);
    
    // Shall we enlarge the table?
    if(NewItemCount > pArray->ItemCountMax)
    {
        // Calculate new table size
        ItemCountMax = pArray->ItemCountMax;
        while(ItemCountMax < NewItemCount)
            ItemCountMax = ItemCountMax << 1;

        // Allocate new table
        NewItemArray = CASC_REALLOC(char, pArray->ItemArray, pArray->ItemSize * ItemCountMax);
        if(NewItemArray == NULL)
            return false;

        // Set the new table size
        pArray->ItemCountMax = ItemCountMax;
        pArray->ItemArray = NewItemArray;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Public functions

int Array_Create_(PCASC_ARRAY pArray, size_t ItemSize, size_t ItemCountMax)
{
    pArray->ItemArray = CASC_ALLOC(char, (ItemSize * ItemCountMax));
    if(pArray->ItemArray == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    pArray->ItemCountMax = ItemCountMax;
    pArray->ItemCount = 0;
    pArray->ItemSize = ItemSize;
    return ERROR_SUCCESS;
}

void Array_ZeroItems(PCASC_ARRAY pArray)
{
    memset(pArray->ItemArray, 0, pArray->ItemCountMax + pArray->ItemSize);
}

void * Array_Insert(PCASC_ARRAY pArray, const void * NewItems, size_t NewItemCount)
{
    char * NewItemPtr;

    // Try to enlarge the buffer, if needed
    if(!EnlargeArray(pArray, pArray->ItemCount + NewItemCount))
        return NULL;
    NewItemPtr = pArray->ItemArray + (pArray->ItemCount * pArray->ItemSize);

    // Copy the old item(s), if any
    if(NewItems != NULL)
        memcpy(NewItemPtr, NewItems, (NewItemCount * pArray->ItemSize));

    // Increment the size of the array
    pArray->ItemCount += NewItemCount;
    return NewItemPtr;
}

void * Array_ItemAt(PCASC_ARRAY pArray, size_t ItemIndex)
{
    assert(ItemIndex < pArray->ItemCount);
    return pArray->ItemArray + (ItemIndex * pArray->ItemSize);
}

// Inserts an item at a given index. If there is not enough items in the array,
// the array will be enlarged. Should any gaps to be created, the function will zero them
void * Array_InsertAt(PCASC_ARRAY pArray, size_t ItemIndex)
{
    void * NewItemPtr;
    size_t AddedItemCount;

    // Is there enough items?
    if (ItemIndex > pArray->ItemCount)
    {
        // Capture the new item count
        AddedItemCount = ItemIndex - pArray->ItemCount;

        // Insert the amount of items
        NewItemPtr = Array_Insert(pArray, NULL, AddedItemCount);
        if (NewItemPtr == NULL)
            return NULL;

        // Zero the inserted items
        memset(NewItemPtr, 0, pArray->ItemSize * AddedItemCount);
    }

    // Is the item already inserted?
    if (ItemIndex == pArray->ItemCount)
    {
        Array_Insert(pArray, NULL, 1);
    }

    // Return the item at a given index
    return Array_ItemAt(pArray, ItemIndex);
}

bool Array_CheckMember(PCASC_ARRAY pArray, const void * ArrayPtr)
{
    LPBYTE pbArrayStart = (LPBYTE)pArray->ItemArray;
    LPBYTE pbArrayPtr = (LPBYTE)ArrayPtr;
    LPBYTE pbArrayEnd = pbArrayStart + (pArray->ItemCount * pArray->ItemSize);

    return (pbArrayStart <= pbArrayPtr && pbArrayPtr < pbArrayEnd);
}

size_t Array_IndexOf(PCASC_ARRAY pArray, const void * ArrayPtr)
{
    char * ArrayItem = (char *)ArrayPtr;

    assert(pArray->ItemArray <= ArrayItem && ArrayItem <= pArray->ItemArray + (pArray->ItemCount * pArray->ItemSize));
    return ((ArrayItem - pArray->ItemArray) / pArray->ItemSize);
}

void Array_Free(PCASC_ARRAY pArray)
{
    if(pArray != NULL && pArray->ItemArray != NULL)
    {
        CASC_FREE(pArray->ItemArray);
    }
}
