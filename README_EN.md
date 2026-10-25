# TinyShell · Unix Processes and Job Control

[简体中文](README.md) | [English](README_EN.md)

A C shell experiment based on **CS:APP Shell Lab**, connecting process creation, foreground/background jobs, process groups and signal handling into an interactive Unix job-control workflow.

**Stack:** C · POSIX · Linux · GNU Make · Perl

## Features

| Feature | Implementation |
| --- | --- |
| External programs | `fork()` creates a process; `execve()` executes the supplied path |
| Foreground/background execution | Foreground by default; trailing `&` selects background |
| Job listing | Tracks PID, JID, state and command line; inspect with `jobs` |
| Job resumption | `bg` / `fg` accept a PID or `%JID` |
| Interrupt/suspend | Forwards Ctrl+C / Ctrl+Z signals to the foreground process group |
| Child-state collection | `SIGCHLD` handler uses `waitpid()` to collect status changes |
| Parsing | Argument splitting with single-quoted arguments |
| Exit | `quit` or end of input |

External programs use the supplied path, such as `/bin/ls` or `./myspin`.

## Key design choices

### Process-group isolation

Children establish independent process groups with `setpgid(0, 0)`. Negative process-group IDs let the shell deliver signals to the complete foreground job.

### Signal masking and the job table

Signal masks coordinate process creation, job-table updates and child-exit notifications around `fork()` and `addjob()`. Jobs use `BG`, `FG` and `ST` states for background, foreground and stopped execution.

### Foreground waiting

`waitfg()` combines `sigprocmask()` and `sigsuspend()` to wait for notifications. `sigchld_handler()` updates job states and reaps exited children instead of relying on continuous polling.

## Build and run

Use Linux or WSL with GCC and Make. Trace evaluation also requires Perl.

```bash
git clone https://github.com/VincentLucifer/TinyShell.git
cd TinyShell/shlab-handout
make
./tsh
```

Example interaction:

```text
tsh> /bin/echo 'hello tiny shell'
tsh> ./myspin 10 &
tsh> jobs
tsh> fg %1
```

Press Ctrl+Z while a foreground job runs to suspend it. Inspect its actual JID with `jobs`, resume it using `bg %<JID>`, or bring it forward with `fg %<JID>`.

| Built-in | Purpose |
| --- | --- |
| `jobs` | List jobs |
| `bg <PID>` / `bg %<JID>` | Resume in the background |
| `fg <PID>` / `fg %<JID>` | Move to the foreground |
| `quit` | Exit |

Startup flags: `-h` for help, `-v` for diagnostics and `-p` to suppress the prompt for driver interaction.

## Trace evaluation

The repository includes `trace01.txt` through `trace16.txt`, `sdriver.pl` and the reference executable `tshref`.

```bash
# Run a trace against this implementation
make test01
make test16

# Reference output for the same trace
make rtest01

# Invoke the driver explicitly
perl sdriver.pl -t trace15.txt -s ./tsh -a "-p"
```

Traces exercise basic commands, foreground/background execution, signals and job control, making input/output behaviour easy to inspect in sequence.

## Code map

| File | Contents |
| --- | --- |
| `shlab-handout/tsh.c` | Main loop, parsing, job table, built-ins and signal handlers |
| `shlab-handout/csapp.c` / `csapp.h` | System-call wrappers and helpers |
| `shlab-handout/Makefile` | Build and trace targets |
| `shlab-handout/sdriver.pl` | Trace driver |
| `shlab-handout/myspin.c` and peers | Job-control helper programs |
| Root-level report | Course experiment record |

## Background

A computer-systems experiment built on the CS:APP Shell Lab teaching framework, focusing on coordination between process lifecycles, asynchronous signals and job states.
