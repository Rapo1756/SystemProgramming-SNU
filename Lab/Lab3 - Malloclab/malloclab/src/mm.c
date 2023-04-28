/*
 * mm-2021-13194.c - malloc package using the segregated free list.
 * 
 * In this approach, a block is allocated by finding optimal free block in the
 * segregated free list. the segregated free list contains doubly linked lists
 * that each block in the lists has size from 2^(index) to 2^(index+1)-1. By
 * maintaining each list is sorted, finding optimal free block can be conducted
 * readily and quickly. Once the block is found, mark the block as allocated by
 * changing alloc info of header and footer of the block. If such free block is
 * not found, extend heap memory to allocate.
 *
 * Freeing the allocated memory is conducted by changing alloc info in header
 * and footer of the block. After adding the block in the list, coalesce adjacent
 * free block to manage the free block efficiently.
 *
 * Reallocation is implemented considering three cases excluding trivial cases:
 *  (1) If new size is less than old size, update header and footer and make
 * remaining space as free block, and coalesce.
 *  (2) If new size is larger than old size and next block is free, then check
 * if the next block is large enough to remainder of the new size. If it is,
 * coalesce two block and allocate new block of the size.
 *  (3) Otherwise, just malloc for completely new address copy the data, and
 * free original one.
 * By implementing this, fragmentation can be reduced.
 *
 * Heap consistency checker checks:
 *  - Is every block in the list marked as free?
 *  - Is there no contiquous free blocks escaping coalescing?
 *  - Is every free block in the list?
 *  - Is every block has same data in header and footer?
 *  - Is every block aligned?
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/

/* single word (4) or double word (8) alignment */
#define ALIGNMENT               8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size)             (((size) + (ALIGNMENT-1)) & ~0x7)

/* Basic constant and macros (Reference : Textbook p.857) */
#define WSIZE                   4           /* Word and header/footer size (bytes) */
#define DSIZE                   8           /* Double word size (bytes) */
#define CHUNKSIZE               (1<<12)     /* Extend heap by this amount (bytes) */
#define SEG_LIST_COUNT          12          /* Length of segregated list */
#define MIN_BLK_SIZE            (2 * DSIZE) /* Minimum size of a block */

#define MAX(x, y)               ((x) > (y) ? (x) : (y))
/* 
 * Pack a size and allocated bit into a word
 * If (alloc) == 0, the block is free block, otherwise, allocated
 */
#define PACK(size, alloc)       ((size) | (alloc))

/* Read and write a word at address ptr */
#define GET(ptr)                (*(unsigned int *)(ptr))
#define PUT(ptr, val)           (*(unsigned int *)(ptr) = (val))

/* Read the size and allocated fields from address ptr */
#define GET_SIZE(ptr)           (GET(ptr) & ~0x7)
#define GET_ALLOC(ptr)          (GET(ptr) & 0x1)

/* Given block ptr, compute address of its header and footer */
#define HDRP(blk_ptr)           ((char *)(blk_ptr) - WSIZE)
#define FTRP(blk_ptr)           ((char *)(blk_ptr) + GET_SIZE(HDRP(blk_ptr)) - DSIZE)

/* Given block ptr, compute address of previous and next blocks */
#define PREV_BLKP(blk_ptr)      ((char *)(blk_ptr) - GET_SIZE(((char *)(blk_ptr) - DSIZE)))
#define NEXT_BLKP(blk_ptr)      ((char *)(blk_ptr) + GET_SIZE(((char *)(blk_ptr) - WSIZE)))

/* Write an address at node_ptr in segregated list */
#define PUT_PTR(node_ptr, ptr)  (*(unsigned int *)(node_ptr) = (unsigned int)(ptr))

/* Read an address of previous and next node of free blocks in the list */
#define PREV_NODEP(node_ptr)    ((char *)(node_ptr) + WSIZE)
#define NEXT_NODEP(node_ptr)    ((char *)(node_ptr))

/* Read an address of previous and next free block in the free block list */
#define PREV_FREE_BLKP(ptr)     (*(char **)((char *)(ptr) + WSIZE))
#define NEXT_FREE_BLKP(ptr)     (*(char **)(ptr))

/* Read an address of list in the segregated list */
#define SEG_LIST(list_ptr, index)   (*((char **)list_ptr + index))

/* Basic functions (Reference : Textbook pp.858-861)*/
static void *extend_heap(size_t words);
static void *coalesce(void *ptr);
static void *find_fit(size_t size);
static void *place(void *ptr, size_t size);

/* Functions for segregated list */
static void insert_to_list(void *ptr, size_t size);
static void remove_from_list(void *ptr);

/* Heap consistency checker function */
int mm_check(void);

static void *seg_list_ptr = NULL;   /* Pointer for segregated list*/
static char *heap_ptr = NULL;       /* Pointer for heap */

/* 
 * mm_init - initialize the malloc package.
 * After segregated list is allocated on the heap, initialize the heap.
 */
int mm_init(void)
{
    /* Allocate memory for segregated list and initialize it */
    seg_list_ptr = mem_sbrk(SEG_LIST_COUNT * WSIZE);
    for(int list_index = 0; list_index < SEG_LIST_COUNT; list_index++)
        SEG_LIST(seg_list_ptr, list_index) = NULL;

    /* Allocate memory for heap and initialize it */
    if((heap_ptr = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_ptr, 0);                           /* Alignment padding */
    PUT(heap_ptr + (1*WSIZE), PACK(DSIZE, 1));  /* Prologue header */ 
    PUT(heap_ptr + (2*WSIZE), PACK(DSIZE, 1));  /* Prologue footer */
    PUT(heap_ptr + (3*WSIZE), PACK(0, 1));      /* Epilogue footer */
    heap_ptr += (2 * WSIZE);

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if(extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by finding enough space to fit in.
 * If not found, extenp heap and allocate block on exteded heap.
 */
void *mm_malloc(size_t size)
{
    size_t adjusted_size;   /* Adjusted block size */
    char *blk_ptr;      /* Block pointer */

    /* Ignore spurious requests */
    if(size == 0) return NULL;

    /* Adjust block size to include overhead and alignment reqs */
    if(size <= DSIZE)
        adjusted_size = MIN_BLK_SIZE;
    else
        adjusted_size = DSIZE * ((size + DSIZE + DSIZE - 1) / DSIZE);

    /* Search the free list for a fit */
    if((blk_ptr = find_fit(adjusted_size)) != NULL)
        return place(blk_ptr, adjusted_size);

    /* No fit found. Get more memory and place the block */
    if((blk_ptr = extend_heap(MAX(adjusted_size, CHUNKSIZE)/ WSIZE)) == NULL)
        return NULL;
    return place(blk_ptr, adjusted_size);
}

/*
 * mm_free - mark the block as free and insert it to the list.
 * coalesce if it is needed.
 */
void mm_free(void *blk_ptr)
{
    size_t size = GET_SIZE(HDRP(blk_ptr));

    /* Mark the block as free */
    PUT(HDRP(blk_ptr), PACK(size, 0));
    PUT(FTRP(blk_ptr), PACK(size, 0));
    /* Insert this block to the list */
    insert_to_list(blk_ptr, size);
    coalesce(blk_ptr);
}

/*
 * mm_realloc - reallocate memory considering several cases:
 *  Default behavior :  If size == 0, free it. If blk_ptr == NULL, malloc it.
 *                      If size is same, just return original one.
 *  Case 1: If new size is less than old size, allocate on the original block.
 *  Case 2: If new size is larger than old size, next block is free block,
 *          and the sum of size of two block is less than new size,
 *          change original block size to be able to hold the new one.
 *  Case 3: Otherwise, we should use completely different address,
 *          so malloc new address and copy original one into new address,
 *          and free old one.
 * It is difficult to use the case of that the previous block is free, 
 * since copying into the previous block is risky.
 */
void *mm_realloc(void *blk_ptr, size_t size)
{
    /* If size is zero, just free it */
    if (size == 0) {
        mm_free(blk_ptr);
        return NULL;
    }
    /* If blk_ptr is NULL, just malloc it */
    if (blk_ptr == NULL)
        return mm_malloc(size);

    /* Get aligned size and block size of blk_ptr excluding header/footer */
    size_t align_size = ALIGN(size);
    size_t org_size = GET_SIZE(HDRP(blk_ptr)) - DSIZE;

    /* If two sizes are same, just return blk_ptr */
    if (align_size == org_size)
        return blk_ptr;

    /* Case 1 : If new size is less than original size, the original block can hold the new one */
    else if (align_size < org_size) {
        /* If new size is too large for remaining space to be a free block, just return original one */
        if (org_size - align_size < MIN_BLK_SIZE)
            return blk_ptr;

        /* Adjust block size and make remaining space as free block */
        PUT(HDRP(blk_ptr), PACK(align_size + DSIZE, 1));
        PUT(FTRP(blk_ptr), PACK(align_size + DSIZE, 1));
        void *next_blk_ptr = NEXT_BLKP(blk_ptr);
        PUT(HDRP(next_blk_ptr), PACK(org_size - align_size, 0));
        PUT(FTRP(next_blk_ptr), PACK(org_size - align_size, 0));
        insert_to_list(next_blk_ptr, GET_SIZE(HDRP(next_blk_ptr)));
        coalesce(next_blk_ptr);
        return blk_ptr;
    }
    /* Case 2 : If next block is free, check if it is enough to hold the new one */
    else if ((NEXT_BLKP(blk_ptr) != NULL) && !GET_ALLOC(HDRP(NEXT_BLKP(blk_ptr)))) {
        size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(blk_ptr)));
        /* If the sum of two block's sizes is enough to hold the new one, allocate it */
        if (next_size + org_size >= align_size) {
            remove_from_list(NEXT_BLKP(blk_ptr));
            /* If new size is too large for remaining space to be a free block, just use all of it */
            if (next_size + org_size - align_size < MIN_BLK_SIZE) {
                PUT(HDRP(blk_ptr), PACK(org_size + DSIZE + next_size, 1));
                PUT(FTRP(blk_ptr), PACK(org_size + DSIZE + next_size, 1));
                return blk_ptr;
            }
            /* Allocate new block with aligned size and make remaining space to free block */
            else {
                PUT(HDRP(blk_ptr), PACK(align_size + DSIZE, 1));
                PUT(FTRP(blk_ptr), PACK(align_size + DSIZE, 1));
                void *next_ptr = NEXT_BLKP(blk_ptr);
                PUT(HDRP(next_ptr), PACK(org_size + next_size - align_size, 0));
                PUT(FTRP(next_ptr), PACK(org_size + next_size - align_size, 0));
                insert_to_list(next_ptr, GET_SIZE(HDRP(next_ptr))); 
                
                /* there is no need to coalesce since next block was a free block */
                return blk_ptr;
            }
        }
    }

    /* Case 3 : since blk_ptr is not sufficient to hold the new one, just use malloc and free */
    void *new_blk_ptr = mm_malloc(size);
    if (new_blk_ptr == NULL)
        return NULL;
    memcpy(new_blk_ptr, blk_ptr, org_size);
    mm_free(blk_ptr);
    return new_blk_ptr;
}

/*
 * extend_heap: extend the heap by requested words.
 */
static void* extend_heap(size_t words)
{
    char *blk_ptr;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(blk_ptr = mem_sbrk(size)) == -1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(blk_ptr), PACK(size, 0));          /* Free block header */
    PUT(FTRP(blk_ptr), PACK(size, 0));          /* Free block footer */
    PUT(HDRP(NEXT_BLKP(blk_ptr)), PACK(0, 1));  /* New epilogue header */
    insert_to_list(blk_ptr, size);          /* Insert the free block to the list */

    /* Coalesce if the previous block was free */
    return coalesce(blk_ptr);
}


/*
 * coalesce - check if previous/next block is free and coalesce them.
 * Before coalescing, removing them from the list should be preceded,
 * and new free block should be inserted to the list after coalescing.
 */
static void *coalesce(void *blk_ptr)
{
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(blk_ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(blk_ptr)));
    size_t size = GET_SIZE(HDRP(blk_ptr));

    if (prev_alloc && next_alloc)               /* Case 1 */
        return blk_ptr;
    else if (prev_alloc && !next_alloc) {       /* Case 2 */
        remove_from_list(blk_ptr);
        remove_from_list(NEXT_BLKP(blk_ptr));
        size += GET_SIZE(HDRP(NEXT_BLKP(blk_ptr)));
        PUT(HDRP(blk_ptr), PACK(size, 0));
        PUT(FTRP(blk_ptr), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc) {       /* Case 3 */
        remove_from_list(PREV_BLKP(blk_ptr));
        remove_from_list(blk_ptr);
        size += GET_SIZE(HDRP(PREV_BLKP(blk_ptr)));
        PUT(FTRP(blk_ptr), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(blk_ptr)), PACK(size, 0));
        blk_ptr = PREV_BLKP(blk_ptr);
    }
    else {                                      /* Case 4 */
        remove_from_list(PREV_BLKP(blk_ptr));
        remove_from_list(blk_ptr);
        remove_from_list(NEXT_BLKP(blk_ptr));
        size += GET_SIZE(HDRP(PREV_BLKP(blk_ptr)))
                + GET_SIZE(FTRP(NEXT_BLKP(blk_ptr)));
        PUT(HDRP(PREV_BLKP(blk_ptr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(blk_ptr)), PACK(size, 0));
        blk_ptr = PREV_BLKP(blk_ptr);
    }

    insert_to_list(blk_ptr, size);
    return blk_ptr;
}

/*
 * find_fit - search a free block that the give size can fit in.
 * As the segregated free list has lists for the free blocks having certain size
 * as its elements, find the index for suck list first, and find a node that the
 * given size can fit in. If such free block is not found, return NULL.
 */
static void *find_fit(size_t size)
{
    size_t size_copy = size;   /* Copy of the size */
    void *free_blk_ptr = NULL;
    /* Find index of the list that the size can be fit in */
    for (int list_index = 0; list_index < SEG_LIST_COUNT; list_index++){
        /* If list_index is last of the list, or there are some free blocks in the list that the size is fit in */
        if ((list_index == SEG_LIST_COUNT - 1) ||
            ((size_copy <= 1) && (SEG_LIST(seg_list_ptr, list_index) != NULL))) {
            free_blk_ptr = SEG_LIST(seg_list_ptr, list_index);
            /* Loop segregated block while the size is bigger than the free size */
            while ((free_blk_ptr != NULL) && (size > GET_SIZE(HDRP(free_blk_ptr))))
                free_blk_ptr = NEXT_FREE_BLKP(free_blk_ptr);
            /* If the block is found, break the loop */
            if (free_blk_ptr != NULL)
                break;
        }
        size_copy >>= 1;
    }
    return free_blk_ptr;    /* free_blk_ptr is NULL iff the free block is not found */
}

/*
 * place - allocate a block of the size in the free block.
 * There is no need to coalesce since the block was free block.
 */

static void *place(void *blk_ptr, size_t size)
{
    size_t block_size = GET_SIZE(HDRP(blk_ptr));    /* Block size of free block */
    void *next_blk_ptr = NULL;
    remove_from_list(blk_ptr);

    /* If the remaining space is large enough to be free block, make it free block */
    if ((block_size - size) >= (MIN_BLK_SIZE)) {
        PUT(HDRP(blk_ptr), PACK(block_size - size, 0));
        PUT(FTRP(blk_ptr), PACK(block_size - size, 0));
        next_blk_ptr = NEXT_BLKP(blk_ptr);
        PUT(HDRP(next_blk_ptr), PACK(size, 1));
        PUT(FTRP(next_blk_ptr), PACK(size, 1));
        insert_to_list(blk_ptr, block_size - size);
        return next_blk_ptr;
    }
    /* Otherwise, use all of the block */
    else {
        PUT(HDRP(blk_ptr), PACK(block_size, 1));
        PUT(FTRP(blk_ptr), PACK(block_size, 1));
    }
    return blk_ptr;
}

/*
 * insert_to_list - insert a free block into the segregated list.
 * First find the list that the size fit in, and find where it should be inserted.
 * After finding, insert in the list and link with previous/next nodes.
 */
static void insert_to_list(void *blk_ptr, size_t size)
{
    int size_copy = size;       /* Copy of the block size */
    int list_index;
    /* Find list_index for the list that the size can be contained */
    for (list_index = 0; (list_index < SEG_LIST_COUNT - 1) && (size_copy > 1); list_index++)
        size_copy >>= 1;

    /* Get next_ptr for list_index */
    void *next_ptr = SEG_LIST(seg_list_ptr, list_index);
    void *prev_ptr = NULL;

    /* Loop to find insertion location */
    while ((next_ptr != NULL) && (size > GET_SIZE(HDRP(next_ptr)))) {
        prev_ptr = next_ptr;
        next_ptr = NEXT_FREE_BLKP(next_ptr);
    }
    if (next_ptr != NULL) {
        /* If prev node and next node exist, insert ptr between them */
        if (prev_ptr != NULL) {
            PUT_PTR(PREV_NODEP(blk_ptr), prev_ptr);
            PUT_PTR(NEXT_NODEP(prev_ptr), blk_ptr);
            PUT_PTR(PREV_NODEP(next_ptr), blk_ptr);
            PUT_PTR(NEXT_NODEP(blk_ptr), next_ptr);
        }
        /* If prev node does not exist, add ptr at the head of the list */
        else {
            PUT_PTR(PREV_NODEP(blk_ptr), NULL);
            PUT_PTR(NEXT_NODEP(blk_ptr), next_ptr);
            PUT_PTR(PREV_NODEP(next_ptr), blk_ptr);
            SEG_LIST(seg_list_ptr, list_index) = blk_ptr;

        }
    }
    else {
        /* If next node does not exist, add ptr at the tail of the list */
        if (prev_ptr != NULL){
            PUT_PTR(PREV_NODEP(blk_ptr), prev_ptr);
            PUT_PTR(NEXT_NODEP(blk_ptr), NULL);
            PUT_PTR(NEXT_NODEP(prev_ptr), blk_ptr);
        }
        /* If the list for the list_index is empty, add ptr to the list */
        else {
            SEG_LIST(seg_list_ptr, list_index) = blk_ptr;
            PUT_PTR(PREV_NODEP(blk_ptr), NULL);
            PUT_PTR(NEXT_NODEP(blk_ptr), NULL);
        }
    }
}

/*
 * remove_from_list - remove a free block from the segregated list.
 * First find the list that the size fit in, remove it, and relink list.
 */

static void remove_from_list(void *blk_ptr)
{
    size_t size = GET_SIZE(HDRP(blk_ptr));

    /* If there exists prev node of ptr */
    if ((PREV_FREE_BLKP(blk_ptr) != NULL)) {
        /* Set next node of the prev node as next node of ptr */
        PUT_PTR(NEXT_NODEP(PREV_FREE_BLKP(blk_ptr)), NEXT_FREE_BLKP(blk_ptr));

        /* If next node exists, set prev node of the next node as prev node of ptr */
        if (NEXT_FREE_BLKP(blk_ptr) != NULL)
            PUT_PTR(PREV_NODEP(NEXT_FREE_BLKP(blk_ptr)), PREV_FREE_BLKP(blk_ptr));
    }
    /* If ptr is the head of the list */
    else {
        int list_index;
        /* Find the index for the list containing ptr */
        for (list_index = 0; (list_index < SEG_LIST_COUNT - 1) && (size > 1); list_index++) 
            size >>= 1;
        /* Set the head of the list as next node of ptr */
        SEG_LIST(seg_list_ptr, list_index) = NEXT_FREE_BLKP(blk_ptr);

        /* If next node exists, set prev node of the next node as NULL */
        if (SEG_LIST(seg_list_ptr, list_index) != NULL)
            PUT_PTR(PREV_NODEP(SEG_LIST(seg_list_ptr, list_index)), NULL);

    }
}

/*
 * mm_check - heap consistency checker. It returns 0 if there is any error, 1 otherwise.
 * It checks 5 cases:
 *  - Is every block in the list marked as free?
 *  - Is there no contiquous free blocks escaping coalescing?
 *  - Is every free block in the list?
 *  - Is every block has same data in header and footer?
 *  - Is every block aligned?
 */

int mm_check(void) 
{

    /* Check if the blocks in the free list marked as free */
    for (int list_index = 0; list_index < SEG_LIST_COUNT; list_index++) {
        void* ptr = SEG_LIST(seg_list_ptr, list_index);
        while (ptr != NULL) { 
            if (GET_ALLOC(ptr)) {
                printf("A free block is not marked as free!\n");
	            return 0;
            }
            ptr = NEXT_FREE_BLKP(ptr);
        }
    }
    void* ptr = heap_ptr;
    while (GET(HDRP(ptr)) != PACK(0, 1)) {
        if (!GET_ALLOC(HDRP(ptr))) {
            /* Check if two contiguous blocks are free */
            if(!GET_ALLOC(HDRP(NEXT_BLKP(ptr)))){
                printf("Two contiguous blocks escaped coalescing!\n");
                return 0;
            }
            /* Check if a free block is not in the list */
            void* free_blk_ptr = NULL;
            for(int list_index = 0;
                    list_index < SEG_LIST_COUNT && free_blk_ptr != ptr; list_index++) {
                free_blk_ptr = SEG_LIST(seg_list_ptr, list_index);
                while (free_blk_ptr != NULL && free_blk_ptr != ptr)
                    free_blk_ptr = NEXT_FREE_BLKP(free_blk_ptr);
            }
 	        if (free_blk_ptr != ptr) {
	            printf("A free block is not in the segregated list!\n");
                return 0;
            }
        }
        /* Check if block has proper size */
        if (ptr == NULL) {
            printf("A block has inproper size!\n");
        }
        /* Check if the data in header/footer are same */
        if (GET(HDRP(ptr)) != GET(FTRP(ptr))) {
            printf("Header/footer have different information!\n");
            return 0;
        }
        /* Check if the block is aligned */
        if ((unsigned int)ptr % DSIZE) {
            printf("A block is not aligned!\n");
            return 0;
        }
        ptr = NEXT_BLKP(ptr);
    }
    return 1;
}

