#include "simple_websocket.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")

// Simple Base64 encoding
std::string base64_encode(const std::vector<uint8_t>& input) {
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int val = 0, valb = -6;
    for (uint8_t c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (result.size() % 4) result.push_back('=');
    return result;
}

// SHA1 hash using Windows Crypto API
std::vector<uint8_t> sha1_hash(const std::string& input) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::vector<uint8_t> result(20);
    DWORD hashLen = 20;

    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
            if (CryptHashData(hHash, (BYTE*)input.c_str(), input.length(), 0)) {
                CryptGetHashParam(hHash, HP_HASHVAL, result.data(), &hashLen, 0);
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    return result;
}

SimpleWebSocketServer::SimpleWebSocketServer() : serverSocket(INVALID_SOCKET), running(false), port(0) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "WSAStartup failed" << std::endl;
    }
}

SimpleWebSocketServer::~SimpleWebSocketServer() {
    stop();
    WSACleanup();
}

bool SimpleWebSocketServer::start(uint16_t port) {
    if (running) {
        return false;
    }

    this->port = port;
    
    // Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cout << "Socket creation failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) < 0) {
        std::cout << "Setsockopt failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        return false;
    }

    // Bind socket
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cout << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        return false;
    }

    // Listen
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cout << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        return false;
    }

    running = true;
    thread = std::thread(&SimpleWebSocketServer::serverThread, this);
    
    std::cout << "Simple WebSocket server started on port " << port << std::endl;
    return true;
}

void SimpleWebSocketServer::stop() {
    if (!running) {
        return;
    }

    running = false;
    
    if (serverSocket != INVALID_SOCKET) {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        for (SOCKET client : clients) {
            closesocket(client);
        }
        clients.clear();
    }

    if (thread.joinable()) {
        thread.join();
    }

    std::cout << "Simple WebSocket server stopped" << std::endl;
}

void SimpleWebSocketServer::serverThread() {
    while (running) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            if (running) {
                std::cout << "Accept failed: " << WSAGetLastError() << std::endl;
            }
            continue;
        }

        // Handle client in separate thread
        std::thread clientThread(&SimpleWebSocketServer::handleClient, this, clientSocket);
        clientThread.detach();
    }
}

void SimpleWebSocketServer::handleClient(SOCKET clientSocket) {
    if (performWebSocketHandshake(clientSocket)) {
        std::lock_guard<std::mutex> lock(clientsMutex);
        clients.push_back(clientSocket);
        std::cout << "WebSocket client connected. Total clients: " << clients.size() << std::endl;
    } else {
        closesocket(clientSocket);
    }
}

bool SimpleWebSocketServer::performWebSocketHandshake(SOCKET clientSocket) {
    char buffer[4096];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived <= 0) {
        return false;
    }
    buffer[bytesReceived] = '\0';

    std::string request(buffer);
    
    // Find WebSocket key
    std::string keyHeader = "Sec-WebSocket-Key: ";
    size_t keyPos = request.find(keyHeader);
    if (keyPos == std::string::npos) {
        return false;
    }

    keyPos += keyHeader.length();
    size_t keyEnd = request.find("\r\n", keyPos);
    if (keyEnd == std::string::npos) {
        return false;
    }

    std::string key = request.substr(keyPos, keyEnd - keyPos);
    std::string acceptKey = generateWebSocketAccept(key);

    // Send handshake response
    std::string response = 
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
        "\r\n";

    if (send(clientSocket, response.c_str(), response.length(), 0) == SOCKET_ERROR) {
        return false;
    }

    return true;
}

std::string SimpleWebSocketServer::generateWebSocketAccept(const std::string& key) {
    // WebSocket magic string
    std::string combined = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    
    // Generate SHA1 hash
    auto hash = sha1_hash(combined);
    
    // Encode as Base64
    return base64_encode(hash);
}

void SimpleWebSocketServer::sendWebSocketFrame(SOCKET socket, const std::string& data) {
    std::vector<uint8_t> frame;
    
    // WebSocket frame format (simplified)
    frame.push_back(0x81); // FIN + text frame
    
    if (data.length() < 126) {
        frame.push_back(static_cast<uint8_t>(data.length()));
    } else if (data.length() < 65536) {
        frame.push_back(126);
        frame.push_back((data.length() >> 8) & 0xFF);
        frame.push_back(data.length() & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((data.length() >> (i * 8)) & 0xFF);
        }
    }
    
    // Add payload
    frame.insert(frame.end(), data.begin(), data.end());
    
    send(socket, reinterpret_cast<const char*>(frame.data()), frame.size(), 0);
}

void SimpleWebSocketServer::broadcastData(const std::string& data) {
    if (!running) {
        return;
    }

    std::lock_guard<std::mutex> lock(clientsMutex);
    
    for (auto it = clients.begin(); it != clients.end();) {
        try {
            sendWebSocketFrame(*it, data);
            ++it;
        } catch (...) {
            closesocket(*it);
            it = clients.erase(it);
        }
    }
} 