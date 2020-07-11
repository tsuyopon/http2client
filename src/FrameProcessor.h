#ifndef FRAME_PROCESSOR_H
#define FRAME_PROCESSOR_H

#include<string>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "Definitions.h"
#include "ErrorCodes.h"
#include "Hpack.h"

//#define READ_BUF_SIZE 4096
//#define BUF_SIZE 4097
#define READ_BUF_SIZE 32768
#define BUF_SIZE 32768

enum class FrameType {
	DATA = 0x0,
	HEADERS = 0x1,
	PRIORITY = 0x2,
	RST_STREAM = 0x3,
	SETTINGS = 0x4,
	PUSH_PROMISE = 0x5,
	PING = 0x6,
	GOAWAY = 0x7,
	WINDOW_UPDATE = 0x8,
	CONTINUATION = 0x9
};

class FrameProcessor {
public:
	static int readFrameLoop(SSL* ssl, std::string &host);
	// write
	static unsigned char* createFramePayload (int length, char type, char flags, int streamid);
	static int sendSettingsAck(SSL *ssl);
	static int sendHeadersFrame(SSL *ssl, std::string host);
	static int sendGowayFrame(SSL *ssl);
	static int writeFrame(SSL* &ssl, unsigned char* data, int &data_length);
	// read
	static int readFramePayload(SSL* ssl, unsigned char* p, int& payload_length, unsigned char* type, unsigned char* flags, unsigned int& streamid);
	static int getFrameContentsIntoBuffer(SSL* ssl, int payload_length, unsigned char* retbuf);
	static int readFrameContents(SSL* ssl, int &payload_length, int print);
	static unsigned char* to_framedata3byte(unsigned char * &p, int &n);
	static void to_frametype(unsigned char * &p, unsigned char *type);
	static void to_frameflags(unsigned char * &p, unsigned char *flags);
	static void to_framestreamid(unsigned char * &p, unsigned int& streamid);
};

#endif  // FRAME_PROCESSOR_H
