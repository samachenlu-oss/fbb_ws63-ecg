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
#ifndef MQTT_TEST_H
#define MQTT_TEST_H

void MqttTestInit(void);

int MqttTestConnect(const char* host, unsigned short port,
    const char* clientId, const  char* username, const char* password);

int MqttTestSubscribe(char* topic);

int MqttTestPublish(char* topic, char* payload);

int MqttTestEcho(char* topic);

int MqttTestDisconnect(void);

void MqttTestDeinit(void);

#endif  // MQTT_TEST_H
