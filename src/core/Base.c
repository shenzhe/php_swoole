/*
  +----------------------------------------------------------------------+
  | Swoole                                                               |
  +----------------------------------------------------------------------+
  | This source file is subject to version 2.0 of the Apache license,    |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.apache.org/licenses/LICENSE-2.0.html                      |
  | If you did not receive a copy of the Apache2.0 license and are unable|
  | to obtain it through the world-wide-web, please send a note to       |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Tianfeng Han  <mikan.tenny@gmail.com>                        |
  +----------------------------------------------------------------------+
*/

#include "swoole.h"
#include "atomic.h"

uint64_t sw_hash_key(char *str, int str_len)
{
	uint64_t hash = 5381;
	int c, i = 0;
	for (c = *str++; i < str_len; i++)
	{
		hash = (*((hash * 33) + str)) & 0x7fffffff;
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

int swSocket_create(int type)
{
	int _domain;
	int _type;

	switch (type)
	{
	case SW_SOCK_TCP:
		_domain = PF_INET;
		_type = SOCK_STREAM;
		break;
	case SW_SOCK_TCP6:
		_domain = PF_INET6;
		_type = SOCK_STREAM;
		break;
	case SW_SOCK_UDP:
		_domain = PF_INET;
		_type = SOCK_DGRAM;
		break;
	case SW_SOCK_UDP6:
		_domain = PF_INET6;
		_type = SOCK_DGRAM;
		break;
	default:
		return SW_ERR;
	}
	return socket(_domain, _type, 0);
}

SWINLINE void swFloat2timeval(float timeout, long int *sec, long int *usec)
{
	*sec = (int) timeout;
	*usec = (int) ((timeout * 1000 * 1000) - ((*sec) * 1000 * 1000));
}

int swSocket_listen(int type, char *host, int port, int backlog)
{
	int sock;
	int option;
	int ret;

	struct sockaddr_in addr_in4;
	struct sockaddr_in6 addr_in6;

	sock = swSocket_create(type);
	if (sock < 0)
	{
		swWarn("swSocket_listen: Create socket fail.type=%d|Errno=%d", type, errno);
		return SW_ERR;
	}
	//reuse
	option = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(int));

	//IPv6
	if (type > SW_SOCK_UDP)
	{
		bzero(&addr_in6, sizeof(addr_in6));
		inet_pton(AF_INET6, host, &(addr_in6.sin6_addr));
		addr_in6.sin6_port = htons(port);
		addr_in6.sin6_family = AF_INET6;
		ret = bind(sock, (struct sockaddr *) &addr_in6, sizeof(addr_in6));
	}
	//IPv4
	else
	{
		bzero(&addr_in4, sizeof(addr_in4));
		inet_pton(AF_INET, host, &(addr_in4.sin_addr));
		addr_in4.sin_port = htons(port);
		addr_in4.sin_family = AF_INET;
		ret = bind(sock, (struct sockaddr *) &addr_in4, sizeof(addr_in4));
	}
	//将监听套接字同sockaddr绑定
	if (ret < 0)
	{
		swWarn("Bind fail.type=%d|host=%s|port=%d. Error: %s [%d]", type, host, port, strerror(errno), errno);
		return SW_ERR;
	}
	if (type == SW_SOCK_UDP || type == SW_SOCK_UDP6)
	{
		return sock;
	}
	//开始监听套接字
	ret = listen(sock, backlog);
	if (ret < 0)
	{
		swWarn("Listen fail.type=%d|host=%s|port=%d. Error: %s [%d]", type, host, port, strerror(errno), errno);
		return SW_ERR;
	}
	swSetNonBlock(sock);
	return sock;
}

SWINLINE int swRead(int fd, void *buf, int len)
{
	int n = 0, nread;
	sw_errno = 0;
	return recv(fd, buf, len, 0);


	while (1)
	{
		nread = recv(fd, buf + n, len - n, 0);

//		swWarn("Read Len=%d|Errno=%d", nread, errno);
		//遇到错误
		if (nread < 0)
		{
			//中断
			if (errno == EINTR)
			{
				continue;
			}
			//出错了
			else
			{
				if (errno == EAGAIN && n > 0)
				{
					break;
				}
				else
				{
					sw_errno = -1; //异常
					return SW_ERR;
				}
			}
		}
		//连接已关闭
		//需要检测errno来区分是EAGAIN还是ECONNRESET
		else if (nread == 0)
		{
			//这里直接break,保证读到的数据被处理
			break;
		}
		else
		{
			n += nread;
			//内存读满了，还可能有数据
			if (n == len)
			{
				sw_errno = EAGAIN;
				break;
			}
			//已读完 n < len
			else
			{
				break;
			}
		}

	}
	return n;
}

/**
 * for GDB
 */
void swBreakPoint()
{
}

SWINLINE int swWrite(int fd, void *buf, int count)
{
	int nwritten = 0, totlen = 0;
	while (totlen != count)
	{
		nwritten = write(fd, buf, count - totlen);
		if (nwritten == 0)
		{
			return totlen;
		}
		if (nwritten == -1)
		{
			if (errno == EINTR)
			{
				continue;
			}
			else if (errno == EAGAIN)
			{
				swYield();
				continue;
			}
			else
			{
				return -1;
			}
		}
		totlen += nwritten;
		buf += nwritten;
	}
	return totlen;
}

int swPipeNotify_auto(swPipe *p, int blocking, int semaphore)
{
#ifdef HAVE_EVENTFD
	return swPipeEventfd_create(p, blocking, semaphore);
#else
	return swPipeBase_create(p, blocking);
#endif
}

//将套接字设置为非阻塞方式
SWINLINE void swSetNonBlock(int sock)
{
	int	opts, ret;
	do
	{
		opts = fcntl(sock, F_GETFL);
	}
	while(opts <0 && errno == EINTR);
	if (opts < 0)
	{
		swWarn("fcntl(sock,GETFL) fail");
	}
	opts = opts | O_NONBLOCK;
	do
	{
		ret = fcntl(sock, F_SETFL, opts);
	}
	while(ret <0 && errno == EINTR);
	if (ret < 0)
	{
		swWarn("fcntl(sock,SETFL,opts) fail");
	}
}

SWINLINE void swSetBlock(int sock)
{
	int opts, ret;
	do
	{
		opts = fcntl(sock, F_GETFL);
	}
	while(opts <0 && errno == EINTR);

	if (opts < 0)
	{
		swWarn("fcntl(sock,GETFL) fail");
	}
	opts = opts & ~O_NONBLOCK;
	do
	{
		ret = fcntl(sock, F_SETFL, opts);
	}
	while(ret <0 && errno == EINTR);
	if (ret < 0)
	{
		swWarn("fcntl(sock,SETFL,opts) fail");
	}
}

SWINLINE int swAccept(int server_socket, struct sockaddr_in *addr, int addr_len)
{
	int conn_fd;
	bzero(addr, addr_len);

	while (1)
	{
#ifdef SW_USE_ACCEPT4
		conn_fd = accept4(server_socket, (struct sockaddr *) addr, (socklen_t *) &addr_len, SOCK_NONBLOCK);
#else
		conn_fd = accept(server_socket, (struct sockaddr *) addr, (socklen_t *) &addr_len);
#endif
		if (conn_fd < 0)
		{
			//中断
			if (errno == EINTR)
			{
				continue;
			}
			else
			{
				swTrace("accept fail. Error: %s[%d]", strerror(errno), errno);
				return SW_ERR;
			}
		}
#ifndef SW_USE_ACCEPT4
		swSetNonBlock(conn_fd);
#endif
		break;
	}
	return conn_fd;
}

SWINLINE int swSetTimeout(int sock, double timeout)
{
	int ret;
	struct timeval timeo;
	timeo.tv_sec = (int) timeout;
	timeo.tv_usec = (int) ((timeout - timeo.tv_sec) * 1000 * 1000);
	ret = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (void *) &timeo, sizeof(timeo));
	if (ret < 0)
	{
		return SW_ERR;
	}
	ret = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *) &timeo, sizeof(timeo));
	if (ret < 0)
	{
		return SW_ERR;
	}
	return SW_OK;
}

/**
 * clear all singal
 */
void swSingalNone()
{
	sigset_t mask;
	sigfillset(&mask);
	int ret = pthread_sigmask(SIG_BLOCK, &mask, NULL);
	if (ret < 0)
	{
		swWarn("pthread_sigmask fail: %s", strerror(ret));
	}
}

swSignalFunc swSignalSet(int sig, swSignalFunc func, int restart, int mask)
{
	struct sigaction act, oact;
	act.sa_handler = func;
	if (mask)
	{
		sigfillset(&act.sa_mask);
	}
	else
	{
		sigemptyset(&act.sa_mask);
	}
	act.sa_flags = 0;
	if (sigaction(sig, &act, &oact) < 0)
	{
		return NULL;
	}
	return oact.sa_handler;
}

#ifndef HAVE_CLOCK_GETTIME
#ifdef __MACH__
int clock_gettime(clock_id_t which_clock, struct timespec *t)
{
	// be more careful in a multithreaded environement
	if (!orwl_timestart)
	{
		mach_timebase_info_data_t tb =
		{	0};
		mach_timebase_info(&tb);
		orwl_timebase = tb.numer;
		orwl_timebase /= tb.denom;
		orwl_timestart = mach_absolute_time();
	}
	double diff = (mach_absolute_time() - orwl_timestart) * orwl_timebase;
	t->tv_sec = diff * ORWL_NANO;
	t->tv_nsec = diff - (t->tv_sec * ORWL_GIGA);
	return 0;
}
#endif
#endif
