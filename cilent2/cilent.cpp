#include <WINSOCK2.H>
#include <STDIO.H>
#include <iostream>
#include <cstring>
#include <WS2tcpip.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <conio.h>
#include <chrono>
using namespace std;

#pragma comment(lib, "ws2_32.lib")

std::mutex outputMutex; // 保护输出的互斥锁
std::condition_variable cv; // 条件变量
bool inputInProgress = false; // 标记输入状态


void clientCommunication(SOCKET sclient) {
    char recData[255];

    while (true) {
        // 接收服务器消息
        int ret = recv(sclient, recData, sizeof(recData) - 1, 0);
        // 获取当前时间戳
        auto now = std::chrono::system_clock::now();
        std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
        char timeStr[26];
        ctime_s(timeStr, sizeof(timeStr), &timestamp);
        timeStr[strcspn(timeStr, "\n")] = 0; // 移除换行符
        if (ret > 0) {
            recData[ret] = 0x00; // 确保字符串结束
            auto msg = std::string(recData);
            size_t spacePos = msg.find(' ');
            if (spacePos != std::string::npos) {
                int sendPort = std::stoi(msg.substr(0, spacePos));
                std::string recmsg = msg.substr(spacePos + 1);

                {
                    std::unique_lock<std::mutex> lock(outputMutex);
                    // 等待输入进行时挂起输出
                    cv.wait(lock, [] { return !inputInProgress; });
                    if (sendPort == 2046)
                        printf("收到来自服务器消息 [%s]: %s\n", timeStr, recmsg.c_str());
                    else
                    {
                        printf("收到来自客户端 %d 的消息 [%s]: %s\n", sendPort, timeStr, recmsg.c_str());
                    }
                }
            }
        }
    }
}

void inputThread(SOCKET sclient) {
    while (true) {
        if (_kbhit()) { // 检查是否有键盘输入
            char key = _getch(); // 获取按下的键
            if (key == 'Y' || key == 'y') { // 检查是否按下 'Y' 或 'y'
                {
                    inputInProgress = true; // 设置输入进行状态
                    printf("要发送的消息(端口号 消息内容): ");
                }

                string data;
                getline(cin, data); // 使用 getline 获取整行输入
                // 检查格式是否正确
                size_t spacePos = data.find(' ');
                if (spacePos == std::string::npos) {
                    printf("格式错误！请按照 '端口号 消息内容' 输入。\n");
                    inputInProgress = false; // 输入结束
                    continue; // 重新开始循环
                }

                // 获取目标端口号和消息内容
                string portStr = data.substr(0, spacePos);
                string messageContent = data.substr(spacePos + 1);

                // 检查端口号是否合法
                int targetPort;
                try {
                    targetPort = std::stoi(portStr);
                    if (targetPort < 1024 || targetPort > 65535) {
                        throw std::out_of_range("端口号不合法。");
                    }
                }
                catch (...) {
                    printf("端口号无效！请确保输入一个有效的端口号。\n");
                    inputInProgress = false; // 输入结束
                    continue; // 重新开始循环
                }
                const char* sendData = data.c_str(); // string 转 const char*

                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    send(sclient, sendData, strlen(sendData), 0);

                    // 获取当前时间戳
                    auto now = std::chrono::system_clock::now();
                    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
                    char timeStr[26];
                    ctime_s(timeStr, sizeof(timeStr), &timestamp);
                    timeStr[strcspn(timeStr, "\n")] = 0; // 移除换行符

                    // 显示发送消息和时间戳
                    printf("消息发送时间 [%s]\n", timeStr);
                }

                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    inputInProgress = false; // 输入结束
                }
                cv.notify_all(); // 通知输出线程
            }
        }
    }
}


int main() {
    WORD sockVersion = MAKEWORD(2, 2);
    WSADATA data;
    if (WSAStartup(sockVersion, &data) != 0) {
        return 0;
    }

    // 创建客户端套接字
    SOCKET sclient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sclient == INVALID_SOCKET) {
        printf("invalid socket!");
        return 0;
    }

    // 配置服务器地址和端口
    sockaddr_in serAddr;
    serAddr.sin_family = AF_INET;
    serAddr.sin_port = htons(2046);  // 服务器监听的端口
    if (inet_pton(AF_INET, "172.27.63.154", &serAddr.sin_addr) <= 0) {
        printf("Invalid address / Address not supported\n");
        closesocket(sclient);
        return 0;
    }

    // 连接到服务器
    if (connect(sclient, (sockaddr*)&serAddr, sizeof(serAddr)) == SOCKET_ERROR) {
        printf("connect error !");
        closesocket(sclient);
        return 0;
    }

    // 获取本地绑定的端口号
    sockaddr_in clientAddr;
    int addrlen = sizeof(clientAddr);
    getsockname(sclient, (sockaddr*)&clientAddr, &addrlen);
    printf("客户端使用的端口号: %d\n", ntohs(clientAddr.sin_port));

    // 启动接收线程
    std::thread communicationThread(clientCommunication, sclient);
    // 启动输入线程
    std::thread(inputThread, sclient).detach();

    // 等待接收线程结束
    communicationThread.join();

    closesocket(sclient);
    WSACleanup();
    return 0;
}
