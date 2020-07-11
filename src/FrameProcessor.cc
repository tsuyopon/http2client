#include "FrameProcessor.h"

// 読み込んだフレームに応じて、実行する処理を分岐するメインロジック
int FrameProcessor::readFrameLoop(SSL* ssl, std::string &host){

	int write_headers = 0;    // 初回のHEADERSフレームの書き込みを行ったかどうか判定するフラグ */
    int payload_length = 0;
	unsigned char type = 0;
	unsigned char flags = 0;
	unsigned int streamid = 0;
	unsigned char buf[BUF_SIZE] = {0};
	unsigned char* p = buf;

	while(1){
		type = 0;
		flags = 0;
		memset(buf, 0, BUF_SIZE);

		printf("\n\nreadFrameLoop: loop start\n");
		FrameProcessor::readFramePayload(ssl, p, payload_length, &type, &flags, streamid);
		printf("type=%d, payload_length=%d, flags=%d, streamid=%d\n", type, payload_length, type, streamid);

		switch(static_cast<FrameType>(type)){
			// PING responses SHOULD be given higher priority than any other frame. (sec6.7)
			case FrameType::PING:
				printf("=== PING Frame Recieved ===\n");
				FrameProcessor::readFrameContents(ssl, payload_length, 1);

				// If a PING frame is received with a stream identifier field value other than 0x0, the recipient MUST respond with a connection error (Section 5.4.1) of type PROTOCOL_ERROR. (sec6.7)
				if(streamid != 0 ){
					// TBD
				}

				// Receipt of a PING frame with a length field value other than 8 MUST be treated as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR. (sec6.7)
				if( payload_length != 8 ){
					// TBD
				}

				// RESPONSE PING ACK
				unsigned char* headersframe;
				unsigned char* framepayload;
				int writelen;
				framepayload = createFramePayload(8 /* ping length */, static_cast<char>(FrameType::PING), 0x1 /* ACK */, 0 /*streamid*/);
				headersframe = static_cast<unsigned char*>(std::malloc(sizeof(unsigned char)*(BINARY_FRAME_LENGTH + 8)));
				memcpy(headersframe, framepayload, BINARY_FRAME_LENGTH);
				memset(headersframe+BINARY_FRAME_LENGTH, 0, 8);
				writelen = BINARY_FRAME_LENGTH+8;
				if( FrameProcessor::writeFrame(ssl, headersframe, writelen) < 0 ){
					// FIXME: errorとclose_socketへの対応が必要
					return -1;
				}
				break;
			case FrameType::DATA:
				printf("\n=== DATA Frame Recieved ===\n");
				// If an endpoint receives a SETTINGS frame whose stream identifier field is anything other than 0x0, the endpoint MUST respond with a connection error (Section 5.4.1) of type PROTOCOL_ERROR. (sec6.5)
				if(streamid != 0 ){
					 printf("[ERROR] invalid DATA Frame. PROTOCOL_ERROR");
					// TBD
				}

				// 本文の読み込み
				FrameProcessor::readFrameContents(ssl, payload_length, 1);

				// END_STREAM(この処理は分岐を抜けるので、本文読み込み以降で実施)
				if( flags & 0x1 ){
					 printf("\n*** END_STREAM Recieved\n");
					return 0;
				}

				break;
				
			case FrameType::HEADERS:
				printf("=== HEADERS Frame Recieved ===\n");
				if( flags & 0x1 ) printf("*** END_STREAM Recieved\n");
				if( flags & 0x4 ) printf("*** END_HEADERS Recieved\n");
				if( flags & 0x8 ) printf("*** PADDED Recieved\n");
				if( flags & 0x20 ) printf("*** PRIORITY Recieved\n");
				FrameProcessor::readFrameContents(ssl, payload_length, 1);

				break;

			case FrameType::PRIORITY:
				printf("=== PRIORITY Frame Recieved ===\n");
				FrameProcessor::readFrameContents(ssl, payload_length, 1);
				/* do nothing */
				// フレームだけ読み飛ばす
				break;

			case FrameType::RST_STREAM:
				printf("=== RST_STREAM Frame Recieved ===\n");

				// If a RST_STREAM frame is received with a stream identifier of 0x0, the recipient MUST treat this as a connection error (Section 5.4.1) of type PROTOCOL_ERROR. (sec6.4)
				if( streamid != 0 ){
					printf("[ERROR] invalid RST_STREAM. This message must be PROTOCOL_ERROR. streamid=%d\n", streamid);
					// TBD
				}

				// A RST_STREAM frame with a length other than 4 octets MUST be treated as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR. (sec6.4)
				if( payload_length != 4 ){
					printf("[ERROR] invalid RST_STREAM. FRAME_SIZE_ERROR");
					// TBD
				}

				getFrameContentsIntoBuffer(ssl, payload_length /* 4 */, p);
				unsigned int error_code;
				error_code = ( ( (p[0] & 0xFF) << 24 ) + ((p[1] & 0xFF) << 16 ) + ((p[2] & 0xFF) << 8 ) + ((p[3] & 0xFF) ));
				printf("error_code = %d, message = %s\n", error_code, ErrorMessages[error_code].c_str());

				return static_cast<int>(FrameType::RST_STREAM);

			case FrameType::SETTINGS:
			{
				printf("=== SETTINGS Frame Recieved ===\n");

				getFrameContentsIntoBuffer(ssl, payload_length, p);

				int setting_num;
				setting_num = payload_length/6;
				printf("Recieved %d settings\n", setting_num);

				// SETTINGSフレームで取得した設定値があれば、表示する。
				while(setting_num){
					//printf("%02x %02x %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3], p[4], p[5]);
					unsigned short identifier;
					unsigned int value;
					identifier = ((p[0] & 0xFF) << 8 ) + (p[1] & 0xFF);
					value = ( ( (p[2] & 0xFF) << 24 ) + ((p[3] & 0xFF) << 16 ) + ((p[4] & 0xFF) << 8 ) + ((p[5] & 0xFF) ));
					printf("identifier=%d, value=%d\n", identifier, value);
					p += 6;
					setting_num--;
				}

				// SETTINGSフレームには設定が0なら0octet、設定が1つなら6octet、2つなら12octetと6の倍数の値になることが保証されています。
				// A SETTINGS frame with a length other than a multiple of 6 octets MUST be treated as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.
				if( payload_length % 6 != 0 ){
					printf("=== [ERROR] Invalid Settings Frame Recieved\n");
					return -1;
				}

				// SETTINGSフレームへの応答
				// TODO: Upon receiving the SETTINGS frame, the client is expected to honor any parameters established. (sec3.5)
				printf("=== SETTINGS Frame flags===\n");
				if(sendSettingsAck(ssl) < 0){
					// TBD
				}

				// 初回SETTINGSフレームを受信した後にだけ、HEADERSフレームをリクエストする
				if(write_headers == 0){
					if(sendHeadersFrame(ssl, host) < 0){
						// TBD
					}
					write_headers = 1;
				}

				break;
			}

			case FrameType::PUSH_PROMISE:
				printf("=== PUSH_PROMISE Frame Recieved ===\n");
				FrameProcessor::readFrameContents(ssl, payload_length, 1);
				/* do nothing */
				// フレームだけ読み飛ばす
				break;

			case FrameType::GOAWAY:
			{
				printf("=== GOAWAY Frame Recieved ===\n");
//				FrameProcessor::readFrameContents(ssl, payload_length, 1);
				getFrameContentsIntoBuffer(ssl, payload_length, p);
				unsigned int last_streamid;
				unsigned int error_code;
				// GOAWAYパケットの最初の4byteはlast_stream_id、次の4byteはerror_code、その後additional debug dataが続く
				last_streamid = ( ( (p[0] & 0xFF) << 24 ) + ((p[1] & 0xFF) << 16 ) + ((p[2] & 0xFF) << 8 ) + ((p[3] & 0xFF) ));
				error_code = ( ( (p[4] & 0xFF) << 24 ) + ((p[5] & 0xFF) << 16 ) + ((p[6] & 0xFF) << 8 ) + ((p[7] & 0xFF) ));
				printf("last_streamid = %d, error_code = %d message = %s\n", last_streamid, error_code, ErrorMessages[error_code].c_str());
				return 0;
			}

			case FrameType::WINDOW_UPDATE:
				printf("=== WINDOW_UPDATE Frame Recieved ===\n");
				getFrameContentsIntoBuffer(ssl, payload_length, p);
				unsigned int size_increment;;
				size_increment = ( ( (p[0] & 0xFF) << 24 ) + ((p[1] & 0xFF) << 16 ) + ((p[2] & 0xFF) << 8 ) + ((p[3] & 0xFF) ));
				printf("%02x %02x %02x %02x\n", p[0], p[1], p[2], p[3]);
				printf("window_size_increment = %d\n", size_increment);
				break;

			case FrameType::CONTINUATION:
				printf("=== CONTINUATION Frame Recieved ===\n");
				if(streamid == 0 ){
					printf("Invalid CONTINUATION Frame Recieved\n");
					// TBD
				}
				FrameProcessor::readFrameContents(ssl, payload_length, 1);
				break;

			/* how to handle unknown frame type */
			default:
				printf("=== UNKNOWN Frame Recieved ===\n");
				FrameProcessor::readFrameContents(ssl, payload_length, 1);
				break;

		}
	}
	return 0;  // FIXME
}

//////////////////////////
// WRITE
//////////////////////////
/*
 *  HTTP/2 フレーム仕様: https://tools.ietf.org/html/rfc7540#section-4
 *  length(24) + type(8) + Flags(8) + R(1) + StreamID(31)
 *  (lengthにはフレームペイロード自体の9byteは含まれないことに注意すること)
 */
// FIXME: StreamIDは31なのにintで定義してる
unsigned char* FrameProcessor::createFramePayload (int length, char type, char flags, int streamid){
    unsigned char *frame;
    frame = static_cast<unsigned char*>(std::malloc(BINARY_FRAME_LENGTH));   // BINARY_FRAME_LENGTH = 9 byte

	// Relate: Values greater than 2^14 (16,384) MUST NOT be sent unless the receiver has set a larger value for SETTINGS_MAX_FRAME_SIZE. (sec4.1)

	// 最初の3byte(24bit)はLength
	// int(4byte)なので、1byte先から3byte分取得する)
	frame[0] = ((length>>16)&0xFF);
	frame[1] = ((length>>8)&0xFF);
	frame[2] = ((length)&0xFF);

	// フレームタイプ
	frame[3] = type;

	// Flags
	frame[4] = flags;

	// TODO: Reservedは省略した作りになっている。
	// R: A reserved 1-bit field. The semantics of this bit are undefined, and the bit MUST remain unset (0x0) when sending and MUST be ignored when receiving. (sec4.1)

	// intを各種バイトずつ敷き詰める。memcpyで4byteコピーを指定すると先頭ビットに1が配置されてしまうようでうまくいかない
	frame[5] = ((streamid>>24)&0xFF);
	frame[6] = ((streamid>>16)&0xFF);
	frame[7] = ((streamid>>8)&0xFF);
	frame[8] = ((streamid)&0xFF);

	return frame;
}

//------------------------------------------------------------
// ACKの送信.
// ACKはSettingフレームを受け取った側が送る必要がある.
// ACKはSettingフレームのフラグに0x01を立ててpayloadを空にしたもの.
//
// フレームタイプは「0x04」
// 5バイト目にフラグ0x01を立てます。
//------------------------------------------------------------
// When this bit(ACK) is set, the payload of the SETTINGS frame MUST be empty.  (sec6.5)
int FrameProcessor::sendSettingsAck(SSL *ssl){
	const unsigned char settingframeAck[BINARY_FRAME_LENGTH] = { 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00 };
	printf("=== Start write SETTINGS frame ACK flags\n");
	int writelen = BINARY_FRAME_LENGTH;
	// MEMO: const unsigned char[9]は const_castで一気にunsigned char*へと変換できる。reinterpret_castは不要。
	if( FrameProcessor::writeFrame(ssl, const_cast<unsigned char*>(settingframeAck), writelen) < 0 ){
		// TBD: errorとclose_socketは入れる
		return -1;
	}
	return 0;
}

//------------------------------------------------------------
// HEADERSフレームの送信.
//
// フレームタイプは「0x01」
// このフレームに必要なヘッダがすべて含まれていてこれでストリームを終わらせることを示すために、
// END_STREAM(0x1)とEND_HEADERS(0x4)を有効にします。
// 具体的には5バイト目のフラグに「0x05」を立てます。
// ストリームIDは「0x01」を使います.
//
// ここまででヘッダフレームは「ペイロードの長さ(3バイト), 0x01, 0x05, 0x00, 0x00, 0x00, 0x01」になります.
//
//
// ●HTTP1.1でのセマンティクス
// 　　"GET / HTTP1/1"
// 　　"Host: nghttp2.org
//
// ●HTTP2でのセマンティクス
//      :method GET
//      :path /
//      :scheme https
//      :authority nghttp2.org
//
// 本来HTTP2はHPACKという方法で圧縮します.
// 今回は上記のHTTP2のセマンティクスを圧縮なしで記述します.
//
// 一つのヘッダフィールドの記述例
//
// |0|0|0|0|      0|   // 最初の4ビットは圧縮に関する情報、次の4ビットはヘッダテーブルのインデクス.(今回は圧縮しないのですべて0)
// |0|            7|   // 最初の1bitは圧縮に関する情報(今回は0)、次の7bitはフィールドの長さ
// |:method|           // フィールドをそのままASCIIのオクテットで書く。
// |0|            3|   // 最初の1bitは圧縮に関する情報(今回は0)、次の7bitはフィールドの長さ
// |GET|               // 値をそのままASCIIのオクテットで書く。
//
// 上記が一つのヘッダフィールドの記述例で、ヘッダーフィールドの数だけこれを繰り返す.
//
// See: https://tools.ietf.org/html/rfc7541#appendix-B
//------------------------------------------------------------

// バイト数を変更したら配列数を変更してください、また、SSL_wirteにわたすバイト数も変更してください。
// フレームの先頭3byteはフレームに含まれるバイト数です。全体で74ならば、そこからヘッダフレーム9byteを引いた64(0x00, 0x00, 0x41)を指定します。
//    const unsigned char headersframe[74] = {
//        0x00, 0x00, 0x41, 0x01, 0x05, 0x00, 0x00, 0x00, 0x01,   // ヘッダフレーム(**バイト数を変更したら上位３ビットを変更してください**)
//        0x00,                                                   // 圧縮情報
//        0x07, 0x3a, 0x6d, 0x65, 0x74, 0x68, 0x6f, 0x64,         // 7 :method
//        0x03, 0x47, 0x45, 0x54,                                 // 3 GET
//        0x00,                                                   // 圧縮情報
//        0x05, 0x3a, 0x70, 0x61, 0x74, 0x68,                     // 5 :path
//        0x01, 0x2f,                                             // 1 /
//        0x00,                                                   // 圧縮情報
//        0x07, 0x3a, 0x73, 0x63, 0x68, 0x65, 0x6d, 0x65,         // 7 :scheme
//        0x05, 0x68, 0x74, 0x74, 0x70, 0x73,                     // 5 https
//        0x00,                                                   // 圧縮情報
//        0x0a, 0x3a, 0x61, 0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x74, 0x79,           // 10 :authority
//        0x0f, 0x77, 0x77, 0x77, 0x2e, 0x79, 0x61, 0x68, 0x6f, 0x6f, 0x2e, 0x63, 0x6f, 0x2e, 0x6a, 0x70 };  // 15.www.yahoo.co.jp
int FrameProcessor::sendHeadersFrame(SSL *ssl, std::string host){

    int ret_value, ret_value2, ret_value3, ret_value4, total;
    unsigned char* query1;
    unsigned char* query2;
    unsigned char* query3;
    unsigned char* query4;
    ret_value  = Hpack::createHpack(std::string(":method"),    std::string("GET"), query1);
    ret_value2 = Hpack::createHpack(std::string(":path"),      std::string("/"), query2);
    ret_value3 = Hpack::createHpack(std::string(":scheme"),    std::string("https"), query3);
    ret_value4 = Hpack::createHpack(std::string(":authority"), host, query4);
    total = ret_value + ret_value2 + ret_value3 + ret_value4;

    unsigned char* framepayload;
    framepayload = createFramePayload(total, 0x01, 0x05, 1);  // 第２引数: フレームタイプはHEADER「0x01」、第３引数: END_STREAM(0x1)とEND_HEADERS(0x4)を有効にします、第４引数はstramID

	unsigned char* headersframe;
	headersframe = static_cast<unsigned char*>(std::malloc(sizeof(unsigned char)*(total+BINARY_FRAME_LENGTH)));
	int offset;
	memcpy(headersframe, framepayload, BINARY_FRAME_LENGTH);
	offset = BINARY_FRAME_LENGTH;
	memcpy(headersframe+offset, query1, ret_value);
	offset += ret_value;
	memcpy(headersframe+offset, query2, ret_value2);
	offset += ret_value2;
	memcpy(headersframe+offset, query3, ret_value3);
	offset += ret_value3;
	memcpy(headersframe+offset, query4, ret_value4);

    printf("=== Start write HEADERS frame\n");
	int writelen = total+BINARY_FRAME_LENGTH;
	if( FrameProcessor::writeFrame(ssl, headersframe, writelen) < 0 ){
		return -1;
	}
	return 0;
}

//------------------------------------------------------------
// GOAWAYの送信.
//
// これ以上データを送受信しない場合はGOAWAYフレームを送信します.
// フレームタイプは「0x07」
// ストリームIDは「0x00」(コネクション全体に適用するため)
//------------------------------------------------------------
int FrameProcessor::sendGowayFrame(SSL *ssl){
	printf("\n=== Start write GOAWAY frame\n");
	const char goawayframe[17] = { 0x00, 0x00, 0x08, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
	int writelen = sizeof(goawayframe);
	// MEMO: 一旦constを除去して、その後char*からunsigned char*への変換が必要。(一気にreinterpret_castやconst_castでの変換はできない)
	if( FrameProcessor::writeFrame(ssl, reinterpret_cast<unsigned char *>(const_cast<char*>(goawayframe)), writelen) < 0 ){
		return -1;
	}
	return 0;
}


int FrameProcessor::writeFrame(SSL* &ssl, unsigned char* data, int &data_length){

	int r = 0;
	int ret = 0;
	bool b = false;
    while (1){

        r = SSL_write(ssl, data, data_length);
        ret = SSL_get_error(ssl, r);
        switch (ret){
            case SSL_ERROR_NONE:
                b = true;
                break;
            case SSL_ERROR_WANT_WRITE:
                continue;
            default:
                if (r == -1){
                    printf("Error Occured: Preface SSL_write");
                    return ret;
                }
        }
        if (b) break;
    }
	return ret;
}

//////////////////////////
// READ
//////////////////////////
// フレームペイロード(9byte)を読み込む関数
int FrameProcessor::readFramePayload(SSL* ssl, unsigned char* p, int& payload_length, unsigned char* type, unsigned char* flags, unsigned int& streamid){  // TODO: unsigned intに変更した方がいいかも

	int r = 0;
	int ret = 0;
	bool b = false;
    while (1){

        r = SSL_read(ssl, p, BINARY_FRAME_LENGTH);
        printf("BINARY_FRAME: %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
        ret = SSL_get_error(ssl, r); 
        switch (ret){
            case SSL_ERROR_NONE:
                b = true;
                break;
            case SSL_ERROR_WANT_READ:
                continue;
            default:
                if (r == -1){
                    printf("Error Occured: HEADER_FRAME SSL_read");
                    return ret;  // TODO: 後で綺麗にする
                }
        }
        if (b) break;
    }

	FrameProcessor::to_framedata3byte(p, payload_length);
	FrameProcessor::to_frametype(p, type);
	FrameProcessor::to_frameflags(p, flags);
	FrameProcessor::to_framestreamid(p, streamid);
	printf("streamid = %d\n\n", streamid);

	return ret;
}


// 一部の小さなフレーム用のデータでは、取得したコンテンツを解析して使います。このためのデータを取得します。
// 大きなデータはreadFrameContentsで読み込んでください。
int FrameProcessor::getFrameContentsIntoBuffer(SSL* ssl, int payload_length, unsigned char* retbuf){

	int r = 0;
	int ret = 0;
	unsigned char buf[BUF_SIZE] = { 0 };
	unsigned char* p = buf;
	unsigned int total_read_bytes = 0;

    while (payload_length > 0){

        p = buf;
        r = SSL_read(ssl, p, payload_length);
        ret = SSL_get_error(ssl, r);
        memcpy(retbuf+total_read_bytes, p, r);    // 読み込んんだサイズ分だけコピーする
        switch (ret){
            case SSL_ERROR_NONE:
                break;
            case SSL_ERROR_WANT_READ:
                continue;
            default:
                if (r == -1){
                    printf("Error Occured: payload contents SSL_read");
                    return ret;
                }
        }

        total_read_bytes += r;
        payload_length -= r;
    }
    return ret;
}

// フレームに含まれるコンテンツを読む。主にDATAやHEADERSなどの大きいデータ用途
// 現状skipしかしませんが。。。
int FrameProcessor::readFrameContents(SSL* ssl, int &payload_length, int print){

	int r = 0;
	int ret = 0;
	unsigned char buf[BUF_SIZE] = { 0 };
	unsigned char* p = buf;

    while (payload_length > 0){

        memset(buf, 0x00, BUF_SIZE);
        p = buf;

		// フレームで指定されたペイロード長がREAD_BUF_SIZEよりも小さい場合には、payload_lengthを指定しないと、フレームで指定されたペイロード長を超えたサイズを読み込むことになる。
		if(payload_length > READ_BUF_SIZE) {
			r = SSL_read(ssl, p, READ_BUF_SIZE);
		} else {
			r = SSL_read(ssl, p, payload_length);
		}
        ret = SSL_get_error(ssl, r);
        switch (ret){
            case SSL_ERROR_NONE:
                break;
            case SSL_ERROR_WANT_READ:
                continue;
            default:
                if (r == -1){
                    printf("Error Occured: payload contents SSL_read");
                    return ret;
                }
        }

        payload_length -= r;

        printf("Rest payload_length = %d\n", payload_length);
        if(print) printf("%s", p);
    }
    return ret;
}
unsigned char* FrameProcessor::to_framedata3byte(unsigned char * &p, int &n){
	printf("to_framedata3byte: %02x %02x %02x\n", p[0], p[1], p[2]);
    u_char buf[4] = {0};      // bufを4byte初期化
    memcpy(&(buf[1]), p, 3);  // bufの2byte目から4byteめまでをコピー
    memcpy(&n, buf, 4);       // buf領域を全てコピー
    n = ntohl(n);             // ネットワークバイトオーダーを変換
    p += 3;                   // 読み込んだ3byteをスキップする      // MEMO: 引数を&で参照にしないとポインタの加算が行われない。
    return p;
}

// パケットからフレームタイプを取得する
void FrameProcessor::to_frametype(unsigned char * &p, unsigned char *type){
	printf("to_frametype: %02x\n", p[0]);
	*type = p[0];
	p++;
}

// パケットからフレームタイプのflagsを取得する
void FrameProcessor::to_frameflags(unsigned char * &p, unsigned char *flags){   // to_frametypeと共通
	printf("to_frameflags: %02x\n", p[0]);
	*flags = p[0];
	p++;
}

// パケットからstreamidを取得する
void FrameProcessor::to_framestreamid(unsigned char * &p, unsigned int& streamid){
	streamid = 0;
	// see: How to make int from char[4]? (in C)
	//   https://stackoverflow.com/questions/17602229/how-to-make-int-from-char4-in-c/17602505
	printf("to_framestreamid: %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3]);
	streamid = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3]);
	p += 4;
}