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

/** By the way, this is a nice bare bones example, easier to expand to whatever non-OS
media you might have */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "transport.h"

/**
This simple low-level implementation assumes a single connection for a single thread. Thus, single static
variables are used for that connection.
On other scenarios, you might want to put all these variables into a structure and index via the 'sock'
parameter, as some functions show in the comments
The blocking rx function is not supported.
If you plan on writing one, take into account that the current implementation of
MQTTPacket_read() has a function pointer for a function call to get the data to a buffer, but no provisions
to know the caller or other indicator (the socket id): int (*getfn)(unsigned char*, int)
*/
static transport_iofunctions_t *io = NULL;
static unsigned char *from = NULL;		// to keep track of data sending
static int howmany;				// ditto


void transport_sendPacketBuffernb_start(int sock, unsigned char* buf, int buflen)
{
	from = buf;			// from[sock] or mystruct[sock].from
	howmany = buflen;		// myhowmany[sock] or mystruct[sock].howmany
}

int transport_sendPacketBuffernb(int sock)
{
transport_iofunctions_t *myio = io;	// io[sock] or mystruct[sock].io
int len;

	/* you should have called open() with a valid pointer to a valid struct and 
	called sendPacketBuffernb_start with a valid buffer, before calling this */
	assert((myio != NULL) && (myio->send != NULL) && (from != NULL));
	if((len = myio->send(from, howmany)) > 0){
		from += len;
		if((howmany -= len) <= 0){
			return TRANSPORT_DONE;
		}
	} else if(len < 0){
		return TRANSPORT_ERROR;
	}
	return TRANSPORT_AGAIN;
}

int transport_sendPacketBuffer(int sock, unsigned char* buf, int buflen)
{
int rc;
int c = 1;
	transport_sendPacketBuffernb_start(sock, buf, buflen);
	while((rc=transport_sendPacketBuffernb(sock)) == TRANSPORT_AGAIN){
		/* this is unlikely to loop forever unless there is a hardware problem */
		if (rc != TRANSPORT_AGAIN) { // 添加退出条件
            break;
        }
	}
	if(rc == TRANSPORT_DONE){
		return buflen;
	}
	return TRANSPORT_ERROR;
}


int transport_getdata(unsigned char* buf, int count)
{
	assert(0);		/* This function is NOT supported, it is just here to tease you */
	return TRANSPORT_ERROR;	/* nah, it is here for similarity with other transport examples */
}

int transport_getdatanb(void *sck, unsigned char* buf, int count)
{
//int sock = *((int *)sck); 		/* sck: pointer to whatever the system may use to identify the transport */
transport_iofunctions_t *myio = io;	// io[sock] or mystruct[sock].io
int len;
	
	/* you should have called open() with a valid pointer to a valid struct before calling this */
	assert((myio != NULL) && (myio->recv != NULL));
	/* this call will return immediately if no bytes, or return whatever outstanding bytes we have,
	 upto count */
	if((len = myio->recv(buf, count)) >= 0)
		return len;
	return TRANSPORT_ERROR;
}

/**
return >=0 for a connection descriptor, <0 for an error code
*/
int transport_open(transport_iofunctions_t *thisio)
{
int idx=0;	// for multiple connections, you might, basically turn myio into myio[MAX_CONNECTIONS],

	//if((idx=assignidx()) >= MAX_CONNECTIONS)	// somehow assign an index,
	//	return TRANSPORT_ERROR;
	io = thisio;					// store myio[idx] = thisio, or mystruct[idx].io = thisio, 
	return idx;					// and return the index used
}

int transport_close(int sock)
{
int rc=TRANSPORT_DONE;

	return rc;
}
