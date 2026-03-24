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
 * Description: Provides adc port \n
 *
 * History: \n
 * 2022-09-16， Create file. \n
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mqtt_test.h"

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage %s host [port] [clientId] [username] [passwords]\n", argv[0]);
        return 1;
    }
    const char* host = argv[1];
    unsigned short port = argc > 2 ? atoi(argv[2]) : 1883;
    printf("MQTT test with %s %d start.\r\n", host, port);

    char* clientId = argc > 3 ? argv[3] : "EchoClient";
    char* username = argc > 4 ? argv[4] : NULL;
    char* passwords = argc > 5 ? argv[5] : NULL;
    printf("clientId = %s\n", clientId);
    printf("username = %s\n", username);
    printf("passwords = %s\n", passwords);

    MqttTestInit();

    if (MqttTestConnect(host, port, clientId, username, passwords) != 0) {
        printf("MqttTestConnect failed!\n");
        return 1;
    }

    int rc = MqttTestEcho("OHOS/sample/a");
    if (rc != 0) {
        printf("MqttTestEcho ERROR\r\n");
        return 1;
    }

    MqttTestDisconnect();
    MqttTestDeinit();
    return 0;
}