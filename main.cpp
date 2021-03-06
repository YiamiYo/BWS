#include <thread>
#include "wsserver.h"

int main(int argc, char** argv)
{
    WSServer server(argc, argv);
    while(server.tick()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}

