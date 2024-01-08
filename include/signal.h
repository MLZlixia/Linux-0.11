#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

/*
1 系统中什么是信息，都有什么信号
2 在系统接收到信号后，他是如何处理的
3 信号的作用
*/

typedef int sig_atomic_t;
typedef unsigned int sigset_t;		/* 32 bits */

#define _NSIG             32
#define NSIG		_NSIG

#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGIOT		 6
#define SIGUNUSED	 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGUSR1		10
#define SIGSEGV		11
#define SIGUSR2		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGSTKFLT	16
#define SIGCHLD		17
#define SIGCONT		18
#define SIGSTOP		19
#define SIGTSTP		20
#define SIGTTIN		21
#define SIGTTOU		22

/* Ok, I haven't implemented sigactions, but trying to keep headers POSIX */
#define SA_NOCLDSTOP	1
#define SA_NOMASK	0x40000000
#define SA_ONESHOT	0x80000000

#define SIG_BLOCK          0	/* for blocking signals */
#define SIG_UNBLOCK        1	/* for unblocking signals */
#define SIG_SETMASK        2	/* for setting the signal mask */

#define SIG_DFL		((void (*)(int))0)	/* default signal handling */
#define SIG_IGN		((void (*)(int))1)	/* ignore signal */

/*

struct sigaction {
    void (*sa_handler)(int);    // 信号处理函数指针，用于指定信号处理函数
    sigset_t sa_mask;           // 信号掩码，用于指定在处理当前信号时需要屏蔽哪些信号
    int sa_flags;               // 信号处理标志，用于指定信号处理的一些行为
    void (*sa_restorer)(void);  // 保留字段，不再使用
};

*/
struct sigaction {
	void (*sa_handler)(int);
	sigset_t sa_mask;
	int sa_flags; // 状态码
	void (*sa_restorer)(void); // 回复函数
};

void (*signal(int _sig, void (*_func)(int)))(int);
int raise(int sig);
int kill(pid_t pid, int sig);
int sigaddset(sigset_t *mask, int signo);
int sigdelset(sigset_t *mask, int signo);
int sigemptyset(sigset_t *mask);
int sigfillset(sigset_t *mask);
int sigismember(sigset_t *mask, int signo); /* 1 - is, 0 - not, -1 error */
int sigpending(sigset_t *set);
int sigprocmask(int how, sigset_t *set, sigset_t *oldset);
int sigsuspend(sigset_t *sigmask);
int sigaction(int sig, struct sigaction *act, struct sigaction *oldact);

#endif /* _SIGNAL_H */
