/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_pause(void);
int sys_close(int fd);

// 释放函数 释放内存，清空内核指针 立即进行调度
void release(struct task_struct * p)
{
	int i;

	if (!p)
		return;
	for (i=1 ; i<NR_TASKS ; i++)
		// 如果找到清空任务结构体
		if (task[i]==p) {
			// 清空槽 （任务描述表中的对应表项）
			task[i]=NULL;
			// 释放页 （代码段，内存段，堆栈）
			free_page((long)p);
			// 重新进行调度
			schedule();
			return;
		}
	panic("trying to release non-existent task");
}

// 向指定的任务（进程）发送信号
static inline int send_sig(long sig,struct task_struct * p,int priv)
{
	if (!p || sig<1 || sig>32)
		return -EINVAL;
	// 权限为1 有效的用户id是指定的用户id 或者是一个root用户
	if (priv || (current->euid==p->euid) || suser())
		// 向信号位图中添加信号
		p->signal |= (1<<(sig-1));
	else
		return -EPERM;
	return 0;
}

// 关闭会话
static void kill_session(void)
{
	struct task_struct **p = NR_TASKS + task;
	
	while (--p > &FIRST_TASK) {
		// 循环查找当前的会话
		// 向终止的当前进程的会话发送挂断信号
		if (*p && (*p)->session == current->session)
			(*p)->signal |= 1<<(SIGHUP-1);
	}
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 */
// kill 系统调用 向任何进程或者进程组发送信号
// 
int sys_kill(int pid,int sig)
{
	struct task_struct **p = NR_TASKS + task;
	int err, retval = 0;
	// 
	if (!pid) while (--p > &FIRST_TASK) {
		// pid == 0 给当前进程组发送sig
		if (*p && (*p)->pgrp == current->pid) 
			if ((err=send_sig(sig,*p,1)))
				retval = err;
	} else if (pid>0) while (--p > &FIRST_TASK) {
		// pid > 0 给对应的pid发送sig
		if (*p && (*p)->pid == pid) 
			if ((err=send_sig(sig,*p,0)))
				retval = err;
	} else if (pid == -1) while (--p > &FIRST_TASK) {
		// pid == -1 给任何进程 发送 kill all
		if ((err = send_sig(sig,*p,0)))
			retval = err;
	} else while (--p > &FIRST_TASK)
	  // pid < -1 给进程组号为-pid额进程组发送
		if (*p && (*p)->pgrp == -pid)
			if ((err = send_sig(sig,*p,0)))
				retval = err;
	return retval;
}

// 子进程通知父进程
static void tell_father(int pid)
{
	int i;

	if (pid)
	   // 循环遍历找到父进程 向对应进程发送SIGCHLD信号
		for (i=0;i<NR_TASKS;i++) {
			if (!task[i])
				continue;
			if (task[i]->pid != pid)
				continue;
			task[i]->signal |= (1<<(SIGCHLD-1));
			return;
		}
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
	printk("BAD BAD - no father found\n\r");
	// 释放子进程
	release(current);
}

int do_exit(long code)
{
	int i;
	// 释放代码段占用的内存
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	// 释放数据段占用的内存
	// 销毁文件 子进程不能销毁 会让1号进程作为新的父进程
	// 当前进程是一个会话头进程，会终止会话中的所有进程
	// 改变当前进程的运行状态，变成TASK_ZOMBIE(僵死状态)，并且向其父进程发送SIGCHILD信号。

	// 父进程运行子进程的时候 会运行wait和waitpid （父进程等待某个子进程终止）
	   // 当父进程搜到SIGCHILD会终止僵死的子进程
	// 首先父进程会把子进程的运行时间累加到自己的进程变量中
	// 把对应的子进程的描述结构体进行释放，置空任务数组中的空槽
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->father == current->pid) {
			task[i]->father = 1;
			if (task[i]->state == TASK_ZOMBIE)
				/* assumption task[1] is always init */
				(void) send_sig(SIGCHLD, task[1], 1);
		}
	for (i=0 ; i<NR_OPEN ; i++)
		if (current->filp[i])
			sys_close(i);
	iput(current->pwd);
	current->pwd=NULL;
	iput(current->root);
	current->root=NULL;
	iput(current->executable);
	current->executable=NULL;
	if (current->leader && current->tty >= 0)
		tty_table[current->tty].pgrp = 0;
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	if (current->leader)
		kill_session();
	current->state = TASK_ZOMBIE;
	current->exit_code = code;
	tell_father(current->father);
	schedule();
	return (-1);	/* just to suppress warnings */
}

int sys_exit(int error_code)
{
	return do_exit((error_code&0xff)<<8);
}

int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
	int flag, code;
	struct task_struct ** p;
	// 验证区域是否可用
	verify_area(stat_addr,4);
repeat:
	flag=0;
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		// 循环过滤函数
		if (!*p || *p == current)
			continue;
		if ((*p)->father != current->pid)
			continue;
		if (pid>0) {
			if ((*p)->pid != pid)
				continue;
		} else if (!pid) {
			if ((*p)->pgrp != current->pgrp)
				continue;
		} else if (pid != -1) {
			if ((*p)->pgrp != -pid)
				continue;
		}
		switch ((*p)->state) {
			case TASK_STOPPED:
			     // 暂停状态
				if (!(options & WUNTRACED))
					continue;
				put_fs_long(0x7f,stat_addr);
				return (*p)->pid;
			case TASK_ZOMBIE:
			   // 僵死状态
			   // 时间累加到当前的进程，释放掉p
				current->cutime += (*p)->utime;
				current->cstime += (*p)->stime;
				flag = (*p)->pid;
				code = (*p)->exit_code;
				release(*p);
				put_fs_long(code,stat_addr);
				return flag;
			default:
				flag=1;
				continue;
		}
	}
	if (flag) {
		if (options & WNOHANG)
			return 0;
		current->state=TASK_INTERRUPTIBLE;
		schedule();
		if (!(current->signal &= ~(1<<(SIGCHLD-1))))
			goto repeat;
		else
			return -EINTR;
	}
	return -ECHILD;
}


