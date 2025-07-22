#pragma once

// Only include if not already included
#ifndef _WINSOCK2API_
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <functional>

#pragma comment(lib, "ws2_32.lib")

class SimpleWebSocketServer {
public:
    SimpleWebSocketServer();
    ~SimpleWebSocketServer();
    
    bool start(uint16_t port);
    void stop();
    void broadcastData(const std::string& data);
    bool isRunning() const { return running; }
    
private:
    void serverThread();
    void handleClient(SOCKET clientSocket);
    std::string generateWebSocketAccept(const std::string& key);
    void sendWebSocketFrame(SOCKET socket, const std::string& data);
    bool performWebSocketHandshake(SOCKET clientSocket);
    
    SOCKET serverSocket;
    std::thread thread;
    std::vector<SOCKET> clients;
    std::mutex clientsMutex;
    bool running;
    uint16_t port;
}; 