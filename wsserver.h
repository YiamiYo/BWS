#ifndef WSSERVER_H
#define WSSERVER_H

#include <cstdlib>
#include <cstdint>
#include <cstdio>

#include <winsock2.h>

class WSServer
{
private:
    static constexpr size_t MAX_CLIENTS = 10;
    static constexpr size_t BUFFER_SIZE = 1024;
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

public:
    WSServer(int argc = 0, char** argv = nullptr);
    ~WSServer();
    bool tick();
};

#endif // WSSERVER_H
