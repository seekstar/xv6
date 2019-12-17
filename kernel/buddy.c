#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#include "buddy.h"

// Buddy allocator

static int nsizes;     // the number of entries in bd_sizes array

typedef struct list Bd_list;

// The allocator has sz_info for each size k. Each sz_info has a free
// list, an array alloc to keep track which blocks have been
// allocated.  The arrays are of type char (which is 1 byte), but the
// allocator uses 1 bit per block (thus, one char records the info of
// 8 blocks).
struct sz_info {
  Bd_list free;
  char *alloc;
};
typedef struct sz_info Sz_info;

static Sz_info *bd_sizes; 
static void *bd_base;   // start address of memory managed by the buddy allocator
static struct spinlock lock;

// Return 1 if bit at position index in array is set to 1
int bit_isset(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  return (b & m) == m;
}

// Set bit at position index in array to 1
void bit_set(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b | m);
}

// Clear bit at position index in array
void bit_clear(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b & ~m);
}

// Print a bit vector as a list of ranges of 1 bits
void
bd_print_vector(char *vector, int len) {
  int last, lb;
  
  last = 1;
  lb = 0;
  for (int b = 0; b < len; b++) {
    if (last == bit_isset(vector, b))
      continue;
    if(last == 1)
      printf(" [%d, %d)", lb, b);
    lb = b;
    last = bit_isset(vector, b);
  }
  if(lb == 0 || last == 1) {
    printf(" [%d, %d)", lb, len);
  }
  printf("\n");
}

// Print buddy's data structures
void
bd_print() {
  for (int k = 0; k < nsizes; k++) {
    printf("size %d (blksz %d nblk %d): free list: ", k, BLK_SIZE(k), NBLK(k));
    lst_print(&bd_sizes[k].free);
    printf("  alloc:");
    bd_print_vector(bd_sizes[k].alloc, NBLK(k));
  }
}

// What is the first k such that 2^k >= n?
int
firstk(uint64 n) {
  int k = 0;
  uint64 size = LEAF_SIZE;

  while (size < n) {
    k++;
    size *= 2;
  }
  return k;
}

// Compute the block index for address base at size k
int
blk_index(int k, char *base) {
  int n = base - (char *) bd_base;
  return n / BLK_SIZE(k);
}

// Convert a block index at size k back into an address
void *addr(int k, int bi) {
  int n = bi * BLK_SIZE(k);
  return (char *) bd_base + n;
}

uint64
bd_real_alloc(uint64 nbytes) {
  return BLK_SIZE(firstk(nbytes));
}

// allocate nbytes, but malloc won't return anything smaller than LEAF_SIZE
void *
bd_malloc(uint64 nbytes)
{
  int fk, k;

  acquire(&lock);

  // Find a free block >= nbytes, starting with smallest k possible
  fk = firstk(nbytes);
  for (k = fk; k < nsizes; k++) {
    if(!lst_empty(&bd_sizes[k].free))
      break;
  }
  if(k >= nsizes) { // No free blocks?
    release(&lock);
    return 0;
  }

  // Found a block; pop it and potentially split it.
  char *base = lst_pop(&bd_sizes[k].free);
  bit_set(bd_sizes[k].alloc, blk_index(k, base));
  for(; k > fk; k--) {
    // split a block at size k and mark one half allocated at size k-1
    // and put the buddy on the free list at size k-1
    char *q = base + BLK_SIZE(k-1);   // base's buddy
    bit_set(bd_sizes[k-1].alloc, blk_index(k-1, base));
    lst_push(&bd_sizes[k-1].free, q);
  }
  release(&lock);

  return base;
}

//bi: block index
int get_buddy(int bi) {
  return (bi % 2 == 0) ? bi+1 : bi-1;
}
// Find the size of the block that base points to.
// In other words, find the biggest k that not be splited
int
size(char *base) {
  for (int k = 0; k < nsizes; ++k) {
    int bi = blk_index(k, base);
    if (bit_isset(bd_sizes[k].alloc, bi)) {
      return k;
    }
  }
  return 0;
}

// Free memory pointed to by base, which was earlier allocated using
// bd_malloc.
void
bd_free(void *base) {
  void *q;
  int k;

  acquire(&lock);
  for (k = size(base); k < MAXSIZE; k++) {
    int bi = blk_index(k, base);
    int buddy = get_buddy(bi);
    bit_clear(bd_sizes[k].alloc, bi);  // free base at size k
    if (bit_isset(bd_sizes[k].alloc, buddy)) {  // is buddy allocated?
      break;   // break out of loop
    }
    // budy is free; merge with buddy
    q = addr(k, buddy);
    lst_remove(q);    // remove buddy from free list
    if(buddy % 2 == 0) {
      base = q;
    }
  }
  lst_push(&bd_sizes[k].free, base);
  release(&lock);
}

// Compute the first block at size k that doesn't contain base
int
blk_index_next(int k, char *base) {
  int n = (base - (char *) bd_base) / BLK_SIZE(k);
  if((base - (char*) bd_base) % BLK_SIZE(k) != 0)
      n++;
  return n ;
}

int
log2(uint64 n) {
  int k = 0;
  while (n > 1) {
    k++;
    n = n >> 1;
  }
  return k;
}

// Mark memory from [start, stop), starting at size 0, as allocated. 
void
bd_mark(void *start, void *stop)
{
  int bi, bj;

  if (((uint64) start % LEAF_SIZE != 0) || ((uint64) stop % LEAF_SIZE != 0))
    panic("bd_mark");

  for (int k = 0; k < nsizes; k++) {
    bi = blk_index(k, start);
    bj = blk_index_next(k, stop);
    for(; bi < bj; bi++) {
      bit_set(bd_sizes[k].alloc, bi);
    }
  }
}

// If a block is marked as allocated and the buddy is free, put the
// buddy on the free list at size k.
int
bd_initfree_pair(int k, int bi) {
  int buddy = (bi % 2 == 0) ? bi+1 : bi-1;
  int free = 0;
  if(bit_isset(bd_sizes[k].alloc, bi) !=  bit_isset(bd_sizes[k].alloc, buddy)) {
    // one of the pair is free
    free = BLK_SIZE(k);
    if(bit_isset(bd_sizes[k].alloc, bi))
      lst_push(&bd_sizes[k].free, addr(k, buddy));   // put buddy on free list
    else
      lst_push(&bd_sizes[k].free, addr(k, bi));      // put bi on free list
  }
  return free;
}

// Initialize the free lists for each size k.  For each size k, there
// are only two pairs that may have a buddy that should be on free list:
// bd_left and bd_right.
int
bd_initfree(void *bd_left, void *bd_right) {
  int free = 0;

  for (int k = 0; k < MAXSIZE; k++) {   // skip max size
    int left = blk_index_next(k, bd_left);
    int right = blk_index(k, bd_right);
    free += bd_initfree_pair(k, left);
    if(right <= left)
      continue;
    free += bd_initfree_pair(k, right);
  }
  return free;
}

// Mark the range [bd_base,base) as allocated
int
bd_mark_data_structures(char *base) {
  int meta = base - (char*)bd_base;
  printf("bd: %d meta bytes for managing %d bytes of memory\n", meta, BLK_SIZE(MAXSIZE));
  bd_mark(bd_base, base);
  return meta;
}

// Mark the range [end, HEAPSIZE) as allocated
int
bd_mark_unavailable(void *end, void *left) {
  int unavailable = BLK_SIZE(MAXSIZE)-(end-bd_base);
  if(unavailable > 0)
    unavailable = ROUNDUP(unavailable, LEAF_SIZE);
  printf("bd: 0x%x bytes unavailable\n", unavailable);

  void *bd_end = bd_base+BLK_SIZE(MAXSIZE)-unavailable;
  bd_mark(bd_end, bd_base+BLK_SIZE(MAXSIZE));
  return unavailable;
}

// Initialize the buddy allocator: it manages memory from [base, end).
void
bd_init(void *base, void *end) {
  base = (char *) ROUNDUP((uint64)base, LEAF_SIZE);
  int sz;

  initlock(&lock, "buddy");
  bd_base = (void *) base;

  // compute the number of sizes we need to manage [base, end)
  nsizes = log2(((char*)end-(char*)base)/LEAF_SIZE) + 1;
  if((char*)end-(char*)base > BLK_SIZE(MAXSIZE)) {
    nsizes++;  // round up to the next power of 2
  }

  printf("bd: memory sz is %d bytes; allocate an size array of length %d\n",
         (char*) end - (char*)base, nsizes);

  // allocate bd_sizes array
  bd_sizes = (Sz_info *) base;
  base += sizeof(Sz_info) * nsizes;
  memset(bd_sizes, 0, sizeof(Sz_info) * nsizes);

  // initialize free list and allocate the alloc array for each size k
  for (int k = 0; k < nsizes; ++k) {
    lst_init(&bd_sizes[k].free);
    sz = sizeof(char) * ROUNDUP(NBLK(k), 8)/8;
    bd_sizes[k].alloc = base;
    memset(bd_sizes[k].alloc, 0, sz);
    base += sz;
  }

  base = (char *) ROUNDUP((uint64) base, LEAF_SIZE);

  // done allocating; mark the memory range [bd_base, base) as allocated, so
  // that buddy will not hand out that memory.
  int meta = bd_mark_data_structures(base);
  
  // mark the unavailable memory range [end, HEAP_SIZE) as allocated,
  // so that buddy will not hand out that memory.
  int unavailable = bd_mark_unavailable(end, base);
  void *bd_end = bd_base+BLK_SIZE(MAXSIZE)-unavailable;
  
  // initialize free lists for each size k
  int free = bd_initfree(base, bd_end);

  // check if the amount that is free is what we expect
  if(free != BLK_SIZE(MAXSIZE)-meta-unavailable) {
    printf("free %d %d\n", free, BLK_SIZE(MAXSIZE)-meta-unavailable);
    panic("bd_init: free mem");
  }
}
