//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op(ff.ip->dev);
    iput(ff.ip);
    end_op(ff.ip->dev);
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

int fileread_inode(struct file* f, uint offset, int user_dst, uint64 dst, int n) {
  int r;
  ilock(f->ip);
  if((r = readi(f->ip, user_dst, dst, offset, n)) > 0)
    f->off += r;
  iunlock(f->ip);
  return r;
}
// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(f, 1, addr, n);
  } else if(f->type == FD_INODE){
    r = fileread_inode(f, f->off, 0, addr, n);
  } else {
    panic("fileread");
  }

  return r;
}

int filewrite_inode(struct file* f, uint offset, int user_dst, uint64 dst, int n) {
  // write a few blocks at a time to avoid exceeding
  // the maximum log transaction size, including
  // i-node, indirect block, allocation blocks,
  // and 2 blocks of slop for non-aligned writes.
  // this really belongs lower down, since writei()
  // might be writing a device like the console.
  int r;
  int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
  int i = 0;
  while(i < n){
    int n1 = n - i;
    if(n1 > max)
      n1 = max;

    begin_op(f->ip->dev);
    ilock(f->ip);
    if ((r = writei(f->ip, user_dst, dst + i, f->off, n1)) > 0)
      f->off += r;
    iunlock(f->ip);
    end_op(f->ip->dev);

    if(r < 0)
      break;
    if(r != n1)
      panic("short filewrite");
    i += r;
  }
  return i == n ? n : -1;
}
// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  if(f->writable == 0)
    return -1;

  switch (f->type) {
    case FD_PIPE:
      return pipewrite(f->pipe, addr, n);
    case FD_DEVICE:
      if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
        return -1;
      return devsw[f->major].write(f, 1, addr, n);
    case FD_INODE:
      return filewrite_inode(f, f->off, 1, addr, n);
    default:
      panic("filewrite");
  }
}

//Only for FD_INODE
//f->off is not affected
int readfile_offset(struct file *f, uint offset, int user_dst, uint64 dst, int n) {
  if(f->readable == 0)
    return -1;
  
  if (f->type != FD_INODE)
    panic("readfile_offset");

  return fileread_inode(f, offset, user_dst, dst, n);
}

//Return the number of bytes writen, or -1 on error
int writefile_offset(struct file *f, uint offset, int user_src, uint64 src, int n) {
  if(f->writable == 0)
    return -1;

  if(f->type != FD_INODE)
    panic("writefile_offset");

  return filewrite_inode(f, offset, user_src, src, n);
}

//only write [va, end_of_page(va)]
//Return the number of bytes written, or -1 on error
int write_dirty_page(struct mmap_info* vma, struct proc* p, uint64 va) {
  pte_t* pte = walk(p->pagetable, va, 0);
  if (pte && (*pte & PTE_V) && (*pte & PTE_R) && (*pte & PTE_D)) {
    return writefile_offset(p->ofile[vma->fd], vma->offset + (va - vma->addr), 1, va, PGSIZE - (va - PGROUNDDOWN(va)));
  }
  return 0;
}
//Return 0 on success, -1 on error
//va is not page aligned, but (va + n) is page aligned
int write_dirty(struct mmap_info* vma, struct proc* p, uint64 va, uint64 n) {
  if (0 == n) return 0;
  struct file* f = p->ofile[vma->fd];
  if (0 == f->writable) {
    return -1;
  }
  if (f->type != FD_INODE)
    panic("write_dirty");
  
  n += va;
  if (n != PGROUNDDOWN(n)) {
    panic("write_dirty: va + n not page aligned");
  }
  if (va != PGROUNDDOWN(va)) {
    if (write_dirty_page(vma, p, va) < 0)
      return -1;
    va = PGROUNDUP(va);
  }
  for (; va < n; n += PGSIZE) {
    if (write_dirty_page(vma, p, va) < 0)
      return -1;
  }
  return 0;
}
