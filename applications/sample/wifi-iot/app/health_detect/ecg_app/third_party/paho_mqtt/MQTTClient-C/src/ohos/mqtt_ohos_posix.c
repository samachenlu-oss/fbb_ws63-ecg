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
#include "mqtt_ohos.h"
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/syscall.h>

static long gettid(void)
{
    return syscall(SYS_gettid);
}
#define LOGI(fmt, ...) printf("[%ld] " fmt "\n", gettid(), ##__VA_ARGS__)

#if defined(MQTT_TASK)

#define CHECK(expr) (({ \
    int rc = (expr); \
    if (rc != 0) { \
        printf("%s: %d failed: %s!\n", #expr, rc, strerror(rc)); \
    } \
    rc; \
}))

void mqttMutexInit(Mutex* m)
{
    CHECK(pthread_mutex_init(&m->mutex, NULL));
}

int mqttMutexLock(Mutex* m)
{
    return CHECK(pthread_mutex_lock(&m->mutex));
}

int mqttMutexUnlock(Mutex* m)
{
    return CHECK(pthread_mutex_unlock(&m->mutex));
}

void mqttMutexDeinit(Mutex* m)
{
    CHECK(pthread_mutex_destroy(&m->mutex));
}

int ThreadStart(Thread* t, void (*fn)(void*), void* arg)
{
    return CHECK(pthread_create(&t->thread, NULL, (void*(*)(void*))fn, arg));
}

void ThreadJoin(Thread* t)
{
    CHECK(pthread_join(t->thread, NULL));
}

void ThreadYield()
{
#if (defined __linux__)
    CHECK(pthread_yield());
#endif
}

void Sleep(int ms)
{
    usleep(ms * 1000);
}
#endif
