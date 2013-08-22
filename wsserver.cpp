#include "wsserver.h"

WSServer::WSServer(int argc, char** argv) : state(State::None), port(DEFAULT_PORT), host((char*)DEFAULT_HOST) {
    for(int i = 1; i < argc; i++)
    {
        if(argv[i][0] == '-')
        {
            switch(argv[i][1])
            {
            case 'p':
                if(argv[i][2]) port = atoi(argv[i] + 2);
                else port = atoi(argv[++i]);
                break;
            case 'h':
                if(argv[i][2]) host = argv[i] + 2;
                else host = argv[++i];
                break;
            default:
                printf("Usage: %s [-p PORT] [-h HOST]\n\n", argv[0]);
                return;
            }
        }
    }

    if(WSAStartup(MAKEWORD(2, 2), &wsd)) printf("WSAStartup failed!\n");
    else {
        state = State::WSA;
        if((sListen = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) == INVALID_SOCKET) printf("Creating listening socket failed!\n");
        else {
            state = State::Socket;
            u_long iMode = 1;
            if(ioctlsocket(sListen, FIONBIO, &iMode)) printf("Could not set listening socket to non-blocking!\n");
            if((local.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE) printf("Wrong IPv4 address: %s\n", host);
            else {
                local.sin_family = AF_INET;
                local.sin_port = htons(port);
                if(bind(sListen, (sockaddr*)&local, sizeof(local))) printf("Could not bind listening socket!\n");
                else {
                    state = State::Bind;
                    if(listen(sListen, MAX_CLIENTS)) printf("Could not listen to bound socket!\n");
                    else {
                        state = State::Listen;
                        return;
                    }
                }
            }
        }
    }
}

WSServer::~WSServer() {
    switch(state) {
        case State::WSA:
            WSACleanup();
        case State::Socket:
            closesocket(sListen);
        case State::Bind:;
        case State::Listen:;
    }
    state = State::None;
}

void clientMain(SOCKET sClient, sockaddr_in client) {

}

bool WSServer::tick() {
    if(state != State::Listen) return false;

    static SOCKET sClient;
    static sockaddr_in aClient;
    static int len = sizeof(aClient);
    static u_long iMode = 1;
    static std::list<client>::iterator cit;

    if((sClient = accept(sListen, (sockaddr*)&aClient, &len)) != INVALID_SOCKET) {
        printf("Client connected(%s:%d)!\n", inet_ntoa(aClient.sin_addr), ntohs(aClient.sin_port));
        if(ioctlsocket(sClient, FIONBIO, &iMode)) printf("Could not set client socket to non-blocking!\n");
        client temp(sClient, aClient);
        clients.push_back(std::move(temp));
    }

    for(cit = clients.begin(); cit != clients.end();) {
        if(!cit->tick()) {
            printf("Client disconnected!\n");
            clients.erase(cit++);
        }
        else cit++;
    }
    return true;
}

