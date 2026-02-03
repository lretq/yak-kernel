#ifndef _ABIBITS_FCNTL_H
#define _ABIBITS_FCNTL_H

#define O_PATH 010000000

#define O_ACCMODE (03 | O_PATH)
#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02

#define O_CREAT 0100
#define O_NOCTTY 0400
#define O_NOFOLLOW 0400000
#define O_CLOEXEC 02000000

#if defined(__x86_64__) || defined(__i386__) || defined(__riscv) || defined(__loongarch64)
#define O_DIRECT      040000
#define O_LARGEFILE  0100000
#define O_DIRECTORY  0200000
#define O_NOFOLLOW   0400000
#elif defined(__aarch64__) || defined(__m68k__)
#define O_DIRECTORY   040000
#define O_NOFOLLOW   0100000
#define O_DIRECT     0200000
#define O_LARGEFILE  0400000
#else
#warning "Missing <fcntl.h> support for this architecture!"
#endif

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

#define FD_CLOEXEC 1

#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_EMPTY_PATH 0x1000

#endif
