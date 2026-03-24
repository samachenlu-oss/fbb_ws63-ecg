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

#if !defined(COUNTDOWN_H)
#define COUNTDOWN_H

class Countdown
{
public:
    Countdown()
    {  
		interval_end_ms = 0L;
    }
    
    Countdown(int ms)
    {
        countdown_ms(ms);   
    }
    
    bool expired()
    {
        return (interval_end_ms > 0L) && (millis() >= interval_end_ms);
    }
    
    void countdown_ms(unsigned long ms)  
    {
        interval_end_ms = millis() + ms;
    }
    
    void countdown(int seconds)
    {
        countdown_ms((unsigned long)seconds * 1000L);
    }
    
    int left_ms()
    {
        return interval_end_ms - millis();
    }
    
private:
    unsigned long interval_end_ms; 
};

#endif
