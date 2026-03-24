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

#if !defined(IPSTACK_H)
#define IPSTACK_H

#ifndef WiFi_h
  #include <SPI.h>
#endif

#include <Client.h>

class IPStack
{
public:
    IPStack(Client& client) : client(&client)
    {

    }

    int connect(char* hostname, int port)
    {
        return client->connect(hostname, port);
    }

    int connect(uint32_t hostname, int port)
    {
        return client->connect(hostname, port);
    }

    int read(unsigned char* buffer, int len, int timeout)
    {
        int interval = 10;  // all times are in milliseconds
		int total = 0, rc = -1;

		if (timeout < 30)
			interval = 2;
		while (client->available() < len && total < timeout)
		{
			delay(interval);
			total += interval;
		}
		if (client->available() >= len)
			rc = client->readBytes((char*)buffer, len);
		return rc;
    }

    int write(unsigned char* buffer, int len, int timeout)
    {
        client->setTimeout(timeout);
		return client->write((uint8_t*)buffer, len);
    }

    int disconnect()
    {
        client->stop();
        return 0;
    }

private:

    Client* client;
};

#endif
