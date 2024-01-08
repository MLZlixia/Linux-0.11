/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>

void do_exit(int error_code);

int sys_sgetmask()
{
	// 当前任务的信号屏蔽码
	return current->blocked;
}

int sys_ssetmask(int newmask)
{
	// 设置新的信号位图 阻塞码
	int old=current->blocked;

	current->blocked = newmask & ~(1<<(SIGKILL-1));
	return old;
}

static inline void save_old(char * from,char * to)
{
	// 复制信号从from到to
	int i;
	// 验证大小
	verify_area(to, sizeof(struct sigaction));
	// 进行拷贝
	for (i=0 ; i< sizeof(struct sigaction) ; i++) {
		put_fs_byte(*from,to);
		from++;
		to++;
	}
}

static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}


// 作何信合的预处理和设置
int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;
	// 确定信号范围
	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
	// 指定信号处理句柄
	tmp.sa_handler = (void (*)(int)) handler;
	// 设置掩码 屏蔽码
	tmp.sa_mask = 0;
	// 设置状态执行一次恢复到默认值
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
	// 保存恢复处理程序指针
	tmp.sa_restorer = (void (*)(void)) restorer;
	// 更新当前标志指针的信号信息
	handler = (long) current->sigaction[signum-1].sa_handler;
	current->sigaction[signum-1] = tmp;
	return handler;
}

int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
	tmp = current->sigaction[signum-1];
	get_new((char *) action,
		(char *) (signum-1+current->sigaction));
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
		current->sigaction[signum-1].sa_mask = 0;
	else
		current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
	return 0;
}

/*
函数作用解释：
 该函数用于处理信号。它将信号处理函数的指针插入到用户程序的堆栈中，并根据信号的处理标志和掩码进行相应的处理。
然后，调整堆栈指针和参数，将信号处理函数的参数压入堆栈，并跳转到信号处理函数执行。最后，将当前进程的 blocked 字段与 sa_mask 进行按位或操作，屏蔽相应的信号。

函数参数解释：
signr：信号编号
eax, ebx, ecx, edx：寄存器的值
fs, es, ds：段寄存器的值
eip：指令指针
cs：代码段寄存器的值
eflags：标志寄存器的值
esp：堆栈指针
ss：堆栈段寄存器的值
*/

// 是系统调用中断处理程序中的信号处理函数
void do_signal(long signr, long eax, long ebx, long ecx, long edx,
               long fs, long es, long ds,
               long eip, long cs, long eflags,
               unsigned long *esp, long ss)
{
    // 对信号的处理句柄插入到用户的程序的堆栈中

    unsigned long sa_handler; // 信号处理函数指针
    long old_eip = eip; // 保存旧的指令指针
    struct sigaction *sa = current->sigaction + signr - 1; // 获取当前进程对应信号的 sigaction 结构体
    int longs; // 用于计算需要压入堆栈的参数个数
    unsigned long *tmp_esp; // 临时堆栈指针

    sa_handler = (unsigned long)sa->sa_handler; // 获取信号处理函数指针
    if (sa_handler == 1)
        return; // 如果信号处理函数指针为可忽略，则表示使用默认处理方式，直接返回
    if (!sa_handler) { // 如果信号为默认处理方式
        if (signr == SIGCHLD)
            return; // 如果信号为 SIGCHLD（通知子进程结束信号），则直接返回
        else
            do_exit(1 << (signr - 1)); // 否则调用 do_exit 函数，终止进程
    }
    if (sa->sa_flags & SA_ONESHOT)
        sa->sa_handler = NULL; // 如果设置了 SA_ONESHOT 标志，则将信号处理函数指针置为空

    *(&eip) = sa_handler; // 将信号处理函数指针赋值给指令指针，实现跳转到信号处理函数

    // 如果信号句柄只是用一次 则将信息置位空
	longs = (sa->sa_flags & SA_NOMASK) ? 7 : 8; // 计算需要压入堆栈的参数个数，如果设置了 SA_NOMASK 标志，则参数个数为 7，否则为 8

    *(&esp) -= longs; // 调整堆栈指针，为参数预留空间
    verify_area(esp, longs * 4); // 验证堆栈空间是否足够

    tmp_esp = esp; // 保存临时堆栈指针
    put_fs_long((long)sa->sa_restorer, tmp_esp++); // 将 sa_restorer 函数指针压入堆栈
    put_fs_long(signr, tmp_esp++); // 将信号编号压入堆栈

    if (!(sa->sa_flags & SA_NOMASK))
        put_fs_long(current->blocked, tmp_esp++); // 如果未设置 SA_NOMASK 标志，则将当前进程的 blocked 字段压入堆栈

    put_fs_long(eax, tmp_esp++); // 将寄存器的值压入堆栈
    put_fs_long(ecx, tmp_esp++);
    put_fs_long(edx, tmp_esp++);
    put_fs_long(eflags, tmp_esp++);
    put_fs_long(old_eip, tmp_esp++);

    current->blocked |= sa->sa_mask; // 将当前进程的 blocked 字段与 sa_mask 进行按位或操作，屏蔽相应的信号
}

