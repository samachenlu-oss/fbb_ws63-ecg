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
#include "ohos_init.h"
#include "wifiiot_at.h"
#include "wifi_device.h"
#include "mqtt_test.h"

#define MQTT_CONN "+MQTT_CONN"
#define MQTT_DISC "+MQTT_DISC"
#define MQTT_TEST "+MQTT_TEST"
#define MQTT_SUB  "+MQTT_SUB"
#define MQTT_PUB  "+MQTT_PUB"
#define AT(cmd)   "AT" cmd

static void PrintArgs(const char* prefix, int argc, const char** argv)
{
    printf("%s: argc = %d, argv =\r\n", prefix, argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d] = %s\r\n", i, argv[i]);
    }
}

static unsigned int MqttConnCmd(int argc, const char **argv)
{
    printf("%s @ %s +%d\r\n", __FUNCTION__, __FILE__, __LINE__);

    PrintArgs(MQTT_CONN, argc, argv);

    if (argc < 2) {
        printf("Usage: %s=host,port[clientId,username,password]\r\n", AT(MQTT_CONN));
        printf("Usage: %s\r\n", AT(MQTT_TEST));
        printf("Usage: %s\r\n", AT(MQTT_DISC));
        printf("Usage: %s=topic,payload\r\n", AT(MQTT_PUB));
        printf("Usage: %s=topic\r\n", AT(MQTT_SUB));
        return 1;
    }

    MqttTestInit();

    const char* host = argv[0];
    unsigned short port = atoi(argv[1]);
    printf("MQTT test with %s %d start.\r\n", host, port);

    const char* clientId = argc > 2 ? argv[2] : "EchoClient";
    const char* username = argc > 3 ? argv[3] : NULL;
    const char* passwords = argc > 4 ? argv[4] : NULL;
    printf("clientId = %s\r\n", clientId);
    printf("username = %s\r\n", username);
    printf("password = %s\r\n", passwords);

    if (MqttTestConnect(host, port, clientId, username, passwords) != 0) {
        printf("MqttTestConnect failed!\r\n");
        return 1;
    }

    printf("OK\r\n");
    return 0;
}

static unsigned int MqttTestCmd(int argc, const char **argv)
{
    printf("%s @ %s +%d\r\n", __FUNCTION__, __FILE__, __LINE__);

    PrintArgs(MQTT_TEST, argc, argv);

    int rc = MqttTestEcho("OHOS/sample/a");
    if (rc != 0) {
        return 1;
    }
    return 0;
}

static unsigned int MqttSubCmd(int argc, const char **argv)
{
    printf("%s @ %s +%d\r\n", __FUNCTION__, __FILE__, __LINE__);

    PrintArgs(MQTT_SUB, argc, argv);

    if (argc < 1) {
        printf("Usage: AT+MQTT_SUB=topic\r\n");
        printf("ERROR\r\n");
        return 1;
    }

    char* topic = (char*) argv[0];
    int rc = MqttTestSubscribe(topic);
    if (rc != 0) {
        return 1;
    }
    printf("OK\r\n");
    return 0;
}


static unsigned int MqttPubCmd(int argc, const char **argv)
{
    printf("%s @ %s +%d\r\n", __FUNCTION__, __FILE__, __LINE__);
    PrintArgs(MQTT_PUB, argc, argv);

    if (argc < 2) {
        printf("Usage: AT+MQTT_PUB=topic,payload\r\n");
        printf("ERROR\r\n");
        return 1;
    }

    char* topic = (char*) argv[0];
    char* payload = (char*) argv[1];
    int rc = MqttTestPublish(topic, payload);
    if (rc != 0) {
        return 1;
    }
    printf("OK\r\n");
    return 0;
}

static unsigned int MqttDiscCmd(int argc, const char **argv)
{
    printf("%s @ %s +%d\r\n", __FUNCTION__, __FILE__, __LINE__);

    PrintArgs(MQTT_DISC, argc, argv);

    MqttTestDisconnect();

    MqttTestDeinit();

    printf("OK\r\n");
    return 0;
}

void MqttAtEntry(void)
{
    static AtCmdTbl cmdTable[] = {
        {
            .atCmdName = MQTT_CONN,
            .atCmdLen = sizeof(MQTT_CONN) - 1,
            .atSetupCmd = MqttConnCmd,
        },
        {
            .atCmdName = MQTT_SUB,
            .atCmdLen = sizeof(MQTT_SUB) - 1,
            .atSetupCmd = MqttSubCmd,
        },
        {
            .atCmdName = MQTT_PUB,
            .atCmdLen = sizeof(MQTT_PUB) - 1,
            .atSetupCmd = MqttPubCmd,
        },
        {
            .atCmdName = MQTT_DISC,
            .atCmdLen = sizeof(MQTT_DISC) - 1,
            .atExeCmd = MqttDiscCmd,
        },
        {
            .atCmdName = MQTT_TEST,
            .atCmdLen = sizeof(MQTT_TEST) - 1,
            .atExeCmd = MqttTestCmd,
        },
    };

    if (AtRegisterCmd(cmdTable, sizeof(cmdTable)/sizeof(cmdTable[0])) != 0) {
        printf("AtRegisterCmd failed!\r\n");
    }
}
SYS_RUN(MqttAtEntry);
