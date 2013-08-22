#ifndef WSSERVER_H
#define WSSERVER_H

#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <map>
#include <list>
#include <vector>
#include <string>
#include <regex>

#include <winsock2.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

inline uint64_t ntohll(uint64_t num) {
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return num;
    #elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        union {
            uint64_t v64;
            uint8_t v8[8];
        } val;
        val.v64 = num;
        uint8_t t;
        t = val.v8[0]; val.v8[0] = val.v8[7]; val.v8[7] = t;
        t = val.v8[1]; val.v8[1] = val.v8[6]; val.v8[6] = t;
        t = val.v8[2]; val.v8[2] = val.v8[5]; val.v8[5] = t;
        t = val.v8[3]; val.v8[3] = val.v8[4]; val.v8[4] = t;
        return val.v64;
    #else
        #error "Invalid byte order"
    #endif
}

inline void base64_encode(std::string& out) {
    BIO* bmem;
    BIO* b64;
    BUF_MEM* bptr;

    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, out.c_str(), out.size());
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    out.resize(bptr->length);
    memcpy(&out[0], bptr->data, bptr->length);

    BIO_free_all(b64);
}


enum WSOperation {
    Continue = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA
};

union WSHeader {
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        #warning "WSHeader::data not tested for big endian"
        struct { // TODO: test for big endian
            uint8_t Length : 7;
            bool Mask : 1;

            WSOperation Opcode : 4;
            bool RSV3 : 1;
            bool RSV2 : 1;
            bool RSV1 : 1;
            bool FIN : 1;
        } data;
    #elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        struct Data {
            uint8_t Opcode  : 4;
            uint8_t RSV3    : 1;
            uint8_t RSV2    : 1;
            uint8_t RSV1    : 1;
            uint8_t FIN     : 1;

            uint8_t Length  : 7;
            uint8_t Mask    : 1;
        } data;
    #else
        #error "Invalid byte order"
    #endif
    uint8_t bytes[2];
};

struct HTTPHeader {
    std::string type;
    std::string path;
    std::string protocol;
    struct {
        size_t major;
        size_t minor;
    } version;
    std::map<std::string, std::string> fields;

    HTTPHeader(std::string str) {
        static constexpr const char* space = " \t\r\n";
        static constexpr const char* number = "0123456789";
        size_t pos_start, pos_end;

        if((pos_start = str.find_first_not_of(space)) == std::string::npos) return;
        if((pos_end = str.find_first_of(space, pos_start)) == std::string::npos) return;
        type = str.substr(pos_start, pos_end - pos_start);

        if((pos_start = str.find_first_not_of(space, pos_end)) == std::string::npos) return;
        if((pos_end = str.find_first_of(space, pos_start)) == std::string::npos) return;
        path = str.substr(pos_start, pos_end - pos_start);

        if((pos_start = str.find_first_not_of(space, pos_end)) == std::string::npos) return;
        if((pos_end = str.find_first_of('/', pos_start)) == std::string::npos) return;
        protocol = str.substr(pos_start, pos_end - pos_start);

        if((pos_start = str.find_first_of(number, pos_end)) == std::string::npos) return;
        if((pos_end = str.find_first_not_of(number, pos_start)) == std::string::npos) return;
        version.major = std::stoul(str.substr(pos_start, pos_end - pos_start));

        if((pos_start = str.find_first_of(number, pos_end)) == std::string::npos) return;
        if((pos_end = str.find_first_not_of(number, pos_start)) == std::string::npos) return;
        version.minor = std::stoul(str.substr(pos_start, pos_end - pos_start));

        std::string field;
        while((pos_start = str.find_first_not_of(space, pos_end)) != std::string::npos) {
            if((pos_end = str.find_first_of(" :", pos_start)) == std::string::npos) break;
            field = str.substr(pos_start, pos_end - pos_start);

            if((pos_start = str.find_first_not_of(" :", pos_end)) == std::string::npos) break;
            if((pos_end = str.find("\r\n", pos_start)) == std::string::npos) break;
            fields[field] = str.substr(pos_start, pos_end - pos_start);
        }
    }
};

struct HTTPRequest {
    static constexpr size_t BUFFER_SIZE = 1024;
    std::string buffer;
    size_t totalRead;
    size_t headLength;
    bool complete;

    HTTPRequest() : buffer(BUFFER_SIZE, 0), totalRead(0), headLength(0), complete(false) { }
    ~HTTPRequest() {}
    void update(size_t newBytes) { // 0 < newBytes
        totalRead += newBytes;
        buffer.resize(totalRead + BUFFER_SIZE);
        if(!complete) {
            size_t pos = buffer.find("\r\n\r\n");
            if(pos != std::string::npos) {
                headLength = pos + 4;
                complete = true;
            }
        }
    }
};

// 1. nextRead = 2; // For standard frame header
// 2. nextRead = 2|6|8|12; // For mask (4 bytes) and additional payload len (2|8 bytes), if any
// 3. nextRead = dataLength; // For frame data
struct WSFrame {
    std::vector<uint8_t> buffer;
    size_t totalRead;
    size_t nextRead;
    uint8_t headLength;
    size_t dataLength;

    WSFrame() : buffer(14), totalRead(0), nextRead(2), headLength(0), dataLength(0) {}
    ~WSFrame() {}
    void update(size_t newBytes) { // 0 < newBytes <= nextRead
        totalRead += newBytes;
        nextRead -= newBytes;
        if(!nextRead) {
            WSHeader* h = reinterpret_cast<WSHeader*>(buffer.data());
            if(!headLength) { // We got the standard frame header, next read the rest info from header if any
                headLength = 2;
                if(h->data.Mask) headLength += 4;
                if(h->data.Length == 0x7E) headLength += 2;
                else if(h->data.Length == 0x7F) headLength += 8;
                nextRead = headLength - 2;
                if(!nextRead) { // Frame has no mask and is less than 126 bytes long, next read the data
                    dataLength = h->data.Length;
                    nextRead = dataLength;
                    buffer.resize(headLength + dataLength);
                }
            }
            else if(!dataLength) { // We got everything we need from header, next read the data
                if(headLength == 6) {
                    dataLength = h->data.Length;
                } else if(headLength == 4 || headLength == 8) {
                    dataLength = ntohs(*(reinterpret_cast<uint16_t*>(buffer.data() + 2)));
                } else {
                    dataLength = ntohll(*(reinterpret_cast<uint64_t*>(buffer.data() + 2)));
                }
                buffer.resize(headLength + dataLength);
                nextRead = dataLength;
            } // else we have a complete frame! (nextRead == 0)
        }
    }
    std::string getText() {
        WSHeader* h = reinterpret_cast<WSHeader*>(buffer.data());
        if(h->data.Opcode == WSOperation::Text) {
            std::string str(dataLength + 1, 0);
            memcpy(&str[0], &buffer[headLength], dataLength);
            if(h->data.Mask) {
                uint8_t* mask = reinterpret_cast<uint8_t*>(buffer.data() + headLength - 4);
                for(size_t i = 0; i < dataLength; i++) {
                    str[i] ^= mask[i % 4];
                }
            }
            return str;
        } else return std::string("Not a Text Frame!");
    }
};

class client {
private:
    enum class Protocol : uint8_t {
        HTTP,
        WS
    } protocol;
    void* data;

    SOCKET sock;
    sockaddr_in addr;

public:
    client(SOCKET i_sock, sockaddr_in i_addr) : protocol(Protocol::HTTP), data(new HTTPRequest()), sock(i_sock), addr(i_addr) {}
    client(client&& old) : protocol(old.protocol), data(old.data), sock(old.sock), addr(old.addr) { old.data = nullptr; }
    ~client() { free(); }
    void free() {
        if(data) {
            switch(protocol) {
                case Protocol::HTTP:
                    delete reinterpret_cast<HTTPRequest*>(data);
                    break;
                case Protocol::WS:
                    delete reinterpret_cast<WSFrame*>(data);
                    break;
            }
            data = nullptr;
        }
    }
    bool tick() {
        static int ret;

        switch(protocol) {
            case Protocol::HTTP:
            {
                    HTTPRequest* d = reinterpret_cast<HTTPRequest*>(data);
                    ret = recv(sock, &d->buffer[d->totalRead], d->BUFFER_SIZE, 0);
                    if(!ret) {
                        closesocket(sock);
                        return false;
                    } else if(ret != SOCKET_ERROR) {
                        d->update(ret);
                        if(d->complete) {
                            HTTPHeader h(d->buffer.substr(0, d->headLength));
                            if(h.type == "GET" && h.fields["Upgrade"] == "websocket") {
                                std::string accept = h.fields["Sec-WebSocket-Key"];
                                accept.append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
                                SHA1((unsigned char*)accept.c_str(), accept.size(), (unsigned char*)accept.data());
                                accept.resize(20);
                                base64_encode(accept);

                                std::stringstream ss;
                                ss<<"HTTP/1.1 101 Web Socket Protocol Handshake\r\n";
                                ss<<"Upgrade: WebSocket\r\n";
                                ss<<"Connection: Upgrade\r\n";
                                ss<<"Sec-WebSocket-Accept: "<<accept<<"\r\n";
                                ss<<"Server: BWS\r\n";
                                ss<<"Access-Control-Allow-Origin: http://localhost:8080\r\n";
                                //ss<<"Access-Control-Allow-Credentials: true\r\n";
                                //ss<<"Access-Control-Allow-Headers: content-type\r\n";
                                //ss<<"Access-Control-Allow-Headers: authorization\r\n";
                                //ss<<"Access-Control-Allow-Headers: x-websocket-extensions\r\n";
                                ss<<"Access-Control-Allow-Headers: x-websocket-version\r\n";
                                ss<<"Access-Control-Allow-Headers: x-websocket-protocol\r\n";
                                ss<<"\r\n";
                                std::string response = ss.str();
                                send(sock, response.c_str(), response.size(), 0);

                                free();
                                protocol = Protocol::WS;
                                data = new WSFrame();
                            } else {
                                std::stringstream ss;
                                ss<<"HTTP/"<<h.version.major<<"."<<h.version.minor<<" 501 Not Supported\r\n\r\n";
                                std::string response = ss.str();
                                send(sock, response.c_str(), response.size(), 0);
                                printf("HTTP not supported...\n");
                                closesocket(sock);
                                return false;
                            }
                        }
                    }
                    break;
            }
            case Protocol::WS:
            {
                WSFrame* d = reinterpret_cast<WSFrame*>(data);
                ret = recv(sock, (char*)&d->buffer[d->totalRead], d->nextRead, 0);
                if(!ret) {
                    closesocket(sock);
                    return false;
                } else if(ret != SOCKET_ERROR) {
                    d->update(ret);
                    if(!d->nextRead) {
                        printf("%s\n", d->getText().c_str());
                        char buffer[] = { (char)0x81, (char)0x07, 'Y', 'i', 'a', 'm', 'i', 'Y', 'o' };
                        //char c[] = { (char)0x88, (char)0x00};
                        ret = send(sock, buffer, sizeof(buffer), 0);
                        //ret = send(sock, c, sizeof(c), 0);
                        //closesocket(sock);
                        //return false;
                        //ret = send(sock, (char*)&d->buffer[0], d->totalRead, 0);
                        printf("send: %d\n", ret);
                    }
                }
                break;
            }
        }
        return true;
    }
};

class WSServer
{
private:
    static constexpr size_t MAX_CLIENTS = 10;
    static constexpr uint16_t DEFAULT_PORT = 80;
    static constexpr const char* DEFAULT_HOST = "127.0.0.1";

    enum class State : uint8_t {
        None,
        WSA,
        Socket,
        Bind,
        Listen
    } state;

    WSADATA wsd;
    SOCKET sListen;
    sockaddr_in local;
    uint16_t port;
    char* host;
    std::list<client> clients;

public:
    WSServer(int argc = 0, char** argv = nullptr);
    ~WSServer();
    bool tick();
};

#endif // WSSERVER_H
