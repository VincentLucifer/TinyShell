# TinyShell · Unix 进程与作业控制

[简体中文](README.md) | [English](README_EN.md)

基于 **CS:APP Shell Lab** 的 C 语言 Shell 实验，围绕进程创建、前后台作业、进程组与信号处理，实现一个可交互的 Unix 作业控制流程。

**技术栈：** C · POSIX · Linux · GNU Make · Perl

## 核心功能

| 功能 | 对应实现 |
| --- | --- |
| 外部程序执行 | `fork()` 创建进程，`execve()` 执行指定路径 |
| 前后台运行 | 默认前台；命令末尾 `&` 表示后台 |
| 作业列表 | 维护 PID、JID、状态及命令行；`jobs` 查看 |
| 恢复作业 | `bg` / `fg` 支持 PID 或 `%JID` |
| 中断与挂起 | 将 Ctrl+C / Ctrl+Z 对应信号转发给前台进程组 |
| 子进程状态回收 | `SIGCHLD` 处理器调用 `waitpid()` 获取状态变化 |
| 命令解析 | 参数分割，支持单引号包裹的参数 |
| 退出 | `quit` 或输入结束 |

外部程序按传入路径执行，例如 `/bin/ls` 或 `./myspin`。

## 关键设计

### 进程组隔离

子进程通过 `setpgid(0, 0)` 建立独立进程组。Shell 使用负进程组 ID 发送信号，将终端控制作用于整个前台作业。

### 信号屏蔽与作业表

在创建进程及修改作业表期间使用信号屏蔽，协调 `fork()`、`addjob()` 和子进程退出通知的执行顺序。后台作业、前台作业与停止作业分别用 `BG`、`FG`、`ST` 表示。

### 前台等待

`waitfg()` 结合 `sigprocmask()` 与 `sigsuspend()` 等待信号通知，`sigchld_handler()` 更新状态并回收已退出子进程，避免依赖持续轮询。

## 编译与运行

在 Linux 或 WSL 中准备 GCC、Make；trace 驱动另外使用 Perl。

```bash
git clone https://github.com/VincentLucifer/TinyShell.git
cd TinyShell/shlab-handout
make
./tsh
```

示例交互：

```text
tsh> /bin/echo 'hello tiny shell'
tsh> ./myspin 10 &
tsh> jobs
tsh> fg %1
```

在前台作业运行时按 Ctrl+Z 可挂起，再用 `jobs` 查看实际 JID，使用 `bg %<JID>` 恢复到后台，或用 `fg %<JID>` 切回前台。

| 内建命令 | 作用 |
| --- | --- |
| `jobs` | 列出作业 |
| `bg <PID>` / `bg %<JID>` | 恢复到后台 |
| `fg <PID>` / `fg %<JID>` | 切换到前台 |
| `quit` | 退出 |

启动参数：`-h` 显示帮助，`-v` 输出诊断信息，`-p` 关闭提示符，方便测试驱动交互。

## Trace 评估

仓库包含 `trace01.txt` 至 `trace16.txt`、驱动 `sdriver.pl` 和参考程序 `tshref`。

```bash
# 当前实现：指定 trace
make test01
make test16

# 同一 trace 的参考输出
make rtest01

# 显式调用驱动
perl sdriver.pl -t trace15.txt -s ./tsh -a "-p"
```

Trace 覆盖基本命令、前后台执行、信号和作业控制，可按序查看输入和输出行为。

## 代码导航

| 文件 | 内容 |
| --- | --- |
| `shlab-handout/tsh.c` | Shell 主循环、解析、作业表、内建命令与信号处理 |
| `shlab-handout/csapp.c` / `csapp.h` | 系统调用包装与辅助函数 |
| `shlab-handout/Makefile` | 构建和 trace 目标 |
| `shlab-handout/sdriver.pl` | Trace 驱动 |
| `shlab-handout/myspin.c` 等 | 作业控制辅助程序 |
| 根目录实验报告 | 课程实验记录 |

## 来源

基于 CS:APP Shell Lab 教学框架完成的计算机系统实验。项目重点是理解进程生命周期、异步信号与作业状态之间的协调关系。
