#define LEAF_SIZE     16                         // The smallest block size
#define MAXSIZE       (nsizes-1)                 // Largest index in bd_sizes array
#define BLK_SIZE(k)   ((1L << (k)) * LEAF_SIZE)  // Size of block at size k
#define HEAP_SIZE     BLK_SIZE(MAXSIZE) 
#define NBLK(k)       (1 << (MAXSIZE-k))         // Number of block at size k
#define ROUNDUP(n,sz) (((((n)-1)/(sz))+1)*(sz))  // Round up to the next multiple of sz
