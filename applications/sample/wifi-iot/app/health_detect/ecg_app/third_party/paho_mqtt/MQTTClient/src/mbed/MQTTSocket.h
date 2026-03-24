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
#if !defined(MQTTSOCKET_H)
#define MQTTSOCKET_H

#include "MQTTmbed.h"
#include <EthernetInterface.h>
#include <Timer.h>

class MQTTSocket
{
public:
    MQTTSocket(EthernetInterface *anet)
    {
        net = anet;
        open = false;
    }
    
    int connect(char* hostname, int port, int timeout=1000)
    {
        if (open)
            disconnect();
        nsapi_error_t rc = mysock.open(net);
        open = true;
        mysock.set_blocking(true);
        mysock.set_timeout((unsigned int)timeout);  
        rc = mysock.connect(hostname, port);
        mysock.set_blocking(false);  // blocking timeouts seem not to work
        return rc;
    }

    // common read/write routine, avoiding blocking timeouts
    int common(unsigned char* buffer, int len, int timeout, bool read)
    {
        timer.start();
        mysock.set_blocking(false); // blocking timeouts seem not to work
        int bytes = 0;
        bool first = true;
        do 
        {
            if (first)
                first = false;
            else
                wait_ms(timeout < 100 ? timeout : 100);
            int rc;
            if (read)
                rc = mysock.recv((char*)buffer, len);
            else
                rc = mysock.send((char*)buffer, len);
            if (rc < 0)
            {
                if (rc != NSAPI_ERROR_WOULD_BLOCK)
                {
                    bytes = -1;
                    break;
                }
            } 
            else
                bytes += rc;
        }
        while (bytes < len && timer.read_ms() < timeout);
        timer.stop();
        return bytes;
    }

    /* returns the number of bytes read, which could be 0.
       -1 if there was an error on the socket
    */
    int read(unsigned char* buffer, int len, int timeout)
    {
        return common(buffer, len, timeout, true);
    }

    int write(unsigned char* buffer, int len, int timeout)
    {
        return common(buffer, len, timeout, false);
    }

    int disconnect()
    {
        open = false;
        return mysock.close();
    }

    /*bool is_connected()
    {
        return mysock.is_connected();
    }*/

private:

    bool open;
    TCPSocket mysock;
    EthernetInterface *net;
    Timer timer;

};

#endif
