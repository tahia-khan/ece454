/*
 * The heap is initialized to 6 blocks with the following structure:
 * [0|PR HDR|PR PREV_PTR|PR NEXT_PTR|PR FTR|EPI]
 *
 * The free list is initialized to point at the prologue's PREV_PTR and
 * free blocks are inserted at the head of the free list. The prologue
 * will always be the tail of the list (so that we can check for the
 * allocated_bit == 1 when iterating through the free list, since all other
 * blocks will have allocated_bit == 0).
 *
 * Newly freed blocks are added to the free list at the end of coalescing. 
 * Freed blocks are removed from the free list during allocation in first fit
 * order.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Farfisa",
    /* First member's full name */
    "Tahia Khan",
    /* First member's email address */
    "tahia.khan@mail.utoronto.ca",
    /* Second member's full name (leave blank if none) */
    "Miro Kuratczyk",
    /* Second member's email address (leave blank if none) */
    "miro.kuratczyk@mail.utoronto.ca"
};

/*************************************************************************
 * Basic Constants and Macros
 * You are not required to use these macros but may find them helpful.
*************************************************************************/
#define WSIZE       sizeof(void *)            /* word size (bytes) */
#define DSIZE       (2 * WSIZE)            /* doubleword size (bytes) */
#define CHUNKSIZE   (1<<7)      /* initial heap size (bytes) */

#define MAX(x,y) ((x) > (y)?(x) :(y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)          (*(uintptr_t *)(p))
#define PUT(p,val)      (*(uintptr_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)     (GET(p) & ~(DSIZE - 1))
#define GET_ALLOC(p)    (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)        ((char *)(bp) - WSIZE)
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Added macros */
#define PREV_FREE_BLKP(bp)  (*(void **)(bp))
#define NEXT_FREE_BLKP(bp)  (*(void **)(bp + WSIZE))
#define HEAD_OF_SEG(bp)     (*(void **)(bp))

/* Read and write a pointer at address p */
#define GETP(p)          (*(uintptr_t **)(p))
#define PUTP(p,val)      (*(uintptr_t **)(p) = (uintptr_t *)val)

#define NUM_SEG_LISTS       (14)

int mm_check(void);
int mm_check_init(void);
void  print_block(void * bp);
void  print_free_list(void);
void  print_implicit_list(void);
void  splice_free_block(void * bp);
void  splice_buddy(void * bp);
void  add_buddy(void *bp);
void* get_seg_list(size_t size);
void * find_fit_buddy(size_t asize);
size_t next_power_of_two(size_t v);

void* heap_listp = NULL;
void* free_listp = NULL;

//void*buddy_listp = NULL; 
//should be a linked list of pointers which point to the head
//of segregated buddy lists
//1, 2, 4, 8, 16, 32, 64, 128, 256,512, 1024, other; 12 in total


/**********************************************************
 * mm_init
 * Initialize the heap, including "allocation" of the
 * prologue and epilogue
 *
 * This sets up a heap of 6 blocks with the following 
 * structure:
 * [0|PR HDR|PR PREV_PTR|PR NEXT_PTR|PR FTR|EPI]
 **********************************************************/
 int mm_init(void)
 {
   if ((heap_listp = mem_sbrk(18*WSIZE)) == (void *)-1)
         return -1;
   /*
     PUT(heap_listp, 0);                         // alignment padding
     PUT(heap_listp + (1 * WSIZE), PACK(2*DSIZE, 1));   // prologue header
     PUT(heap_listp + (2 * WSIZE), 0);   // prologue PREV_PTR
     PUT(heap_listp + (3 * WSIZE), 0);   // prologue NEXT_PTR
     PUT(heap_listp + (4 * WSIZE), PACK(2*DSIZE, 1));   // prologue footer
     PUT(heap_listp + (5 * WSIZE), PACK(0, 1));    // epilogue header
     heap_listp += DSIZE;
     free_listp= heap_listp; 
   */

   /* Initialize heap list */
   int i;
   for (i = 0; i < NUM_SEG_LISTS + 4; i++) {
       PUT(heap_listp + (i * WSIZE), 0); 
   }

   /* Initialize prologue and epilogue */
   PUT(heap_listp + ( 1 * WSIZE), PACK(16*WSIZE, 1));   // prologue header
   PUT(heap_listp + (16 * WSIZE), PACK(16*WSIZE, 1));   // prologue footer
   PUT(heap_listp + (17 * WSIZE), PACK(0, 1));          // epilogue header

   heap_listp += DSIZE;
   free_listp= heap_listp; /* Initialize free list to point at the prologue */

   mm_check_init();
   return 0;
   
 }

/**********************************************************
 * coalesce
 * Covers the 4 cases discussed in the text:
 * - both neighbours are allocated
 * - the next block is available for coalescing
 * - the previous block is available for coalescing
 * - both neighbours are available for coalescing
 **********************************************************/
void *coalesce_buddy(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    size_t size_p = GET_SIZE(HDRP(PREV_BLKP(bp)));
    size_t size_n = GET_SIZE(HDRP(NEXT_BLKP(bp)));

    if (prev_alloc && next_alloc) {       /* Case 1 afa*/
        printf("case1\n");
    }

    else if (!prev_alloc && !next_alloc && size == size_p && size == size_n) { /* Case 4 fff */
        printf("case4\n");
        size += size_p + size_n;
        splice_buddy(PREV_BLKP(bp));
        splice_buddy(NEXT_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size,0));
        bp = (PREV_BLKP(bp));
    }

    else if (prev_alloc && !next_alloc && size == size_n) { /* Case 2 aff*/
        printf("case2\n");
        size += size_n;
        splice_buddy(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc && size == size_p) { /* Case 3 ffa*/
        printf("case3\n");
        size += size_p;
        splice_buddy(PREV_BLKP(bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = (PREV_BLKP(bp));
    }

    /* adjust free list */
    add_buddy(bp);

    return bp;
}

void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {       /* Case 1 afa*/
    }

    else if (prev_alloc && !next_alloc) { /* Case 2 aff*/
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        splice_free_block(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc) { /* Case 3 ffa*/
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        splice_free_block(PREV_BLKP(bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = (PREV_BLKP(bp));
    }

    else {            /* Case 4 fff*/
        size += GET_SIZE(HDRP(PREV_BLKP(bp)))  +
            GET_SIZE(FTRP(NEXT_BLKP(bp)))  ;
        splice_free_block(PREV_BLKP(bp));
        splice_free_block(NEXT_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size,0));
        bp = (PREV_BLKP(bp));
    }

    /* adjust free list */
    PREV_FREE_BLKP(bp) = NULL;
    NEXT_FREE_BLKP(bp) = free_listp;
    PREV_FREE_BLKP(free_listp) = bp;
    free_listp = bp;

    return bp;
}

/**********************************************************
 * extend_heap
 * Extend the heap by "words" words, maintaining alignment
 * requirements of course. Free the former epilogue block
 * and reallocate its new header
 **********************************************************/
void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignments */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ( (bp = mem_sbrk(size)) == (void *)-1 )
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));                // free block header
    PUT(FTRP(bp), PACK(size, 0));                // free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));        // new epilogue header

    return coalesce_buddy(bp);
}


/**********************************************************
 * find_fit
 * Traverse the heap searching for a block to fit asize
 * Return NULL if no free blocks can handle that size
 * Assumed that asize is aligned
 **********************************************************/
void * find_fit_buddy(size_t asize)
{
    void *bp;
    bp = get_seg_list(asize);
    printf("FIND_FIT %zu \n", asize);

    if (!GET(bp) && asize >= 4096){ /* no suitable free blocks in any seg list -> should extend heap */
        return NULL;
    }
    if (!GET(bp)) /* no free blocks in this seg list -> try next size */
        bp = find_fit_buddy(next_power_of_two(asize+1));
    else {   /* there's free blocks of this size -> take head of list */
        bp = HEAD_OF_SEG(bp);
        splice_buddy(bp);
        return bp;
    }

    if (GET_SIZE(HDRP(bp)) != asize) { /* should split into buddies if we found a larger size */
        //printf("should split the buddy\n");
        //TODO: SPLIT THE BUDDY
    }
    return bp;
}

void * find_fit(size_t asize)
{
    /*void *bp;
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
    {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
        {
            return bp;
        }
    }
    return NULL;*/

    void *bp;

    /* Iterate through explicit free list */
    for (bp = free_listp; GET_SIZE(HDRP(bp)) > 0 && !GET_ALLOC(HDRP(bp)); bp = NEXT_BLKP(bp)) //<-- this looks wrong
    {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
        {
            return bp;
        }
    }
    return NULL;

}

/**********************************************************
 * place
 * Mark the block as allocated
 **********************************************************/
void place(void* bp, size_t asize)
{
  /* Get the current block size */
  size_t bsize = GET_SIZE(HDRP(bp));

  /* splice out this block from the free list
  if (bp == free_listp) {
      free_listp = NEXT_FREE_BLKP(bp);
      PREV_FREE_BLKP(free_listp) = NULL;
      NEXT_FREE_BLKP(bp) = NULL;
  } else {
      NEXT_FREE_BLKP(PREV_FREE_BLKP(bp)) = NEXT_FREE_BLKP(bp);
      PREV_FREE_BLKP(NEXT_FREE_BLKP(bp)) = PREV_FREE_BLKP(bp);
  }*/

  PUT(HDRP(bp), PACK(bsize, 1));
  PUT(FTRP(bp), PACK(bsize, 1));
}

/**********************************************************
 * mm_free
 * Free the block and coalesce with neighbouring blocks
 **********************************************************/
void mm_free(void *bp)
{

    printf("free %zu\n", GET_SIZE(HDRP(bp)));
    if(bp == NULL){
      return;
    }
    if (!GET_ALLOC(HDRP(bp))) {
        return;
    }
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));
    coalesce_buddy(bp);

    //mm_check();
}


/**********************************************************
 * mm_malloc
 * Allocate a block of size bytes.
 * The type of search is determined by find_fit
 * The decision of splitting the block, or not is determined
 *   in place(..)
 * If no block satisfies the request, the heap is extended
 **********************************************************/
void *mm_malloc(size_t size)
{
    size_t asize; /* adjusted block size */
    size_t extendsize; /* amount to extend heap if no fit */
    char * bp;

    printf("malloc %zu", size);
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE) 
        asize = 2 * DSIZE;
    else {
        asize  = DSIZE * ((size + (DSIZE) + (DSIZE-1))/ DSIZE);
        asize = next_power_of_two(asize);
    }
    printf(" %zu\n", asize);

   
    /* Search the free list for a fit */
    if ((bp = find_fit_buddy(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        //TODO: since extend heap does coalesce, check if this block needs buddy splitting
        return NULL;
    splice_buddy(bp);
    place(bp, asize);

    /*//if (mem_heapsize() == 6857536) {
    if (mem_heapsize() == 8272) {
        printf("heapsize %zu\n", mem_heapsize());
        mm_check();
    }*/

    return bp;

}

/**********************************************************
 * mm_realloc
 * Implemented simply in terms of mm_malloc and mm_free
 *********************************************************/
void *mm_realloc(void *ptr, size_t size)
{
    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0){
      mm_free(ptr);
      return NULL;
    }
    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL)
      return (mm_malloc(size));

    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;

    /* Copy the old data. */
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

/**********************************************************
 * mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistant.
 *********************************************************/
int mm_check(void){
    void * bp;
    void * pp;
    void * np;

    size_t size;

    int count_exp_free, count_imp_free, count_imp_allocated;
    int success = 1;
    
    /* validate prologue */
    if (GET_SIZE(HDRP(heap_listp)) != 14*WSIZE && !GET_ALLOC(HDRP(heap_listp)) && HDRP(heap_listp) != FTRP(heap_listp)) {
        printf("prologue invalid");
        success = 0;
    }

    /* Is every block in the free list marked as free? */
    count_exp_free = 0;
    for (bp = free_listp; !GET_ALLOC(HDRP(bp)); bp = NEXT_FREE_BLKP(bp)) {
        count_exp_free += 1;
        if (GET_ALLOC(HDRP(bp)) != 0 && bp != (heap_listp + DSIZE)){
            printf("block in free list not marked as free\n");
            success = 0;
        }
        if (GET(HDRP(bp)) != GET(FTRP(bp))) {
            printf("invalid header and footer on free block\n");
            success = 0;
            break;
        }
        pp = PREV_FREE_BLKP(bp);
        np = NEXT_FREE_BLKP(bp);
        if (np < mem_heap_lo() || np > mem_heap_hi()) {
            printf("NEXT_BLKP (%p) of free block (%p) is not a valid heap pointer\n", np, bp);
            success = 0;
        }
        if (bp != free_listp && (pp < mem_heap_lo() || pp > mem_heap_hi())) {
            printf("PREV_BLKP (%p) of free block (%p) is not a valid heap pointer\n", pp, bp);
            success = 0;
        }
        if (bp != free_listp && bp != (heap_listp + DSIZE)) {
            if (NEXT_FREE_BLKP(pp) != bp) {
                printf("block (%p) in freelist has invalid PREV_BLKP (%p)\n", bp, pp);
                success = 0;
            }
            if (PREV_FREE_BLKP(np) != bp) {
                printf("block (%p) in freelist has invalid NEXT_BLKP (%p)\n", bp, np);
                success = 0;
                break;
            }
        }
    }


    /* Check every block in the implicit list */
    count_imp_free = 0;
    count_imp_allocated = 0;
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
        /* If a block is marked free, it should be pointing into the free list */
        if (GET_ALLOC(HDRP(bp)) == 0) {
            count_imp_free += 1;
            if (bp != free_listp && bp != heap_listp ) {
                if (NEXT_FREE_BLKP(PREV_FREE_BLKP(bp)) != bp && PREV_FREE_BLKP(NEXT_FREE_BLKP(bp)) != bp) {
                    printf("block in freelist has invalid PREV_BLKP and NEXT_BLKP\n");
                    success = 0;
                }
            }
        }
        if (GET_ALLOC(HDRP(bp)) == 1) {
                count_imp_allocated += 1;
        }
        /* Check that block is a power of 2 (buddy allocation */
        size = GET_SIZE(HDRP(bp));
        if (bp != heap_listp && (size & (size - 1)) != 0) {
            printf("block (%p) does not follow buddy allocation, %zu\n", bp, size);
            success = 0;
        }
    }

    /* TODO: validate epilogue */

    printf("MM_CHECK:\nexplicit free: %d  implicit free: %d  implicit allocated: %d\n", count_exp_free, count_imp_free, count_imp_allocated);
    return success;
}

int mm_check_init(void) {
    void * bp;
    int success = 1;

    /* validate prologue */
    if (GET_SIZE(HDRP(heap_listp)) != 16*DSIZE && !GET_ALLOC(HDRP(heap_listp)) && HDRP(heap_listp) != FTRP(heap_listp)) {
        printf("prologue invalid\n");
        success = 0;
    }

    for (bp = heap_listp;bp != heap_listp + NUM_SEG_LISTS*WSIZE; bp += WSIZE) {
        if (GET(bp)){
            printf("all seg lists should be empty initially %p %p\n", bp, HEAD_OF_SEG(bp));
            success = 0;
        }
    }

    /* validate epilogue */
    bp = mem_heap_lo();
    if (!GET_ALLOC(bp) && GET_SIZE(bp)) {
        printf("epilogue invalid\n"); 
        success = 0;
    }
    return success;
}

void  print_block(void * bp) {
    size_t fsize, hsize;
    int halloc, falloc;

    falloc = GET_ALLOC(HDRP(bp));
    halloc = GET_ALLOC(FTRP(bp));
    fsize = GET_SIZE(HDRP(bp));
    hsize = GET_SIZE(FTRP(bp));

    if (hsize == 0 && halloc == 1)
        printf("%p hdr[%zu|%d]\n", bp, hsize,halloc);
    else
        printf("%p hdr[%zu|%d] ftr[%zu:%d]\n", bp, hsize,halloc,fsize,falloc);
}

void  print_free_list(void) {
    void * bp;
    void * lp;
    size_t size;
    for (size = 1;size <= 32768; size = size*2) {
        printf("seg free list (size %zu)\n", size);
        bp = get_seg_list(size);
        if (!GET(bp)) {
            continue;
        }
        lp = GETP(bp);
        for (; lp != NULL; lp = NEXT_FREE_BLKP(lp)) {
            print_block(lp);
        }
    }
    
}

void  print_implicit_list(void) {
    void * bp;
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        print_block(bp);
    }
    print_block(bp);
}

void splice_free_block(void * bp) {
    if (bp != free_listp) {
      NEXT_FREE_BLKP(PREV_FREE_BLKP(bp)) = NEXT_FREE_BLKP(bp);
      PREV_FREE_BLKP(NEXT_FREE_BLKP(bp)) = PREV_FREE_BLKP(bp);
    } else {
        free_listp = NEXT_FREE_BLKP(bp);
        PREV_FREE_BLKP(free_listp) = NULL;
    }
}


size_t next_power_of_two(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

void splice_buddy(void * bp) {
    void* head;
    head = get_seg_list(GET_SIZE(HDRP(bp)));

    if (!GET(head)) { /* splice should not have been called if this seg list is empty */
        printf("this seg list is empty, wtfffff\n");
        return;
    }

    if (HEAD_OF_SEG(head) == bp) { /* bp is the head of this seg list */
        if (NEXT_FREE_BLKP(bp)) {  /* set the next free block as head of seg list */
            HEAD_OF_SEG(head) = NEXT_FREE_BLKP(bp);
            PREV_FREE_BLKP(NEXT_FREE_BLKP(bp)) = NULL;
        } else {                   /* bp is the only block in this seg list so empty it */
            PUT(head, 0);
            PREV_FREE_BLKP(bp) = NULL;
            NEXT_FREE_BLKP(bp) = NULL;
        }
    } else {                       /* bp is somewhere in seg list */
      NEXT_FREE_BLKP(PREV_FREE_BLKP(bp)) = NEXT_FREE_BLKP(bp);
      if (NEXT_FREE_BLKP(bp)) {   /* check if bp is last block in seg list */
      }
    }
}

void add_buddy(void *bp) {
    void *head;
    void *head_fp;
    head = get_seg_list(GET_SIZE(HDRP(bp)));

    if (!GET(head)) { /* this seg list is empty */
        PUTP(head, bp);
        PREV_FREE_BLKP(bp) = NULL;
        NEXT_FREE_BLKP(bp) = NULL;
        return;
    }
    
    /* add buddy to head of appropriate seg list */
    head_fp = HEAD_OF_SEG(head); //TODO check this
    NEXT_FREE_BLKP(bp) = head_fp;
    PREV_FREE_BLKP(bp) = NULL;
    PREV_FREE_BLKP(head_fp) = bp;
    HEAD_OF_SEG(head) = bp;

    return;
}




   /*
    1 2 3 4 5 6  7  8  9  10  11  12  13    14  15 16
   0|P|1|2|4|8|16|32|64|128|256|512|1024|2048|X|P|E|
      0 1 2 3 4  5  6  7   8   9   10   11  12

      this version has a 16-byte aligned prologue
    1 2 3 4  5  6  7   8   9  10   11   12    13   14  15 16 17 18 
   0|P|4|8|16|32|64|128|256|512|1024|2048|4096|8192|16384|X|P|E|
      0 1 2  3  4  5   6   7   8    9   10   11    12   13 14
      P 16 -> 16
   */
// couldnt think of a smarter / easier way to do this
void* get_seg_list(size_t size) {
    switch(size) {
        case 4:
            return heap_listp + ( 0* WSIZE);
        case 8:
            return heap_listp + ( 1* WSIZE);
        case 16:
            return heap_listp + ( 2* WSIZE);
        case 32:
            return heap_listp + ( 3* WSIZE);
        case 64:
            return heap_listp + ( 4* WSIZE);
        case 128:                                //really seg list should start here (32 is min size)
            return heap_listp + ( 5* WSIZE);
        case 256:
            return heap_listp + ( 6* WSIZE);
        case 512:
            return heap_listp + ( 7* WSIZE);
        case 1024:
            return heap_listp + ( 8* WSIZE);
        case 2048:
            return heap_listp + ( 9* WSIZE);
        case 4096:
            return heap_listp + (10* WSIZE);
        case 8192:
            return heap_listp + (11* WSIZE);
        case 16384:
            return heap_listp + (12* WSIZE);
        default:
            return heap_listp + (13* WSIZE);  // stick sizes larger than 2048 here (they'll still be powers of 2)
    }
}
