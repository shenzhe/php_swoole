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
#include "Client.h"

#include <netdb.h>

int swClient_create(swClient *cli, int type, int async)
{
	int _domain;
	int _type;
	bzero(cli, sizeof(*cli));
	switch (type)
	{
	case SW_SOCK_TCP:
		_domain = AF_INET;
		_type = SOCK_STREAM;
		break;
	case SW_SOCK_TCP6:
		_domain = AF_INET6;
		_type = SOCK_STREAM;
		break;
	case SW_SOCK_UDP:
		_domain = AF_INET;
		_type = SOCK_DGRAM;
		break;
	case SW_SOCK_UDP6:
		_domain = AF_INET6;
		_type = SOCK_DGRAM;
		break;
	default:
		return SW_ERR;
	}
	cli->sock = socket(_domain, _type, 0);
	if (cli->sock < 0)
	{
		return SW_ERR;
	}
	if (type < SW_SOCK_UDP)
	{
		cli->connect = swClient_tcp_connect;
		cli->recv = swClient_tcp_recv;
		cli->send = swClient_tcp_send;
	}
	else
	{
		cli->connect = swClient_udp_connect;
		cli->recv = swClient_udp_recv;
		cli->send = swClient_udp_send;
	}
	cli->close = swClient_close;
	cli->sock_domain = _domain;
	cli->sock_type = SOCK_DGRAM;
	cli->type = type;
	cli->async = async;
	return SW_OK;
}

static int swClient_inet_addr(struct sockaddr_in *sin, char *string)
{
	struct in_addr tmp;
	struct hostent *host_entry;

	if (inet_aton(string, &tmp))
	{
		sin->sin_addr.s_addr = tmp.s_addr;
	}
	else
	{
		if (!(host_entry = gethostbyname(string)))
		{
			swWarn("SwooleClient: Host lookup failed. Error: %s[%d] ", strerror(errno), errno);
			return SW_ERR;
		}
		if (host_entry->h_addrtype != AF_INET)
		{
			swWarn("Host lookup failed: Non AF_INET domain returned on AF_INET socket");
			return 0;
		}
		memcpy(&(sin->sin_addr.s_addr), host_entry->h_addr_list[0], host_entry->h_length);
	}
	return SW_OK;
}

int swClient_close(swClient *cli)
{
	int fd = cli->sock;
	cli->sock = 0;
	cli->connected = 0;
	return close(fd);
}

int swClient_tcp_connect(swClient *cli, char *host, int port, double timeout, int nonblock)
{
	int ret;
	cli->serv_addr.sin_family = cli->sock_domain;
	cli->serv_addr.sin_port = htons(port);

	if (swClient_inet_addr(&cli->serv_addr, host) < 0)
	{
		return SW_ERR;
	}

	cli->timeout = timeout;
	swSetTimeout(cli->sock, timeout);
	if(nonblock == 1)
	{
		swSetNonBlock(cli->sock);
	}
	else
	{
		swSetBlock(cli->sock);
	}

	while (1)
	{
		ret = connect(cli->sock, (struct sockaddr *) (&cli->serv_addr), sizeof(cli->serv_addr));
		if (ret < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
		}
		break;
	}
	if (ret >= 0)
	{
		cli->connected = 1;
	}
	return ret;
}

int swClient_tcp_send(swClient *cli, char *data, int length)
{
	int written = 0;
	int n;

	//总超时，for循环中计时
	while (written < length)
	{
		n = send(cli->sock, data, length - written, 0);
		if (n < 0)
		{
			//中断
			if (errno == EINTR)
			{
				continue;
			}
			//让出
			else if(errno == EAGAIN)
			{
				swYield();
				continue;
			}
			else
			{
				return SW_ERR;
			}
		}
		written += n;
		data += n;
	}
	return written;
}

int swClient_tcp_recv(swClient *cli, char *data, int len, int waitall)
{
	int flag = 0, ret;
	if (waitall == 1)
	{
		flag = MSG_WAITALL;
	}

	ret = recv(cli->sock, data, len, flag);

	if (ret < 0)
	{
		if (errno == EINTR)
		{
			ret = recv(cli->sock, data, len, flag);
		}
		else
		{
			return SW_ERR;
		}
	}
	return ret;
}

int swClient_udp_connect(swClient *cli, char *host, int port, double timeout, int udp_connect)
{
	int ret;
	char buf[1024];

	cli->timeout = timeout;
	ret = swSetTimeout(cli->sock, timeout);
	if(ret < 0)
	{
		swWarn("setTimeout fail.errno=%d\n", errno);
		return SW_ERR;
	}

	cli->serv_addr.sin_family = cli->sock_domain;
	cli->serv_addr.sin_port = htons(port);

	if (swClient_inet_addr(&cli->serv_addr, host) < 0)
	{
		return SW_ERR;
	}

	if(udp_connect != 1)
	{
		return SW_OK;
	}

	if(connect(cli->sock, (struct sockaddr *) (&cli->serv_addr), sizeof(cli->serv_addr)) == 0)
	{
		//清理connect前的buffer数据遗留
		while(recv(cli->sock, buf, 1024 , MSG_DONTWAIT) > 0);
		cli->connected = 1;
		return SW_OK;
	}
	else
	{
		return SW_ERR;
	}
}

int swClient_udp_send(swClient *cli, char *data, int len)
{
	int n;
	n = sendto(cli->sock, data, len , 0, (struct sockaddr *) (&cli->serv_addr), sizeof(struct sockaddr));
	if(n < 0 || n < len)
	{

		return SW_ERR;
	}
	else
	{
		return n;
	}
}

int swClient_udp_recv(swClient *cli, char *data, int length, int waitall)
{
	int flag = 0, ret;
	socklen_t len;

	if(waitall == 1)
	{
		flag = MSG_WAITALL;

	}
	len = sizeof(struct sockaddr);
	ret = recvfrom(cli->sock, data, length, flag, (struct sockaddr *) (&cli->remote_addr), &len);
	if(ret < 0)
	{
		if(errno == EINTR)
		{
			ret = recvfrom(cli->sock, data, length, flag, (struct sockaddr *) (&cli->remote_addr), &len);
		}
		else
		{
			return SW_ERR;
		}
	}
	return ret;
}
