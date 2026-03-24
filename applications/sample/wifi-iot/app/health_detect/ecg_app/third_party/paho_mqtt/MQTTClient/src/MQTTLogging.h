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

#if !defined(MQTT_LOGGING_H)
#define MQTT_LOGGING_H

#define STREAM      stdout
#if !defined(DEBUG)
#define DEBUG(...)    \
    {\
    fprintf(STREAM, "DEBUG:   %s L#%d ", __PRETTY_FUNCTION__, __LINE__);  \
    fprintf(STREAM, ##__VA_ARGS__); \
    fflush(STREAM); \
    }
#endif
#if !defined(LOG)
#define LOG(...)    \
    {\
    fprintf(STREAM, "LOG:   %s L#%d ", __PRETTY_FUNCTION__, __LINE__);  \
    fprintf(STREAM, ##__VA_ARGS__); \
    fflush(STREAM); \
    }
#endif
#if !defined(WARN)
#define WARN(...)   \
    { \
    fprintf(STREAM, "WARN:  %s L#%d ", __PRETTY_FUNCTION__, __LINE__);  \
    fprintf(STREAM, ##__VA_ARGS__); \
    fflush(STREAM); \
    }
#endif 
#if !defined(ERROR)
#define ERROR(...)  \
    { \
    fprintf(STREAM, "ERROR: %s L#%d ", __PRETTY_FUNCTION__, __LINE__); \
    fprintf(STREAM, ##__VA_ARGS__); \
    fflush(STREAM); \
    exit(1); \
    }
#endif

#endif
