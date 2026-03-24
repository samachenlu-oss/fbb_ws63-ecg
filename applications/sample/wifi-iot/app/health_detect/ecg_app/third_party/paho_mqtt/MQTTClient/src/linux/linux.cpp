/**
 * Copyright (c) 2020 HiSilicon (Shanghai) Technologies CO., LIMITED.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.  *
 *
 * History: \n
 * 2022-09-16， Create file. \n
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <stdlib.h>
#include <string.h>
#include <signal.h>


class IPStack
{
public:
  IPStack()
  {

  }

  int connect(const char* hostname, int port)
  {
		int type = SOCK_STREAM;
		struct sockaddr_in address;
		int rc = -1;
		sa_family_t family = AF_INET;
		struct addrinfo *result = NULL;
		struct addrinfo hints = {0, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};

		if ((rc = getaddrinfo(hostname, NULL, &hints, &result)) == 0)
		{
			struct addrinfo* res = result;

			/* prefer ip4 addresses */
			while (res)
			{
				if (res->ai_family == AF_INET)
				{
					result = res;
					break;
				}
				res = res->ai_next;
			}

			if (result->ai_family == AF_INET)
			{
				address.sin_port = htons(port);
				address.sin_family = family = AF_INET;
				address.sin_addr = ((struct sockaddr_in*)(result->ai_addr))->sin_addr;
			}
			else
				rc = -1;

			freeaddrinfo(result);
		}

		if (rc == 0)
		{
			mysock = socket(family, type, 0);
			if (mysock != -1)
			{
				int opt = 1;

				//if (setsockopt(mysock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&opt, sizeof(opt)) != 0)
				//	printf("Could not set SO_NOSIGPIPE for socket %d", mysock);

				rc = ::connect(mysock, (struct sockaddr*)&address, sizeof(address));
			}
		}

        return rc;
    }

  // return -1 on error, or the number of bytes read
  // which could be 0 on a read timeout
  int read(unsigned char* buffer, int len, int timeout_ms)
  {
		struct timeval interval = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
		if (interval.tv_sec < 0 || (interval.tv_sec == 0 && interval.tv_usec <= 0))
		{
			interval.tv_sec = 0;
			interval.tv_usec = 100;
		}

		setsockopt(mysock, SOL_SOCKET, SO_RCVTIMEO, (char *)&interval, sizeof(struct timeval));

		int bytes = 0;
    int i = 0; const int max_tries = 10;
		while (bytes < len)
		{
			int rc = ::recv(mysock, &buffer[bytes], (size_t)(len - bytes), 0);
			if (rc == -1)
			{
        if (errno != EAGAIN && errno != EWOULDBLOCK)
          bytes = -1;
        break;
			}
			else
				bytes += rc;
      if (++i >= max_tries)
        break;
      if (rc == 0)
        break;
		}
		return bytes;
  }

  int write(unsigned char* buffer, int len, int timeout)
  {
		struct timeval tv;

		tv.tv_sec = 0;  /* 30 Secs Timeout */
		tv.tv_usec = timeout * 1000;  // Not init'ing this can cause strange errors

		setsockopt(mysock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv,sizeof(struct timeval));
		int	rc = ::write(mysock, buffer, len);
		//printf("write rc %d\n", rc);
		return rc;
  }

	int disconnect()
	{
		return ::close(mysock);
	}

private:

    int mysock;
};


class Countdown
{
public:
  Countdown()
  {

  }

  Countdown(int ms)
  {
		countdown_ms(ms);
  }


  bool expired()
  {
		struct timeval now, res;
		gettimeofday(&now, NULL);
		timersub(&end_time, &now, &res);
		//printf("left %d ms\n", (res.tv_sec < 0) ? 0 : res.tv_sec * 1000 + res.tv_usec / 1000);
		//if (res.tv_sec > 0 || res.tv_usec > 0)
		//	printf("expired %d %d\n", res.tv_sec, res.tv_usec);
        return res.tv_sec < 0 || (res.tv_sec == 0 && res.tv_usec <= 0);
  }


  void countdown_ms(int ms)
  {
		struct timeval now;
		gettimeofday(&now, NULL);
		struct timeval interval = {ms / 1000, (ms % 1000) * 1000};
		//printf("interval %d %d\n", interval.tv_sec, interval.tv_usec);
		timeradd(&now, &interval, &end_time);
  }


  void countdown(int seconds)
  {
		struct timeval now;
		gettimeofday(&now, NULL);
		struct timeval interval = {seconds, 0};
		timeradd(&now, &interval, &end_time);
  }


  int left_ms()
  {
		struct timeval now, res;
		gettimeofday(&now, NULL);
		timersub(&end_time, &now, &res);
		//printf("left %d ms\n", (res.tv_sec < 0) ? 0 : res.tv_sec * 1000 + res.tv_usec / 1000);
        return (res.tv_sec < 0) ? 0 : res.tv_sec * 1000 + res.tv_usec / 1000;
  }

private:

	struct timeval end_time;
};
