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
#include "MQTTCC3200.h"

unsigned long MilliTimer;

void SysTickIntHandler(void) {
	MilliTimer++;
}

char expired(Timer* timer) {
	long left = timer->end_time - MilliTimer;
	return (left < 0);
}


void countdown_ms(Timer* timer, unsigned int timeout) {
	timer->end_time = MilliTimer + timeout;
}


void countdown(Timer* timer, unsigned int timeout) {
	timer->end_time = MilliTimer + (timeout * 1000);
}


int left_ms(Timer* timer) {
	long left = timer->end_time - MilliTimer;
	return (left < 0) ? 0 : left;
}


void InitTimer(Timer* timer) {
	timer->end_time = 0;
}


int cc3200_read(Network* n, unsigned char* buffer, int len, int timeout_ms) {
	SlTimeval_t timeVal;
	SlFdSet_t fdset;
	int rc = 0;
	int recvLen = 0;

	SL_FD_ZERO(&fdset);
	SL_FD_SET(n->my_socket, &fdset);

	timeVal.tv_sec = 0;
	timeVal.tv_usec = timeout_ms * 1000;
	if (sl_Select(n->my_socket + 1, &fdset, NULL, NULL, &timeVal) == 1) {
		do {
			rc = sl_Recv(n->my_socket, buffer + recvLen, len - recvLen, 0);
			recvLen += rc;
		} while(recvLen < len);
	}
	return recvLen;
}


int cc3200_write(Network* n, unsigned char* buffer, int len, int timeout_ms) {
	SlTimeval_t timeVal;
	SlFdSet_t fdset;
	int rc = 0;
	int readySock;

	SL_FD_ZERO(&fdset);
	SL_FD_SET(n->my_socket, &fdset);

	timeVal.tv_sec = 0;
	timeVal.tv_usec = timeout_ms * 1000;
	do {
		readySock = sl_Select(n->my_socket + 1, NULL, &fdset, NULL, &timeVal);
	} while(readySock != 1);
	rc = sl_Send(n->my_socket, buffer, len, 0);
	return rc;
}


void cc3200_disconnect(Network* n) {
	sl_Close(n->my_socket);
}


void NewNetwork(Network* n) {
	n->my_socket = 0;
	n->mqttread = cc3200_read;
	n->mqttwrite = cc3200_write;
	n->disconnect = cc3200_disconnect;
}

int TLSConnectNetwork(Network *n, char* addr, int port, SlSockSecureFiles_t* certificates, unsigned char sec_method, unsigned int cipher, char server_verify) {
	SlSockAddrIn_t sAddr;
	int addrSize;
	int retVal;
	unsigned long ipAddress;

	retVal = sl_NetAppDnsGetHostByName(addr, strlen(addr), &ipAddress, AF_INET);
	if (retVal < 0) {
		return -1;
	}

	sAddr.sin_family = AF_INET;
	sAddr.sin_port = sl_Htons((unsigned short)port);
	sAddr.sin_addr.s_addr = sl_Htonl(ipAddress);

	addrSize = sizeof(SlSockAddrIn_t);

	n->my_socket = sl_Socket(SL_AF_INET,SL_SOCK_STREAM, SL_SEC_SOCKET);
	if (n->my_socket < 0) {
		return -1;
	}

	SlSockSecureMethod method;
	method.secureMethod = sec_method;
	retVal = sl_SetSockOpt(n->my_socket, SL_SOL_SOCKET, SL_SO_SECMETHOD, &method, sizeof(method));
	if (retVal < 0) {
		return retVal;
	}

	SlSockSecureMask mask;
	mask.secureMask = cipher;
	retVal = sl_SetSockOpt(n->my_socket, SL_SOL_SOCKET, SL_SO_SECURE_MASK, &mask, sizeof(mask));
	if (retVal < 0) {
		return retVal;
	}

	if (certificates != NULL) {
		retVal = sl_SetSockOpt(n->my_socket, SL_SOL_SOCKET, SL_SO_SECURE_FILES, certificates->secureFiles, sizeof(SlSockSecureFiles_t));
		if(retVal < 0)
		{
			return retVal;
		}
	}

	retVal = sl_Connect(n->my_socket, ( SlSockAddr_t *)&sAddr, addrSize);
	if( retVal < 0 ) {
		if (server_verify || retVal != -453) {
			sl_Close(n->my_socket);
			return retVal;
		}
	}

	SysTickIntRegister(SysTickIntHandler);
	SysTickPeriodSet(80000);
	SysTickEnable();

	return retVal;
}

int ConnectNetwork(Network* n, char* addr, int port)
{
	SlSockAddrIn_t sAddr;
	int addrSize;
	int retVal;
	unsigned long ipAddress;

	sl_NetAppDnsGetHostByName(addr, strlen(addr), &ipAddress, AF_INET);

	sAddr.sin_family = AF_INET;
	sAddr.sin_port = sl_Htons((unsigned short)port);
	sAddr.sin_addr.s_addr = sl_Htonl(ipAddress);

	addrSize = sizeof(SlSockAddrIn_t);

	n->my_socket = sl_Socket(SL_AF_INET,SL_SOCK_STREAM, 0);
	if( n->my_socket < 0 ) {
		// error
		return -1;
	}

	retVal = sl_Connect(n->my_socket, ( SlSockAddr_t *)&sAddr, addrSize);
	if( retVal < 0 ) {
		// error
		sl_Close(n->my_socket);
	    return retVal;
	}

	SysTickIntRegister(SysTickIntHandler);
	SysTickPeriodSet(80000);
	SysTickEnable();

	return retVal;
}
