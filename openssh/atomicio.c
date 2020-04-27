/* $OpenBSD: atomicio.c,v 1.30 2019/01/24 02:42:23 dtucker Exp $ */
/*
 * Copyright (c) 2006 Damien Miller. All rights reserved.
 * Copyright (c) 2005 Anil Madhavapeddy. All rights reserved.
 * Copyright (c) 1995,1999 Theo de Raadt.  All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// NOTE: Originally imported from https://github.com/openssh/openssh-portable/blob/master/atomicio.c
//       Rejigged for my own use, with inspiration from git's own read/write wrappers,
//       as well as gnulib's and busybox's
//       c.f., https://github.com/git/git/blob/master/wrapper.c
//             https://git.savannah.gnu.org/cgit/gnulib.git/tree/lib/safe-read.c
//             https://git.savannah.gnu.org/cgit/gnulib.git/tree/lib/full-write.c
//             https://git.busybox.net/busybox/tree/libbb/read.c?h=1_31_stable

#include "atomicio.h"

// read() with retries on recoverable errors (via polling on EAGAIN).
// Not guaranteed to return len bytes, even on success (like read() itself).
// Always returns read()'s return value as-is.
ssize_t
    xread(int fd, void* buf, size_t len)
{
	// Save a trip to EINVAL if len is large enough to make read() fail.
	if (len > MAX_IO_BUFSIZ) {
		len = MAX_IO_BUFSIZ;
	}

	while (1) {
		ssize_t nr = read(fd, buf, len);
		if (nr < 0) {
			if (errno == EINTR) {
				continue;
			} else if (errno == EAGAIN) {
				struct pollfd pfd = { 0 };
				pfd.fd            = fd;
				pfd.events        = POLLIN;

				poll(&pfd, 1, -1);
				continue;
			}
		}
		return nr;
	}
}

// write() with retries on recoverable errors (via polling on EAGAIN).
// Not guaranteed to write len bytes, even on success (like write() itself).
// Always returns write()'s return value as-is.
ssize_t
    xwrite(int fd, const void* buf, size_t len)
{
	// Save a trip to EINVAL if len is large enough to make write() fail.
	if (len > MAX_IO_BUFSIZ) {
		len = MAX_IO_BUFSIZ;
	}

	while (1) {
		ssize_t nw = write(fd, buf, len);
		if (nw < 0) {
			if (errno == EINTR) {
				continue;
			} else if (errno == EAGAIN) {
				struct pollfd pfd = { 0 };
				pfd.fd            = fd;
				pfd.events        = POLLOUT;

				poll(&pfd, 1, -1);
				continue;
			}
		}
		return nw;
	}
}

// Based on OpenSSH's atomicio6, except we keep the return value/data type of the original call.
// Ensure all of data on socket comes through.
ssize_t
    read_in_full(int fd, void* buf, size_t len)
{
	// Save a trip to EINVAL if len is large enough to make write() fail.
	if (len > MAX_IO_BUFSIZ) {
		len = MAX_IO_BUFSIZ;
	}

	char*  s   = buf;
	size_t pos = 0;
	while (len > pos) {
		ssize_t nr = read(fd, s + pos, len - pos);
		switch (nr) {
			case -1:
				if (errno == EINTR) {
					continue;
				} else if (errno == EAGAIN) {
					struct pollfd pfd = { 0 };
					pfd.fd            = fd;
					pfd.events        = POLLIN;

					poll(&pfd, 1, -1);
					continue;
				}
				return -1;
			case 0:
				// i.e., EoF/EoT
				errno = EPIPE;
				return pos;
			default:
				pos += (size_t) nr;
		}
	}
	return pos;
}

ssize_t
    write_in_full(int fd, const void* buf, size_t len)
{
	// Save a trip to EINVAL if len is large enough to make write() fail.
	if (len > MAX_IO_BUFSIZ) {
		len = MAX_IO_BUFSIZ;
	}

	const char* s   = buf;
	size_t      pos = 0;
	while (len > pos) {
		ssize_t nw = write(fd, s + pos, len - pos);
		switch (nw) {
			case -1:
				if (errno == EINTR) {
					continue;
				} else if (errno == EAGAIN) {
					struct pollfd pfd = { 0 };
					pfd.fd            = fd;
					pfd.events        = POLLOUT;

					poll(&pfd, 1, -1);
					continue;
				}
				return -1;
			case 0:
				// That only makes sense for regular files.
				// On the other hand, write() returning 0 on !regular files is UB.
				errno = ENOSPC;
				return -1;
			default:
				pos += (size_t) nw;
		}
	}
	return pos;
}

// Exactly like write_in_full, but using send w/ flags set to MSG_NOSIGNAL,
// so we can handle EPIPE without having to deal with signals.
ssize_t
    send_in_full(int sockfd, const void* buf, size_t len)
{
	// Save a trip to EINVAL if len is large enough to make write() fail.
	if (len > MAX_IO_BUFSIZ) {
		len = MAX_IO_BUFSIZ;
	}

	const char* s   = buf;
	size_t      pos = 0;
	while (len > pos) {
		ssize_t nw = send(sockfd, s + pos, len - pos, MSG_NOSIGNAL);
		switch (nw) {
			case -1:
				if (errno == EINTR) {
					continue;
				} else if (errno == EAGAIN) {
					struct pollfd pfd = { 0 };
					pfd.fd            = sockfd;
					pfd.events        = POLLOUT;

					poll(&pfd, 1, -1);
					continue;
				}
				return -1;
			default:
				pos += (size_t) nw;
		}
	}
	return pos;
}
