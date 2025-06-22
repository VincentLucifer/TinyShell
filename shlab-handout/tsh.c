/*
 * tsh - A tiny shell program with job control
 *
 * <Put your name and login ID here>
 * 文梓锋 220110120
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "csapp.h"

/* Misc manifest constants */
// #define MAXLINE    1024   /* max line size */
#define MAXARGS 128    /* max args on a command line */
#define MAXJOBS 16     /* max jobs at any point in time */
#define MAXJID 1 << 16 /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;   /* defined in libc */
char prompt[] = "tsh> "; /* command line prompt (DO NOT CHANGE) */
int verbose = 0;         /* if true, print additional output */
int nextjid = 1;         /* next job ID to allocate */
char sbuf[MAXLINE];      /* for composing sprintf messages */

volatile sig_atomic_t fg_reaped = 0; /* if fg process is reaped*/

struct job_t
{                          /* The job struct */
    pid_t pid;             /* job PID */
    int jid;               /* job ID [1, 2, ...] */
    int state;             /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE]; /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */

/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF)
    {
        switch (c)
        {
        case 'h': /* print help message */
            usage();
            break;
        case 'v': /* emit additional diagnostic info */
            verbose = 1;
            break;
        case 'p':            /* don't print a prompt */
            emit_prompt = 0; /* handy for automatic testing */
            break;
        default:
            usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT, sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1)
    {

        /* Read command line */
        if (emit_prompt)
        {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin))
        { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */
void eval(char *cmdline)
{
    char *argv[MAXARGS]; // 参数数组
    int bg;              // 是否是后台任务？
    pid_t pid;           // 子进程 ID
    int jid;

    bg = parseline(cmdline, argv); // 解析命令行（获取参数和后台标志）

    if (argv[0] == NULL) // 空命令，直接返回
        return;

    if (!builtin_cmd(argv))
    { // 如果不是内建命令
        sigset_t mask_all, mask_one, prev_one;
        sigemptyset(&mask_one);
        sigaddset(&mask_one, SIGCHLD);                // mask_one 只屏蔽 SIGCHLD（防止竞态）
        sigfillset(&mask_all);                        // mask_all 屏蔽所有信号
        sigprocmask(SIG_BLOCK, &mask_one, &prev_one); // 屏蔽 SIGCHLD

        if ((pid = fork()) == 0)
        {                                              // 子进程 child
            sigprocmask(SIG_SETMASK, &prev_one, NULL); // 子进程取消屏蔽
            setpgid(0, 0);                             // 为当前子进程创建新的进程组，其pgid为当前子进程的pid
            if (execve(argv[0], argv, environ) < 0)
            {
                printf("%s: Command not found\n", argv[0]);
                sio_puts(sbuf);
                exit(0);
            }
        }
        else
        {
            // 执行父进程
            int state = bg ? BG : FG;
            sigprocmask(SIG_BLOCK, &mask_all, NULL);   // 临界区，防止信号竞争
            addjob(jobs, pid, state, cmdline);         // 加入作业表
            sigprocmask(SIG_SETMASK, &prev_one, NULL); // 恢复信号
            jid = pid2jid(pid);
            if (!bg)
            {
                waitfg(pid); // 前台作业则等待它结束
            }
            else
            {
                sprintf(sbuf, "[%d] (%d) %s", jid, pid, cmdline); // 后台作业打印信息
                sio_puts(sbuf);
            }
        }
    }
    return;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
int parseline(const char *cmdline, char **argv)
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf) - 1] = ' ';   /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'')
    {
        buf++;
        delim = strchr(buf, '\'');
    }
    else
    {
        delim = strchr(buf, ' ');
    }

    while (delim)
    {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'')
        {
            buf++;
            delim = strchr(buf, '\'');
        }
        else
        {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;

    if (argc == 0) /* ignore blank line */
        return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc - 1] == '&')) != 0)
    {
        argv[--argc] = NULL;
    }
    return bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
int builtin_cmd(char **argv)
{
    if (!strcmp(argv[0], "quit"))
    { // quit：退出程序
        exit(0);
    }
    else if (!strcmp(argv[0], "jobs"))
    {
        listjobs(jobs); // jobs：打印作业列表
        return 1;
    }
    else if (!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg"))
    {
        do_bgfg(argv); // 调用处理 bg/fg 的函数
        return 1;
    }

    return 0; // 不是内建命令，返回 0 继续走 fork/exec
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv)
{
    struct job_t *job = NULL;
    int jid;
    pid_t pid;

    // 错误：缺少参数
    if (argv[1] == NULL)
    {
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }

    // 参数为 %jobid
    if (argv[1][0] == '%')
    {
        jid = atoi(&argv[1][1]); // 跳过 %
        if (!(job = getjobjid(jobs, jid)))
        {
            printf("%%%d: No such job\n", jid);
            return;
        }
    }
    // 参数为 pid
    else if (isdigit(argv[1][0]))
    {
        pid = atoi(argv[1]);
        if (!(job = getjobpid(jobs, pid)))
        {
            printf("(%d): No such process\n", pid);
            return;
        }
    }
    // 错误：参数既不是 pid 也不是 %jobid
    else
    {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }

    // 发送 SIGCONT 信号唤醒整个作业（即其进程组）
    kill(-(job->pid), SIGCONT);

    // 处理 bg 命令
    if (!strcmp(argv[0], "bg"))
    {
        job->state = BG;
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
    }
    // 处理 fg 命令
    else if (!strcmp(argv[0], "fg"))
    {
        job->state = FG;
        waitfg(job->pid); // 阻塞直到前台任务完成
    }
    else
    {
        printf("do_bgfg: unknown command %s\n", argv[0]);
    }

    return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    sigset_t mask, prev_mask;

    // 创建一个信号集合 mask，并将其初始化为空
    Sigemptyset(&mask);

    // 向 mask 中添加 SIGCHLD 信号（希望阻塞这个信号）
    Sigaddset(&mask, SIGCHLD);

    // 设置阻塞信号掩码：阻塞 SIGCHLD，同时保存原来的信号掩码到 prev_mask
    Sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    /*
     * 如果 fg_reaped 还为 0，说明 sigchld handler 还没处理完前台进程。
     * 则调用 Sigsuspend 临时恢复原来的信号掩码（允许 SIGCHLD），并暂停执行直到收到信号。
     * 该操作是原子的：避免在检查 fg_reaped 和挂起之间发生竞态（miss signal）。
     */
    while (!fg_reaped)
        Sigsuspend(&prev_mask);

    // 前台进程已被回收（sigchld handler 设置了 fg_reaped=1），重置为0以便下次使用
    fg_reaped = 0;

    // 恢复原来的信号掩码，解除对 SIGCHLD 的阻塞
    Sigprocmask(SIG_SETMASK, &prev_mask, NULL);

    return;
}

/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig)
{
    sigset_t all_mask, prev_mask;

    // 阻塞所有信号，防止在处理中被其他信号打断，保护 jobs 列表
    Sigfillset(&all_mask);
    Sigprocmask(SIG_SETMASK, &all_mask, &prev_mask);

    int old_errno = errno; // 保存原始 errno，以免中断系统调用造成错误覆盖
    int status;
    pid_t pid, fg_pid = fgpid(jobs); // 获取当前前台作业的 PID
    struct job_t *job;

    // 回收所有已变为 zombie 的进程，或状态有变的子进程
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
    {
        if (pid == fg_pid)
            fg_reaped = 1; // 通知前台进程等待函数 waitfg 它已经被回收或状态已变

        // 根据 PID 获取对应的 job 结构体
        job = getjobpid(jobs, pid);
        if (!job)
            continue; // 如果找不到 job（理论上不应该），跳过

        // 如果子进程是被信号终止的，例如 Ctrl+C（SIGINT）
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGINT)
        {
            sprintf(sbuf, "Job [%d] (%d) terminated by signal %d\n", job->jid, pid, SIGINT);
            sio_puts(sbuf);       // 使用安全 I/O 打印信息
            deletejob(jobs, pid); // 从 jobs 列表中移除
        }
        // 如果子进程被停止了（比如 Ctrl+Z 发出的 SIGTSTP）
        else if (WIFSTOPPED(status))
        {
            job->state = ST; // 设置为 stopped 状态
            if (WSTOPSIG(status) == SIGTSTP)
            {
                sprintf(sbuf, "Job [%d] (%d) stopped by signal %d\n", job->jid, pid, SIGTSTP);
                sio_puts(sbuf);
            }
        }
        // 如果子进程是正常退出的
        else if (WIFEXITED(status))
        {
            deletejob(jobs, pid); // 从 job 表中删除
        }
    }

    errno = old_errno;                          // 恢复原始 errno
    Sigprocmask(SIG_SETMASK, &prev_mask, NULL); // 恢复信号屏蔽字
    return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
    sigset_t all_mask, prev_mask;

    // 填充 all_mask 为所有信号，这样可以阻塞所有信号
    Sigfillset(&all_mask);
    // 阻塞所有信号，并保存当前信号掩码到 prev_mask
    Sigprocmask(SIG_SETMASK, &all_mask, &prev_mask);
    // 获取当前前台进程的 pid
    pid_t fg_pid = fgpid(jobs);

    // 如果没有前台进程（即 fg_pid 为 0），则直接恢复信号掩码并返回
    if (fg_pid == 0)
    {
        Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        return;
    }

    // 向前台进程发送 SIGINT 信号，终止它的执行
    Kill(-fg_pid, sig);
    // 恢复之前的信号掩码
    Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
    // jobs列表的状态由sigchild handler维护
    sigset_t all_mask, prev_mask;
    Sigfillset(&all_mask); // 填充 all_mask 为所有信号，这样可以阻塞所有信号
    // 设置信号掩码，阻塞所有信号并保存当前信号掩码到 prev_mask
    Sigprocmask(SIG_SETMASK, &all_mask, &prev_mask);
    // 获取当前前台进程的 pid
    pid_t fg_pid = fgpid(jobs);

    // 如果没有前台进程（即 fg_pid 为 0），则直接恢复信号掩码并返回
    if (fg_pid == 0)
    {
        Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        return;
    }
    // 向前台进程发送 SIGTSTP 信号，暂停它的执行
    Kill(-fg_pid, sig);
    // 恢复之前的信号掩码
    Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job)
{
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
    int i, max = 0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid == 0)
        {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if (verbose)
            {
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid == pid)
        {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs) + 1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid)
{
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
        {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid != 0)
        {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state)
            {
            case BG:
                printf("Running ");
                break;
            case FG:
                printf("Foreground ");
                break;
            case ST:
                printf("Stopped ");
                break;
            default:
                printf("listjobs: Internal error: job[%d].state=%d ",
                       i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/

/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void)
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
// void unix_error(char *msg)
// {
//     fprintf(stdout, "%s: %s\n", msg, strerror(errno));
//     exit(1);
// }

/*
 * app_error - application-style error routine
 */
// void app_error(char *msg)
// {
//     fprintf(stdout, "%s\n", msg);
//     exit(1);
// }

/*
 * Signal - wrapper for the sigaction function
 */
// handler_t *Signal(int signum, handler_t *handler)
// {
//     struct sigaction action, old_action;

//     action.sa_handler = handler;
//     sigemptyset(&action.sa_mask); /* block sigs of type being handled */
//     action.sa_flags = SA_RESTART; /* restart syscalls if possible */

//     if (sigaction(signum, &action, &old_action) < 0)
// 	unix_error("Signal error");
//     return (old_action.sa_handler);
// }

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig)
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
