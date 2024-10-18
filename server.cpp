#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <algorithm>
#include <iostream>
#include <string>
#include <conio.h>
#include <tuple>
#pragma comment(lib, "ws2_32.lib")

std::mutex connMutex; // 保护连接列表的互斥锁
std::vector<std::tuple<SOCKET, int, std::string>> clientSockets; // 存储连接的客户端套接字、端口号和 IP 地址
std::mutex inputMutex; // 保护输入的互斥锁
std::mutex outputMutex; // 保护输出的互斥锁
std::condition_variable inputCondition; // 输入条件变量
std::queue<std::pair<SOCKET, std::string>> messageQueue; // 消息队列

bool inputInProgress = false; // 标记输入状态

void handleClient(SOCKET sClient) {
    char revData[255];
    while (true) {
        int ret = recv(sClient, revData, sizeof(revData) - 1, 0); // 确保不越界
        if (ret > 0) {
            revData[ret] = 0x00; // 终止字符串
            {
                std::lock_guard<std::mutex> lock(inputMutex);
                messageQueue.push({ sClient, std::string(revData) });
            }
            inputCondition.notify_one(); // 通知处理线程有新消息
        }
        else {
            closesocket(sClient);
            std::string clientInfo;
            int clientPort = 0; // 新增变量保存端口号
            {
                std::lock_guard<std::mutex> lock(connMutex);
                auto it = std::remove_if(clientSockets.begin(), clientSockets.end(),
                    [sClient, &clientInfo, &clientPort](const auto& tuple) {
                        if (std::get<0>(tuple) == sClient) {
                            clientInfo = std::get<2>(tuple);
                            clientPort = std::get<1>(tuple); // 获取端口号
                            return true;
                        }
                        return false;
                    });
                clientSockets.erase(it, clientSockets.end());
            }
            printf("客户端 %s:%d 断开连接\n", clientInfo.c_str(), clientPort); // 输出IP和端口号
            break;
        }
    }
}


void inputThread() {
    while (true) {
        if (_kbhit()) { // 检查是否有键盘输入
            char key = _getch(); // 获取按下的键
            if (key == 'Y' || key == 'y') { // 检查是否按下 'Y' 或 'y'
                std::lock_guard<std::mutex> lock(outputMutex);
                inputInProgress = true; // 设置输入进行状态

                while (true) {
                    std::string input;
                    std::cout << "请输入目标客户端的端口号: ";
                    std::getline(std::cin, input);

                    try {
                        int targetPort = std::stoi(input); // 转换为整数
                        if (targetPort < 1024 || targetPort > 65535) {
                            throw std::out_of_range("端口号必须在 1024 到 65535 之间。");
                        }

                        char sendData[255];
                        printf("请输入要发送给客户端 %d 的消息: ", targetPort);
                        std::cin.getline(sendData, sizeof(sendData)); // 使用 getline 获取整行输入
                        sendData[strcspn(sendData, "\n")] = 0; // 去除换行符
                        // 在 sendData 前添加端口号和空格
                        std::string messageToSend = "2046 " + std::string(sendData);

                        SOCKET targetSocket = INVALID_SOCKET;
                        {
                            std::lock_guard<std::mutex> lock(connMutex);
                            for (const auto& tuple : clientSockets) {
                                if (std::get<1>(tuple) == targetPort) {
                                    targetSocket = std::get<0>(tuple);
                                    break;
                                }
                            }
                        }

                        if (targetSocket != INVALID_SOCKET) {
                            send(targetSocket, messageToSend.c_str(), messageToSend.length(), 0);
                            // 获取当前时间戳
                            auto now = std::chrono::system_clock::now();
                            std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
                            char timeStr[26];
                            ctime_s(timeStr, sizeof(timeStr), &timestamp);
                            timeStr[strcspn(timeStr, "\n")] = 0; // 移除换行符
                            // 显示发送消息和时间戳
                            printf("消息发送时间 [%s]\n", timeStr);
                        }
                        else {
                            printf("没有找到端口号为 %d 的客户端\n", targetPort);
                        }
                        break; // 输入有效，退出循环
                    }
                    catch (std::invalid_argument&) {
                        std::cout << "无效的输入，请输入一个合法的端口号。" << std::endl;
                    }
                    catch (std::out_of_range&) {
                        std::cout << "端口号必须在 1024 到 65535 之间。" << std::endl;
                    }
                }

                inputInProgress = false; // 输入结束
                inputCondition.notify_all(); // 通知处理线程恢复输出
            }
        }
    }
}

void messageProcessingThread() {
    while (true) {
        std::unique_lock<std::mutex> lock(inputMutex);
        inputCondition.wait(lock, [] { return !messageQueue.empty(); });

        while (!messageQueue.empty()) {
            std::pair<SOCKET, std::string> frontMessage = messageQueue.front();
            messageQueue.pop();

            SOCKET clientSocket = frontMessage.first; // 获取客户端套接字
            std::string msg = frontMessage.second; // 获取消息

            sockaddr_in clientAddr;
            int addrlen = sizeof(clientAddr);
            if (getpeername(clientSocket, (sockaddr*)&clientAddr, &addrlen) == SOCKET_ERROR) {
                printf("获取客户端地址失败\n");
                continue; // 处理失败，继续下一条消息
            }

            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);

            // 解析消息，格式: 目标端口号 消息内容
            size_t spacePos = msg.find(' ');
            if (spacePos != std::string::npos) {
                int targetPort = std::stoi(msg.substr(0, spacePos));
                std::string messageToSend = msg.substr(spacePos + 1);

                SOCKET targetSocket = INVALID_SOCKET;
                {
                    std::lock_guard<std::mutex> lock(connMutex);
                    for (const auto& tuple : clientSockets) {
                        if (std::get<1>(tuple) == targetPort) {
                            targetSocket = std::get<0>(tuple);
                            break;
                        }
                    }
                }
                if (targetSocket != INVALID_SOCKET) {
                    // 获取当前时间戳
                    auto now = std::chrono::system_clock::now();
                    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
                    char timeStr[26];
                    ctime_s(timeStr, sizeof(timeStr), &timestamp);
                    timeStr[strcspn(timeStr, "\n")] = 0; // 移除换行符
                    if (targetPort != 2046) {
                        {
                            std::lock_guard<std::mutex> outputLock(outputMutex);
                            printf("收到客户端 %d 发送给客户端 %d 的消息 [%s]: %s\n", ntohs(clientAddr.sin_port), targetPort, timeStr, messageToSend.c_str());
                        }
                        send(targetSocket, msg.c_str(), msg.size(), 0);
                    }
                    else {
                        {
                            std::lock_guard<std::mutex> outputLock(outputMutex);
                            printf("服务器收到客户端 %d 发送的消息 [%s]: %s\n", ntohs(clientAddr.sin_port), timeStr, messageToSend.c_str());
                        }
                    }
                }
                else {
                    printf("没有找到端口号为 %d 的客户端\n", targetPort);
                }
            }
        }
    }
}

int main() {
    WORD sockVersion = MAKEWORD(2, 2);
    WSADATA wsaData;
    if (WSAStartup(sockVersion, &wsaData) != 0) {
        return 0;
    }

    SOCKET slisten = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (slisten == INVALID_SOCKET) {
        printf("socket error !\n");
        return 0;
    }

    sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(2046);
    sin.sin_addr.S_un.S_addr = INADDR_ANY;
    if (bind(slisten, (LPSOCKADDR)&sin, sizeof(sin)) == SOCKET_ERROR) {
        printf("bind error !\n");
        return 0;
    }

    if (listen(slisten, 5) == SOCKET_ERROR) {
        printf("listen error !\n");
        return 0;
    }

    std::thread(inputThread).detach();
    std::thread(messageProcessingThread).detach();

    while (true) {
        printf("等待连接...\n");
        SOCKET sClient = accept(slisten, nullptr, nullptr);
        if (sClient == INVALID_SOCKET) {
            printf("accept error !\n");
            continue;
        }

        sockaddr_in remoteAddr;
        int nAddrlen = sizeof(remoteAddr);
        getpeername(sClient, (sockaddr*)&remoteAddr, &nAddrlen);
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &remoteAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        printf("接受到一个连接：%s : %d \n", clientIP, ntohs(remoteAddr.sin_port));

        {
            std::lock_guard<std::mutex> lock(connMutex);
            clientSockets.emplace_back(sClient, ntohs(remoteAddr.sin_port), clientIP); // 存储套接字、端口号和 IP 地址
        }

        std::thread(handleClient, sClient).detach(); // 启动处理客户端线程
    }

    closesocket(slisten);
    WSACleanup();
    return 0;
}
