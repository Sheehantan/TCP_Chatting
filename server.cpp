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

std::mutex connMutex; // ���������б�Ļ�����
std::vector<std::tuple<SOCKET, int, std::string>> clientSockets; // �洢���ӵĿͻ����׽��֡��˿ںź� IP ��ַ
std::mutex inputMutex; // ��������Ļ�����
std::mutex outputMutex; // ��������Ļ�����
std::condition_variable inputCondition; // ������������
std::queue<std::pair<SOCKET, std::string>> messageQueue; // ��Ϣ����

bool inputInProgress = false; // �������״̬

void handleClient(SOCKET sClient) {
    char revData[255];
    while (true) {
        int ret = recv(sClient, revData, sizeof(revData) - 1, 0); // ȷ����Խ��
        if (ret > 0) {
            revData[ret] = 0x00; // ��ֹ�ַ���
            {
                std::lock_guard<std::mutex> lock(inputMutex);
                messageQueue.push({ sClient, std::string(revData) });
            }
            inputCondition.notify_one(); // ֪ͨ�����߳�������Ϣ
        }
        else {
            closesocket(sClient);
            std::string clientInfo;
            int clientPort = 0; // ������������˿ں�
            {
                std::lock_guard<std::mutex> lock(connMutex);
                auto it = std::remove_if(clientSockets.begin(), clientSockets.end(),
                    [sClient, &clientInfo, &clientPort](const auto& tuple) {
                        if (std::get<0>(tuple) == sClient) {
                            clientInfo = std::get<2>(tuple);
                            clientPort = std::get<1>(tuple); // ��ȡ�˿ں�
                            return true;
                        }
                        return false;
                    });
                clientSockets.erase(it, clientSockets.end());
            }
            printf("�ͻ��� %s:%d �Ͽ�����\n", clientInfo.c_str(), clientPort); // ���IP�Ͷ˿ں�
            break;
        }
    }
}


void inputThread() {
    while (true) {
        if (_kbhit()) { // ����Ƿ��м�������
            char key = _getch(); // ��ȡ���µļ�
            if (key == 'Y' || key == 'y') { // ����Ƿ��� 'Y' �� 'y'
                std::lock_guard<std::mutex> lock(outputMutex);
                inputInProgress = true; // �����������״̬

                while (true) {
                    std::string input;
                    std::cout << "������Ŀ��ͻ��˵Ķ˿ں�: ";
                    std::getline(std::cin, input);

                    try {
                        int targetPort = std::stoi(input); // ת��Ϊ����
                        if (targetPort < 1024 || targetPort > 65535) {
                            throw std::out_of_range("�˿ںű����� 1024 �� 65535 ֮�䡣");
                        }

                        char sendData[255];
                        printf("������Ҫ���͸��ͻ��� %d ����Ϣ: ", targetPort);
                        std::cin.getline(sendData, sizeof(sendData)); // ʹ�� getline ��ȡ��������
                        sendData[strcspn(sendData, "\n")] = 0; // ȥ�����з�
                        // �� sendData ǰ��Ӷ˿ںźͿո�
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
                            // ��ȡ��ǰʱ���
                            auto now = std::chrono::system_clock::now();
                            std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
                            char timeStr[26];
                            ctime_s(timeStr, sizeof(timeStr), &timestamp);
                            timeStr[strcspn(timeStr, "\n")] = 0; // �Ƴ����з�
                            // ��ʾ������Ϣ��ʱ���
                            printf("��Ϣ����ʱ�� [%s]\n", timeStr);
                        }
                        else {
                            printf("û���ҵ��˿ں�Ϊ %d �Ŀͻ���\n", targetPort);
                        }
                        break; // ������Ч���˳�ѭ��
                    }
                    catch (std::invalid_argument&) {
                        std::cout << "��Ч�����룬������һ���Ϸ��Ķ˿ںš�" << std::endl;
                    }
                    catch (std::out_of_range&) {
                        std::cout << "�˿ںű����� 1024 �� 65535 ֮�䡣" << std::endl;
                    }
                }

                inputInProgress = false; // �������
                inputCondition.notify_all(); // ֪ͨ�����ָ̻߳����
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

            SOCKET clientSocket = frontMessage.first; // ��ȡ�ͻ����׽���
            std::string msg = frontMessage.second; // ��ȡ��Ϣ

            sockaddr_in clientAddr;
            int addrlen = sizeof(clientAddr);
            if (getpeername(clientSocket, (sockaddr*)&clientAddr, &addrlen) == SOCKET_ERROR) {
                printf("��ȡ�ͻ��˵�ַʧ��\n");
                continue; // ����ʧ�ܣ�������һ����Ϣ
            }

            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);

            // ������Ϣ����ʽ: Ŀ��˿ں� ��Ϣ����
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
                    // ��ȡ��ǰʱ���
                    auto now = std::chrono::system_clock::now();
                    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
                    char timeStr[26];
                    ctime_s(timeStr, sizeof(timeStr), &timestamp);
                    timeStr[strcspn(timeStr, "\n")] = 0; // �Ƴ����з�
                    if (targetPort != 2046) {
                        {
                            std::lock_guard<std::mutex> outputLock(outputMutex);
                            printf("�յ��ͻ��� %d ���͸��ͻ��� %d ����Ϣ [%s]: %s\n", ntohs(clientAddr.sin_port), targetPort, timeStr, messageToSend.c_str());
                        }
                        send(targetSocket, msg.c_str(), msg.size(), 0);
                    }
                    else {
                        {
                            std::lock_guard<std::mutex> outputLock(outputMutex);
                            printf("�������յ��ͻ��� %d ���͵���Ϣ [%s]: %s\n", ntohs(clientAddr.sin_port), timeStr, messageToSend.c_str());
                        }
                    }
                }
                else {
                    printf("û���ҵ��˿ں�Ϊ %d �Ŀͻ���\n", targetPort);
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
        printf("�ȴ�����...\n");
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
        printf("���ܵ�һ�����ӣ�%s : %d \n", clientIP, ntohs(remoteAddr.sin_port));

        {
            std::lock_guard<std::mutex> lock(connMutex);
            clientSockets.emplace_back(sClient, ntohs(remoteAddr.sin_port), clientIP); // �洢�׽��֡��˿ںź� IP ��ַ
        }

        std::thread(handleClient, sClient).detach(); // ��������ͻ����߳�
    }

    closesocket(slisten);
    WSACleanup();
    return 0;
}
