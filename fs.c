// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb;

// Read the super block.
void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  readsb(dev, &sb);
  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a cache entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The icache.lock spin-lock protects the allocation of icache
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold icache.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit(int dev)
{
  int i = 0;

  initlock(&icache.lock, "icache");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }

  readsb(dev, &sb);
  cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d\
 inodestart %d bmap start %d\n", sb.size, sb.nblocks,
          sb.ninodes, sb.nlog, sb.logstart, sb.inodestart,
          sb.bmapstart);
}

static struct inode* iget(uint dev, uint inum);

//PAGEBREAK!
// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode.
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;
  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      dip->tag_block = balloc(dev);
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk, since i-node cache is write-through.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  dip->tag_block = ip->tag_block;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    ip->tag_block = dip->tag_block;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip)
{
  acquiresleep(&ip->lock);
  if(ip->valid && ip->nlink == 0){
    acquire(&icache.lock);
    int r = ip->ref;
    release(&icache.lock);
    if(r == 1){
      // inode has no links and no other references: truncate and free.
      itrunc(ip);
      ip->type = 0;
      iupdate(ip);
      ip->valid = 0;
    }
  }
  releasesleep(&ip->lock);

  acquire(&icache.lock);
  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

//PAGEBREAK!
// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  bn -= NINDIRECT;

  if(bn < NDINDIRECT){
    if((addr = ip->addrs[NDIRECT+1]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn/NINDIRECT]) == 0){
      a[bn/NINDIRECT] = addr = balloc(ip->dev);
    }
    brelse(bp);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn%NINDIRECT]) == 0){
      a[bn%NINDIRECT] = addr = balloc(ip->dev);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
static void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  if(ip->addrs[NDIRECT+1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;
    brelse(bp);
    uint* second_level;
    for(j=0; j<NINDIRECT; ++j){
      if(a[j]){
        bp = bread(ip->dev, a[j]);
        second_level = (uint*)(bp->data);
        for(i=0; i<NINDIRECT; ++i){
          if(second_level[i]){
            bfree(ip->dev, second_level[i]);
          }
        }
        brelse(bp);
        bfree(ip->dev, a[j]);
      }
    }
    bfree(ip->dev, ip->addrs[NDIRECT+1]);
  }
  bfree(ip->dev, ip->tag_block);
  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

//PAGEBREAK!
// Read data from inode.
// Caller must hold ip->lock.
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(dst, bp->data + off%BSIZE, m);
    brelse(bp);
  }
  return n;
}

// PAGEBREAK!
// Write data to inode.
// Caller must hold ip->lock.
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp->data + off%BSIZE, src, m);
    log_write(bp);
    brelse(bp);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;
}

//PAGEBREAK!
// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

int
create_symlink(const char* old_path, const char* new_path){
  struct inode* dp;
  char name[DIRSIZ];
  if((dp = nameiparent((char*)new_path, name)) == 0){
    return -1;
  }
  ilock(dp);
  struct inode* ip;
  uint off;
  if((ip = dirlookup(dp, name, &off)) != 0){
    iunlockput(dp);
    return -1;
  }
  begin_op();
  if((ip = ialloc(dp->dev, T_SLINK)) == 0)
    panic("create: ialloc");
  ilock(ip);
  ip->major = 0;
  ip->minor = 0;
  ip->nlink = 1;
  iupdate(ip);
  writei(ip, (char*)old_path, 0, strlen(old_path)+1);
  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");
  iunlockput(dp);
  iunlock(ip);
  end_op();
  return 0;
}

//PAGEBREAK!
// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

int
getlinktarget(struct inode* ip, char* buf, size_t bufsize){
  if(ip->type != T_SLINK){
    iunlock(ip);
    return -1;
  }
  readi(ip, buf, 0, bufsize);
  return 0;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().

static struct inode*
namex(char *path, int nameiparent, char *name, int ref_count)
{
  struct inode *ip, *next;
  //uint dereferences = ref_count;
  //char buffer[100];
  //char namei[DIRSIZ];
  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(!(ip=dereferencelink(ip))){
      return 0;
    }
    /*
    if(ip->type == T_SLINK){
      cprintf("symbolic link\n");
      if (dereferences--) {
        if (getlinktarget(ip, buffer, ip->size) != 0) { //ip = target
          cprintf("readlink error\n");
          return 0;
        }
        iunlockput(ip);
        if (!(ip = namex(buffer, 0, namei, dereferences))){
          cprintf("bad symlink\n");
          return 0;
        }
        ilock(ip);
      }
      else {
        cprintf("too many dereferences\n");
        return 0;
      }
    }*/
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }

  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name, MAX_DEREFERENCE);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name, MAX_DEREFERENCE);
}

int
readlink(const char* pathname, char* buf, size_t bufsize){
  char name[DIRSIZ];
  int ans;
  struct inode* ip = namex((char*)(pathname), 0, name, MAX_DEREFERENCE);
  if(!ip){
    return -1;
  }
  ilock(ip);
  ans = getlinktarget(ip, buf, bufsize);
  iunlock(ip);
  return ans;
}

int
issymlink(struct inode* ip){
  int ans;
  ilock(ip);
  ans = ip->type == T_SLINK;
  iunlock(ip);
  return ans;
}

struct inode*
dereferencelink(struct inode* ip){
  struct inode* ans = ip;
  int dereference = MAX_DEREFERENCE;
  char buffer[100];
  while(ans->type == T_SLINK){
    if(!dereference--){
      iunlockput(ans);
      return 0;
    }
    getlinktarget(ans, buffer, ans->size);
    iunlockput(ans);
    ans = namei(buffer);
    if(!ans){
      return 0;
    }
    ilock(ans);
  }
  return ans;
}

int
ftag(int fd,const char* key, const char* value){
  struct inode* ip = get_inode_from_fd(fd);
  if(strlen(key)>TAGKEY_MAX_LEN){
    return -1;
  }
  if(strlen(value)>TAGVAL_MAX_LEN){
    return -1;
  }
  return set_tag(ip, key, value);
}

int
funtag(int fd,const char* key){
  struct inode* ip = get_inode_from_fd(fd);
  ilock(ip);
  struct buf* b = bread(ip->dev, ip->tag_block);
  remove_tag(b, key);
  defragment_tags(b);
  bwrite(b);
  brelse(b);
  iunlock(ip);
  return 0;
}

int
gettag(int fd,const char* key,char* buf){
  struct inode* ip = get_inode_from_fd(fd);
  int ans=0;
  ilock(ip);
  struct buf* b = bread(ip->dev, ip->tag_block);
  int offset = look_for(b,key,strlen(key)+1,0,1);
  if(offset==-1){ans=-1;goto ret;}
  offset = offset + strlen(key) + 1;
  int value_len = strlen((char*)(b->data+offset));
  buf[0]=0;
  memmove(buf,b->data+offset,value_len+1);
ret:
  brelse(b);
  iunlock(ip);
  return ans;
}

int
printtags(int fd){
  struct inode* ip = get_inode_from_fd(fd);
  ilock(ip);
  struct buf* b = bread(ip->dev, ip->tag_block);
  cprintf("addr:%p\n",ip->tag_block);
  for(int i=0;i<BSIZE;++i){
    cprintf("%d ",b->data[i]);
  }
  cprintf("\n");
  brelse(b);
  iunlock(ip);
  return 0;
}


int
defragment_tags(struct buf* bp){
  unsigned char* data = bp->data;
  int i=0;
  int in_key=1;
  while(i<BSIZE-2){
    if(!data[i] && !data[i+1] && !data[i+2]){
      for(int j=i+1;j<BSIZE;++j){
        if(data[j]){
          for(int k=j;k<BSIZE;++k){
            data[k-j+i+(i==0?0:1)+(i!=0&&in_key?1:0)] = data[k];
          }
          return 1;
        }
      }
    }
    if(!data[i]){in_key=1-in_key;}
    i=i+1;
  }
  return 0;
}

int
look_for(struct buf* bp,const char* str,int pattern_len,int look_from,int look_for_key){
  int in_key=1;
  for(int i=look_from;i<BSIZE-pattern_len;++i){
    char equal=1;
    for(int j=0;j<pattern_len && equal;++j){
      equal = bp->data[i+j] == str[j];
    }
    if(equal && (look_for_key == in_key)){return i;}
    if(!bp->data[i]){in_key=1-in_key;}
  }
  return -1;
}

int
set_tag(struct inode* in, const char* key, const char* value){
  struct buf* b;
  int offset;
  char* end_delimeter = "\0\0";
  char buf_copy[BSIZE];
  ilock(in);
  b = bread(in->dev, in->tag_block); //b is locked
  memmove(buf_copy,b->data,BSIZE);
  remove_tag(b, key);
  defragment_tags(b);
  offset = look_for(b, end_delimeter, 2, 0, 0);
  offset = offset==1?0:offset+1;
  if((offset = insert_to_data(key, b, offset))<0){goto error;}
  if((offset = insert_to_data(value, b, offset))<0){goto error;}
  if((offset = insert_to_data(end_delimeter, b, offset))<0){goto error;}
  bwrite(b);
  brelse(b);
  iunlock(in);
  return 0;
error:
  memmove(b->data,buf_copy,BSIZE);
  brelse(b);
  iunlock(in);
  return -1;
}

int
insert_to_data(const char* str, struct buf* b, uint off) {  // invariant -> b is locked
  uint i, len = strlen(str);
  if (off + len > BSIZE) {
    return -1;
  }
  for (i = 0; i < len; i++) {
    *(b->data + off + i) = str[i];
  }
  *(b->data + off + i) = '\0';

  return off + len + 1;
}

int
remove_tag(struct buf* b, const char* tag) {  // invariant -> b is locked
  uint offset_start, offset_end, pair_len, i;

  offset_start = look_for(b, tag, strlen(tag)+1, 0, 1);
  if(offset_start==-1){return -1;}
  offset_end = look_for(b, "\0", 1, offset_start+strlen(tag)+1, 1);
  pair_len = offset_end - offset_start;

  for (i = 0; i < pair_len; i++) {
    *(b->data + offset_start + i) = '\0';
  }

  return 0;
}

struct inode*
get_inode_from_fd(int fd){
  struct proc* p = myproc();
  return p->ofile[fd]->ip;
}

int
setoffset(struct file* f, uint off) {
  if (off > f->ip->size) return -1;
  f->off = off;
  return f->ip->size - off;
}
