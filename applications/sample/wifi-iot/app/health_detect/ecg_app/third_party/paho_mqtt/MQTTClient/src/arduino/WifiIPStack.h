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

#ifndef ARDUINOWIFIIPSTACK_H
#define ARDUINOWIFIIPSTACK_H

#include <WiFi.h>

class WifiIPStack 
{
public:    
    WifiIPStack()
    {
        //WiFi.begin();              // Use DHCP
        iface.setTimeout(1000);    // 1 second Timeout 
    }
    
    int connect(char* hostname, int port)
    {
        return iface.connect(hostname, port);
    }

    int connect(uint32_t hostname, int port)
    {
        return iface.connect(hostname, port);
    }

    int read(char* buffer, int len, int timeout)
    {
        iface.setTimeout(timeout);
        while(!iface.available());
        return iface.readBytes(buffer, len);
    }
    
    int write(char* buffer, int len, int timeout)
    {
        iface.setTimeout(timeout);  
        return iface.write((uint8_t*)buffer, len);
    }
    
    int disconnect()
    {
        iface.stop();
        return 0;
    }
    
private:

    WiFiClient iface;
    
};

#endif



