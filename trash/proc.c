// Return 0 on success, -1 on error
int add_mmap(struct proc* p, void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
  struct mmap_info* cur = &p->head;
  for (; cur->length && cur->nxt;cur = cur->nxt);
  if (cur->nxt &&cur->length) {
    cur->nxt = bd_malloc(sizeof(struct mmap_info));
    cur =cur->nxt;
  }
  if (cur) {
    return -1;
  }
  cur->addr = addr;
  cur->length = length;
  cur->prot = prot;
  cur->flags = flags;
  cur->fd = fd;
  cur->offset = offset;
  return 0;
}