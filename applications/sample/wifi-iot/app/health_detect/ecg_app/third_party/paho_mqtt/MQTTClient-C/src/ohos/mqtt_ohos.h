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
#ifndef MQTT_OHOS_H
#define MQTT_OHOS_H

#include <sys/time.h>

typedef struct {
    struct timeval endTime;
} Timer;

void TimerInit(Timer* timer);
char TimerIsExpired(Timer* timer);
void TimerCountdownMS(Timer* timer, unsigned int ms);
void TimerCountdown(Timer* timer, unsigned int seconds);
int TimerLeftMS(Timer* timer);

typedef struct Network {
    int socket;
    int (*mqttread)(struct Network*, unsigned char*, int, int);
    int (*mqttwrite)(struct Network*, unsigned char*, int, int);
} Network;

void NetworkInit(Network* network);
int NetworkConnect(Network* network, char* host, int port);
void NetworkDisconnect(Network* network);

#if defined(MQTT_TASK)

#ifdef OHOS_CMSIS
#include "cmsis_os2.h"

typedef struct {
    osMutexId_t mutex;
} Mutex;

typedef struct {
    osThreadId_t thread;
} Thread;

#ifndef MQTT_TASK_STACK_SIZE
#define MQTT_TASK_STACK_SIZE 4096
#endif

#else // OHOS_CMSIS
#define __USE_GNU
#include <pthread.h>

typedef struct {
    pthread_mutex_t mutex;
} Mutex;

typedef struct {
    pthread_t thread;
} Thread;

#endif // OHOS_CMSIS

void mqttMutexInit(Mutex* mutex);
int mqttMutexLock(Mutex* mutex);
int mqttMutexUnlock(Mutex* mutex);
void mqttMutexDeinit(Mutex* mutex);

int ThreadStart(Thread* thread, void (*fn)(void*), void* arg);
void ThreadJoin(Thread* thread);
void ThreadYield(void);
void Sleep(int ms);
#endif

#endif // MQTT_OHOS_H
