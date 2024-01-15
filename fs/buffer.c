/*
 *  linux/fs/buffer.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have
 * been avoided by NEVER letting a interrupt change a buffer (except for the
 * data, of course), but instead letting the caller do it. NOTE! As interrupts
 * can wake up a caller, some cli-sti sequences are needed to check for
 * sleep-on-calls. These should be extremely quick, though (I hope).
 */

/*
 * NOTE! There is one discordant note here: checking floppies for
 * disk change. This is where it fits best, I think, as it should
 * invalidate changed floppy-disk-caches.
 */

// 

#include <stdarg.h>
 
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>

extern int end;
extern void put_super(int);
extern void invalidate_inodes(int);

struct buffer_head * start_buffer = (struct buffer_head *) &end;
// hash 结构 
struct buffer_head * hash_table[NR_HASH];
static struct buffer_head * free_list;
static struct task_struct * buffer_wait = NULL;
int NR_BUFFERS = 0;

static inline void wait_on_buffer(struct buffer_head * bh)
{
	// 等待解锁
	cli(); // 中断 挂起
	while (bh->b_lock) // 有锁定
	    // 在b_wait链表上一直等待，没有锁定解开。
		sleep_on(&bh->b_wait);
	sti(); // 关闭中断
}

int sys_sync(void)
{
	int i;
	struct buffer_head * bh;

	sync_inodes();		/* write out inodes into buffers */
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		wait_on_buffer(bh);
		if (bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}

int sync_dev(int dev)
{
	int i;
	struct buffer_head * bh;

	// buffer的开始区域
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		// 找到对应的块 并且有数据 需要写盘操作
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh); // 底层的块设备读写函数
	}
	// i节点同步
	sync_inodes();
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}

static void inline invalidate_buffers(int dev)
{
	int i;
	struct buffer_head * bh;

	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev)
			bh->b_uptodate = bh->b_dirt = 0;
	}
}

/*
 * This routine checks whether a floppy has been changed, and
 * invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 *
 * NOTE! Although currently this is only for floppies, the idea is
 * that any additional removable block-device will use this routine,
 * and that mount/open needn't know that floppies/whatever are
 * special.
 */
void check_disk_change(int dev)
{
	int i;

	if (MAJOR(dev) != 2)
		return;
	if (!floppy_change(dev & 0x03))
		return;
	for (i=0 ; i<NR_SUPER ; i++)
		if (super_block[i].s_dev == dev)
			put_super(super_block[i].s_dev);
	invalidate_inodes(dev);
	invalidate_buffers(dev);
}

// hash表中的散列函数
#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
// 指向hashtable 计算对应的散列值
#define hash(dev,block) hash_table[_hashfn(dev,block)]

static inline void remove_from_queues(struct buffer_head * bh)
{
/* remove from hash-queue */
// 先从hash表中移除
	if (bh->b_next)
		bh->b_next->b_prev = bh->b_prev;
	if (bh->b_prev)
		bh->b_prev->b_next = bh->b_next;
	if (hash(bh->b_dev,bh->b_blocknr) == bh)
		hash(bh->b_dev,bh->b_blocknr) = bh->b_next;
/* remove from free list */
// 从空闲列表中移除
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("Free block list corrupted");
	bh->b_prev_free->b_next_free = bh->b_next_free;
	bh->b_next_free->b_prev_free = bh->b_prev_free;
	if (free_list == bh)
		free_list = bh->b_next_free;
}

static inline void insert_into_queues(struct buffer_head * bh)
{
/* put at end of free list */
	bh->b_next_free = free_list;
	bh->b_prev_free = free_list->b_prev_free;
	free_list->b_prev_free->b_next_free = bh;
	free_list->b_prev_free = bh;
/* put the buffer in new hash-queue if it has a device */
	bh->b_prev = NULL;
	bh->b_next = NULL;
	if (!bh->b_dev)
		return;
	bh->b_next = hash(bh->b_dev,bh->b_blocknr);
	hash(bh->b_dev,bh->b_blocknr) = bh;
	bh->b_next->b_prev = bh;
}

static struct buffer_head * find_buffer(int dev, int block)
{		
	struct buffer_head * tmp;
	// 在hash散列表后在那个查找 不断循环查找下一个
	for (tmp = hash(dev,block) ; tmp != NULL ; tmp = tmp->b_next)
	    // dev和block相等，找到返回
		if (tmp->b_dev==dev && tmp->b_blocknr==block)
			return tmp;
	return NULL;
}

/*
 * Why like this, I hear you say... The reason is race-conditions.
 * As we don't lock buffers (unless we are readint them, that is),
 * something might happen to it while we sleep (ie a read-error
 * will force it bad). This shouldn't really happen currently, but
 * the code is ready.
 */
// 散列值的确认 设备号^逻辑块号
struct buffer_head * get_hash_table(int dev, int block)
{
	struct buffer_head * bh;

	// 在散列表中
	for (;;) {
		// 没找到返回nil
		if (!(bh=find_buffer(dev,block)))
			return NULL;
		// 找到计数+1
		bh->b_count++;
		// 没找到等待buffer 挂起
		wait_on_buffer(bh);
		// 等待完之后，重新比较（可能被其他人使用）
		if (bh->b_dev == dev && bh->b_blocknr == block)
			return bh;
		bh->b_count--;
	}
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algoritm is changed: hopefully better, and an elusive bug removed.
 */
// 判断缓冲区的修改标志和锁定标志
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)
struct buffer_head * getblk(int dev,int block)
{
	struct buffer_head * tmp, * bh; // 定义一个buffer_head类型的结构体指针

repeat:
    // 获取 在哈希表中 直接返回，在有效高速缓冲区中
	if ((bh = get_hash_table(dev,block)))
		return bh;
	// 搜素空闲缓冲队列 寻找合适块
	tmp = free_list;
	do {
		if (tmp->b_count) // 缓冲区被使用次数 不为0是被使用了
			continue;
		// 找最小的权重值的块
		if (!bh || BADNESS(tmp)<BADNESS(bh)) {
			bh = tmp;
			if (!BADNESS(tmp))
				break;
		}
/* and repeat until we find something good */
	} while ((tmp = tmp->b_next_free) != free_list);
	if (!bh) {
		// 寻找不到合适的块休眠 进入sleep队列 进行等待
		sleep_on(&buffer_wait);
		goto repeat;
	}
	// 多线程过程中 资源被锁定 等待解锁 临界内存区域方法
	wait_on_buffer(bh);
	// 确保在等待过程中找到的高速缓冲区没有被使用
	// b_count 如果被使用设置为1 没有被使用为0
	if (bh->b_count)
	   // 已经被使用重新查找一个块
		goto repeat;
	// 查找高速缓冲区是否有剩余的数据（曾经被使用）
	while (bh->b_dirt) {
		// 有剩余数据写盘操作（回写同步）
		sync_dev(bh->b_dev);
		// 同步完 进行等待
		wait_on_buffer(bh);
		// 被使用继续查找新高速缓冲区块
		if (bh->b_count)
			goto repeat;
	}

/* NOTE!! While we slept waiting for this block, somebody else might */
/* already have added "this" block to the cache. check it */
   // 块已经在hash表中，不能被使用，重新查找
	if (find_buffer(dev,block))
		goto repeat;
/* OK, FINALLY we know that this buffer is the only one of it's kind, */
/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
    // 进行头的设置并且塞入哈希表
	bh->b_count=1;
	bh->b_dirt=0;
	bh->b_uptodate=0;
	remove_from_queues(bh);
	bh->b_dev=dev;
	bh->b_blocknr=block;
	insert_into_queues(bh);
	return bh;
}

// 释放高速缓冲区的块
void brelse(struct buffer_head * buf)
{
	if (!buf)
		return;
	wait_on_buffer(buf);
	// 引用计数-1
	if (!(buf->b_count--))
		panic("Trying to free free buffer");
	// 唤醒等待空闲缓冲区的进程
	wake_up(&buffer_wait);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */
// 从设备上读取指定的数据块，并返回含有数据的高速缓冲区
struct buffer_head * bread(int dev,int block)
{
	struct buffer_head * bh;
    // 获取高速缓冲块
	if (!(bh=getblk(dev,block)))
		panic("bread: getblk returned NULL\n");
	// 块被更新过
	if (bh->b_uptodate)
		return bh;
	// 读取对应设备块，让缓冲区与块设备的数据一致
	ll_rw_block(READ,bh);
	// 等待缓冲区解锁
	wait_on_buffer(bh);
	// 判断数据是否有效
	if (bh->b_uptodate)
		return bh;
	// 无效数据 释放缓冲块
	brelse(bh);
	return NULL;
}

// 复制1024的数据 从from 拷贝一块数据到to的位置
#define COPYBLK(from,to) \
__asm__("cld\n\t" \
	"rep\n\t" \
	"movsl\n\t" \
	::"c" (BLOCK_SIZE/4),"S" (from),"D" (to) \
	)

/*
 * bread_page reads four buffers into memory at the desired address. It's
 * a function of its own, as there is some speed to be got by reading them
 * all at the same time, not waiting for one to be read, and then another
 * etc.
 */
// 一次性读取1页 4块 4 * 1024 类似于getblk 
void bread_page(unsigned long address,int dev,int b[4])
{
	struct buffer_head * bh[4];
	int i;

	for (i=0 ; i<4 ; i++)
		if (b[i]) {
			if ((bh[i] = getblk(dev,b[i])))
				if (!bh[i]->b_uptodate)
				    // 从设备块读取数据，使高速缓冲区的数据和块保持一致，最新
					ll_rw_block(READ,bh[i]);
		} else
			bh[i] = NULL;
	for (i=0 ; i<4 ; i++,address += BLOCK_SIZE)
		if (bh[i]) {
			// 等待找到的块解锁
			wait_on_buffer(bh[i]);
			// 如果被使用（有数据），读取1024到指定的地址
			if (bh[i]->b_uptodate)
				COPYBLK((unsigned long) bh[i]->b_data,address);
			// 释放高速缓冲区的块
			brelse(bh[i]);
		}
}

/*
 * Ok, breada can be used as bread, but additionally to mark other
 * blocks for reading as well. End the argument list with a negative
 * number.
 */

/*
这段代码是一个名为breada的函数，它的作用是读取指定设备上的块，并使用breada标记其他块以供后续读取。

函数的参数包括dev（设备号）和first（第一个块号），后面还可以跟随其他块号。参数列表以负数作为结束标志。
*/

/*
总体而言，breada函数用于读取指定设备上的块，并使用breada标记其他块以供后续读取。它支持同时读取多个块，并通过异步读取提高效率。
*/
struct buffer_head * breada(int dev,int first, ...)
{
	va_list args;
	struct buffer_head * bh, *tmp;

	va_start(args,first); // 解析参数  
	// 使用getblk函数获取指定设备和块号的缓冲区头结构bh。如果bh为空（即获取失败），则触发panic。
	if (!(bh=getblk(dev,first)))
		panic("bread: getblk returned NULL\n");
	// 如果bh的数据不是最新的（b_uptodate为假），则使用ll_rw_block函数进行块的读取操作。
	if (!bh->b_uptodate)
		ll_rw_block(READ,bh);
	// 使用可变参数列表args，循环读取后续的块号。
	while ((first=va_arg(args,int))>=0) {
		// 对于每个后续的块号，使用getblk函数获取缓冲区头结构tmp
		tmp=getblk(dev,first);
		if (tmp) {
			// 如果tmp的数据不是最新的（b_uptodate为假），则使用ll_rw_block函数进行块的异步读取操作（使用READA标志）。
			if (!tmp->b_uptodate)
				ll_rw_block(READA,bh);
			// 减少tmp的引用计数b_count。
			tmp->b_count--;
		}
	}
	// 结束可变参数列表的处理。
	va_end(args);
	// 使用wait_on_buffer函数等待bh的读取操作完成。
	wait_on_buffer(bh);
	// 如果bh的数据是最新的（b_uptodate为真），则返回bh。
	if (bh->b_uptodate)
		return bh;
	// 否则，释放bh的缓冲区头结构，并返回空指针。
	brelse(bh);
	return (NULL);
}

// 高速缓冲区的初始化程序（空闲缓冲区双向链表创建、哈希表的创建）
// 它的作用是初始化缓冲区管理器。函数的参数是buffer_end，表示内存的结束地址。
void buffer_init(long buffer_end)
{
	// 将指针h指向起始缓冲区头结构start_buffer。
	struct buffer_head * h = start_buffer; // 内核结束的内存 
	void * b;
	int i;

	// 根据buffer_end的值，确定起始内存地址b。
    // 如果buffer_end等于1<<20（即1MB），则将b设置为640KB的地址。
    // 否则，将b设置为buffer_end的地址。
	if (buffer_end == 1<<20)
		b = (void *) (640*1024); 
	else
		b = (void *) buffer_end;
	// 进入循环，每次迭代将b减去块大小（BLOCK_SIZE），直到b小于等于h+1的地址。
	while ( (b -= BLOCK_SIZE) >= ((void *) (h+1)) ) {
		// 创建循环链表
		/*
		 将设备号b_dev设置为0。
		将脏标志b_dirt设置为0。
		将引用计数b_count设置为0。
		将锁标志b_lock设置为0。
		将最新数据标志b_uptodate设置为0。
		将等待队列b_wait设置为NULL。
		将下一个缓冲区头结构b_next设置为NULL。
		将上一个缓冲区头结构b_prev设置为NULL。
		将数据指针b_data设置为当前的内存地址b。
		将上一个空闲缓冲区头结构b_prev_free设置为h-1。
		将下一个空闲缓冲区头结构b_next_free设置为h+1。
		将h指针向后移动一位。
		增加缓冲区计数NR_BUFFERS
		*/
		h->b_dev = 0;
		h->b_dirt = 0;
		h->b_count = 0;
		h->b_lock = 0;
		h->b_uptodate = 0;
		h->b_wait = NULL;
		h->b_next = NULL;
		h->b_prev = NULL;
		h->b_data = (char *) b;
		h->b_prev_free = h-1;
		h->b_next_free = h+1;
		h++;
		NR_BUFFERS++;
		// 如果当前的内存地址b等于0x100000，则将b设置为0xA0000。
		if (b == (void *) 0x100000)
			b = (void *) 0xA0000;
	}
	// 将h指针向前移动一位，使其指向最后一个缓冲区头结构。
	h--;
	// 创建空闲的链表

	// 将起始缓冲区头结构start_buffer赋值给空闲链表头结构free_list。
	free_list = start_buffer;
	// 将空闲链表头结构的上一个空闲缓冲区指针b_prev_free设置为h
	free_list->b_prev_free = h;
	// 将最后一个缓冲区头结构的下一个空闲缓冲区指针b_next_free设置为空闲链表头结构。
	h->b_next_free = free_list;
	// 初始化散列表：将307个散列项的指针初始化为NULL。
	for (i=0;i<NR_HASH;i++)
	    // 创建307个散列项
		hash_table[i]=NULL;
}	
