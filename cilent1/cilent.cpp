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

std::mutex outputMutex; // ��������Ļ�����
std::condition_variable cv; // ��������
bool inputInProgress = false; // �������״̬


void clientCommunication(SOCKET sclient) {
    char recData[255];

    while (true) {
        // ���շ�������Ϣ
        int ret = recv(sclient, recData, sizeof(recData) - 1, 0);
        // ��ȡ��ǰʱ���
        auto now = std::chrono::system_clock::now();
        std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
        char timeStr[26];
        ctime_s(timeStr, sizeof(timeStr), &timestamp);
        timeStr[strcspn(timeStr, "\n")] = 0; // �Ƴ����з�
        if (ret > 0) {
            recData[ret] = 0x00; // ȷ���ַ�������
            auto msg = std::string(recData);
            size_t spacePos = msg.find(' ');
            if (spacePos != std::string::npos) {
                int sendPort = std::stoi(msg.substr(0, spacePos));
                std::string recmsg = msg.substr(spacePos + 1);

                {
                    std::unique_lock<std::mutex> lock(outputMutex);
                    // �ȴ��������ʱ�������
                    cv.wait(lock, [] { return !inputInProgress; });
                    if (sendPort == 2046)
                        printf("�յ����Է�������Ϣ [%s]: %s\n", timeStr, recmsg.c_str());
                    else
                    {
                        printf("�յ����Կͻ��� %d ����Ϣ [%s]: %s\n", sendPort, timeStr, recmsg.c_str());
                    }
                }
            }
        }
    }
}

void inputThread(SOCKET sclient) {
    while (true) {
        if (_kbhit()) { // ����Ƿ��м�������
            char key = _getch(); // ��ȡ���µļ�
            if (key == 'Y' || key == 'y') { // ����Ƿ��� 'Y' �� 'y'
                {
                    inputInProgress = true; // �����������״̬
                    printf("Ҫ���͵���Ϣ(�˿ں� ��Ϣ����): ");
                }

                string data;
                getline(cin, data); // ʹ�� getline ��ȡ��������
                // ����ʽ�Ƿ���ȷ
                size_t spacePos = data.find(' ');
                if (spacePos == std::string::npos) {
                    printf("��ʽ�����밴�� '�˿ں� ��Ϣ����' ���롣\n");
                    inputInProgress = false; // �������
                    continue; // ���¿�ʼѭ��
                }

                // ��ȡĿ��˿ںź���Ϣ����
                string portStr = data.substr(0, spacePos);
                string messageContent = data.substr(spacePos + 1);

                // ���˿ں��Ƿ�Ϸ�
                int targetPort;
                try {
                    targetPort = std::stoi(portStr);
                    if (targetPort < 1024 || targetPort > 65535) {
                        throw std::out_of_range("�˿ںŲ��Ϸ���");
                    }
                }
                catch (...) {
                    printf("�˿ں���Ч����ȷ������һ����Ч�Ķ˿ںš�\n");
                    inputInProgress = false; // �������
                    continue; // ���¿�ʼѭ��
                }
                const char* sendData = data.c_str(); // string ת const char*

                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    send(sclient, sendData, strlen(sendData), 0);

                    // ��ȡ��ǰʱ���
                    auto now = std::chrono::system_clock::now();
                    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
                    char timeStr[26];
                    ctime_s(timeStr, sizeof(timeStr), &timestamp);
                    timeStr[strcspn(timeStr, "\n")] = 0; // �Ƴ����з�

                    // ��ʾ������Ϣ��ʱ���
                    printf("��Ϣ����ʱ�� [%s]\n", timeStr);
                }

                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    inputInProgress = false; // �������
                }
                cv.notify_all(); // ֪ͨ����߳�
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

    // �����ͻ����׽���
    SOCKET sclient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sclient == INVALID_SOCKET) {
        printf("invalid socket!");
        return 0;
    }

    // ���÷�������ַ�Ͷ˿�
    sockaddr_in serAddr;
    serAddr.sin_family = AF_INET;
    serAddr.sin_port = htons(2046);  // �����������Ķ˿�
    if (inet_pton(AF_INET, "172.27.63.154", &serAddr.sin_addr) <= 0) {
        printf("Invalid address / Address not supported\n");
        closesocket(sclient);
        return 0;
    }

    // ���ӵ�������
    if (connect(sclient, (sockaddr*)&serAddr, sizeof(serAddr)) == SOCKET_ERROR) {
        printf("connect error !");
        closesocket(sclient);
        return 0;
    }

    // ��ȡ���ذ󶨵Ķ˿ں�
    sockaddr_in clientAddr;
    int addrlen = sizeof(clientAddr);
    getsockname(sclient, (sockaddr*)&clientAddr, &addrlen);
    printf("�ͻ���ʹ�õĶ˿ں�: %d\n", ntohs(clientAddr.sin_port));

    // ���������߳�
    std::thread communicationThread(clientCommunication, sclient);
    // ���������߳�
    std::thread(inputThread, sclient).detach();

    // �ȴ������߳̽���
    communicationThread.join();

    closesocket(sclient);
    WSACleanup();
    return 0;
}
