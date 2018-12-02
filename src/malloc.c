#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

#define ALIGN4(s) (((((s)-1) >> 2) << 2) + 4)
#define BLOCK_DATA(b) ((b) + 1)
#define BLOCK_HEADER(ptr) ((struct block *)(ptr)-1)

static int atexit_registered = 0;
static int num_mallocs = 0;
static int num_frees = 0;
static int num_reuses = 0;
static int num_grows = 0;
static int num_splits = 0;
static int num_coalesces = 0;
static int num_blocks = 0;
static int num_requested = 0;
static int max_heap = 0;

/*
 *  \brief printStatistics
 *
 *  \param none
 *
 *  Prints the heap statistics upon process exit.  Registered
 *  via atexit()
 *
 *  \return none
 */
void printStatistics(void)
{
   printf("\nheap management statistics\n");
   printf("mallocs:\t%d\n", num_mallocs);
   printf("frees:\t\t%d\n", num_frees);
   printf("reuses:\t\t%d\n", num_reuses);
   printf("grows:\t\t%d\n", num_grows);
   printf("splits:\t\t%d\n", num_splits);
   printf("coalesces:\t%d\n", num_coalesces);
   printf("blocks:\t\t%d\n", num_blocks);
   printf("requested:\t%d\n", num_requested);
   printf("max heap:\t%d\n", max_heap);
}

struct block
{
   size_t size;        /* Size of the allocated block of memory in bytes */
   struct block *next; /* Pointer to the next block of allcated memory   */
   bool free;          /* Is this block free?                     */
   bool dirty;         /* Has this block been used before?        */
};

struct block *FreeList = NULL; /* Free list to track the blocks available */
struct block *Final = NULL;

/*
 * \brief findFreeBlock
 *
 * \param last pointer to the linked list of free blocks
 * \param size size of the block needed in bytes 
 *
 * \return a block that fits the request or NULL if no free block matches
 *
 * \TODO Implement Next Fit
 * \TODO Implement Best Fit
 * \TODO Implement Worst Fit
 */
struct block *findFreeBlock(struct block **last, size_t size)
{
   struct block *curr = FreeList;

   num_blocks = 0;

#if defined FIT && FIT == 0
   /* First fit */
   while (curr && !(curr->free && curr->size >= size))
   {
      *last = curr;
      curr = curr->next;
      num_blocks++;
   }
#endif

#if defined BEST && BEST == 0
   struct block *best = NULL;

   while (curr)
   {
      num_blocks++;
      if (curr->free && (curr->size >= size) && (best == NULL || curr->size < best->size))
      {
         best = curr;
         if (best->size == size)
         {
            break;
         }
      }
      curr = curr->next;
   }

   curr = best;

#endif

#if defined WORST && WORST == 0
   struct block *worst = NULL;
   size_t max = 0;

   while (curr)
   {
      num_blocks++;
      if (curr->free && curr->size > size && abs(curr->size - size) > max)
      {
         max = curr->size;
         worst = curr;
      }
      *last = curr;
      curr = curr->next;
   }

   curr = worst;

#endif

#if defined NEXT && NEXT == 0

   if (Final == NULL)
   {
      Final = curr;
   }
   curr = Final;
   while (curr && !(curr->free && curr->size >= size))
   {
      num_blocks++;
      *last = curr;
      curr = curr->next;
   }
   Final = curr;

#endif

   return curr;
}

/*
 * \brief growheap
 *
 * Given a requested size of memory, use sbrk() to dynamically 
 * increase the data segment of the calling process.  Updates
 * the free list with the newly allocated memory.
 *
 * \param last tail of the free block list
 * \param size size in bytes to request from the OS
 *
 * \return returns the newly allocated block of NULL if failed
 */
struct block *growHeap(struct block *last, size_t size)
{
   /* Request more space from OS */
   struct block *curr = (struct block *)sbrk(0);
   struct block *prev = (struct block *)sbrk(sizeof(struct block) + size);

   assert(curr == prev);

   /* OS allocation failed */
   if (curr == (struct block *)-1)
   {
      return NULL;
   }

   /* Update FreeList if not set */
   if (FreeList == NULL)
   {
      FreeList = curr;
   }

   /* Attach new block to prev block */
   if (last)
   {
      last->next = curr;
   }

   /* Update block metadata */
   curr->size = size;
   curr->next = NULL;
   curr->free = false;
   num_grows++;
   if (size > max_heap)
   {
      max_heap = size;
   }
   return curr;
}

/*
 * \brief malloc
 *
 * finds a free block of heap memory for the calling process.
 * if there is no free block that satisfies the request then grows the 
 * heap and returns a new block
 *
 * \param size size of the requested memory in bytes
 *
 * \return returns the requested memory allocation to the calling process 
 * or NULL if failed
 */
void *malloc(size_t size)
{

   if (atexit_registered == 0)
   {
      atexit_registered = 1;
      atexit(printStatistics);
   }

   /* Align to multiple of 4 */
   size = ALIGN4(size);

   /* Handle 0 size */
   if (size == 0)
   {
      return NULL;
   }

   /* Look for free block */
   struct block *last = FreeList;
   struct block *next = findFreeBlock(&last, size);

   /* TODO: Split free block if possible */
   if (next && size < next->size)
   {
      struct block temp;
      temp.size = next->size - size;
      temp.free = true;
      if (next->next)
      {
         temp.next = next->next;
      }
      next->next = &temp;
      next->size = size;
      num_splits++;
   }

   /* Could not find free block, so grow heap */
   if (next == NULL)
   {
      next = growHeap(last, size);
   }

   /* Could not find free block or grow heap, so just return NULL */
   if (next == NULL)
   {
      return NULL;
   }

   if (next->dirty)
   {
      num_reuses++;
   }

   /* Mark block as in use */
   next->free = false;
   next->dirty = true;
   num_mallocs++;
   num_requested += size;

   /* Return data address associated with block */
   return BLOCK_DATA(next);
}

/*
 * \brief free
 *
 * frees the memory block pointed to by pointer. if the block is adjacent
 * to another block then coalesces (combines) them
 *
 * \param ptr the heap memory to free
 *
 * \return none
 */
void free(void *ptr)
{
   if (ptr == NULL)
   {
      return;
   }

   /* Make block as free */
   struct block *curr = BLOCK_HEADER(ptr);
   struct block *tempBlock = FreeList;
   assert(curr->free == 0);
   curr->free = true;
   num_frees++;

   /* TODO: Coalesce free blocks if needed */
   if (curr->next) //if curr->next does not point to nothing
   {
      struct block *rightBlock = curr->next;
      if (rightBlock->free == true)
      {
         curr->size += rightBlock->size; //Add the size of the rightBlock to the current block
         num_coalesces++;                //Increase the number of times we've coalesced now
         if (!rightBlock->next)          //if rightBlock->next points to nothing
         {
            curr->next = NULL; //Remove the next block.
         }
         else
            curr->next = rightBlock->next;
      }
   }
   while (tempBlock) //while tempBlock is not empty, we are not at the end of the list / the list is populated.
   {
      if (tempBlock->next == curr && tempBlock->free) //If the next one is the current node and the temporary left block from curr is free.
      {
         num_coalesces++; //Increase the number of times we've coalesced now
         struct block *previous = tempBlock;
         previous->size += curr->size;
         if (curr->next == NULL)
         {
            previous->next == NULL;
         }
         else
         {
            previous->next = curr->next;
         }
      }
      tempBlock = tempBlock->next;
   }
}

/* vim: set expandtab sts=3 sw=3 ts=6 ft=cpp: --------------------------------*/
