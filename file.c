//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "mmu.h"


struct cache_block{
  uint ff;
  struct cache_block* next;
  struct cache_block* prev;
};

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct cache_block* cache_pointer;
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
  ftable.cache_pointer = 0;
}

struct cache_block* create_filecache(void){
  struct cache_block* cb = (struct cache_block*)kalloc();
  if (cb == 0) return 0;
  memset(cb, 0, PGSIZE);
  cb->next = cb->prev = 0;
  // Correct calculation of free files per block
  cb->ff = (PGSIZE - sizeof(struct cache_block)) / sizeof(struct file);
  return cb;
}

struct file* get_file_from_file_cache(void){
  struct cache_block *cb, *prev;
  struct file* fp;
  char *st, *end;

  acquire(&ftable.lock);
  for(cb = ftable.cache_pointer; cb != 0; prev = cb, cb = cb->next){
      if (cb->ff == 0) continue;

      st = (char*)cb + sizeof(struct cache_block);
      end = st + (PGSIZE - sizeof(struct cache_block)) / sizeof(struct file) * sizeof(struct file);
      for(fp = (struct file*)st; (char*)fp < end; fp++){
          if(fp->ref == 0){
              fp->ref = 1;
              cb->ff--;
              
              release(&ftable.lock);
              return fp;
          }
      }
  }

  struct cache_block* new_cb = create_filecache();
  if(new_cb == 0){
      release(&ftable.lock);
      return 0;
  }

  if(ftable.cache_pointer == 0){
      ftable.cache_pointer = new_cb;
  } 
  else{
      prev->next = new_cb;
      new_cb->prev = prev;
  }

  fp = (struct file*)((char*)new_cb + sizeof(struct cache_block));
  fp->ref = 1;
  new_cb->ff--;
  release(&ftable.lock);
  return fp;
}

struct file* 
filealloc(void) 
{
  return get_file_from_file_cache();
}

struct file* 
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if (f->ref < 1) panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

void return_file_to_file_cache(struct file *fp){
  struct cache_block *cb;
  char *st, *end;

  // Caller must hold ftable.lock
  for(cb = ftable.cache_pointer; cb != 0; cb = cb->next){
      st = (char*)cb + sizeof(struct cache_block);
      end = st + (PGSIZE - sizeof(struct cache_block)) / sizeof(struct file) * sizeof(struct file);

      if((char*)fp >= st && (char*)fp < end){
          cb->ff++;
          // Check if all files in this block are free
          if(cb->ff == (PGSIZE - sizeof(struct cache_block)) / sizeof(struct file)){
              if (cb->prev) cb->prev->next = cb->next;
              if (cb->next) cb->next->prev = cb->prev;
              if (ftable.cache_pointer == cb) ftable.cache_pointer = cb->next;
              kfree((char*)cb);
          }
          break;
      }
  }
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void 
fileclose(struct file *f) 
{
  struct file ff;

  acquire(&ftable.lock);
  if (f->ref < 1) panic("fileclose");
  if (--f->ref > 0) {
      release(&ftable.lock);
      return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  return_file_to_file_cache(f);
  release(&ftable.lock);

  if(ff.type == FD_PIPE)
      pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
      begin_op();
      iput(ff.ip);
      end_op();
  }
}

// Get metadata about file f.
int
filestat(struct file *f, struct stat *st)
{
  if(f->type == FD_INODE){
    ilock(f->ip);
    stati(f->ip, st);
    iunlock(f->ip);
    return 0;
  }
  return -1;
}

// Read from file f.
int
fileread(struct file *f, char *addr, int n)
{
  int r;

  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
    return r;
  }
  panic("fileread");
}

//PAGEBREAK!
// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("filewrite");
}

