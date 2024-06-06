﻿#include <vector>
#include <string>
#include <iostream>
#include <Winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <thread>
#include <mutex>
#include <queue>
#pragma comment(lib, "Ws2_32.lib")

/*
constexpr auto SERVERPORT = 1900;
constexpr auto SSDP_GROUP = "239.255.255.250";
const char buff[] =
"M-SEARCH * HTTP/1.1\r\n"
"HOST: 239.255.255.250:1900\r\n"
"MAN: \"ssdp:discover\"\r\n"
"MX: 3\r\n"
"ST: ssdp:all\r\n"
"\r\n";
*/
//自定义报文类型
/*
* mychat
* discover
* name
* IP
*/

/*
* mychat
* context
* name
* IP
* text
*/

/*
* mychat
* alive
* name
* IP
*/

/*
* mychat
* bye
* name
* IP
*/

class mychat
{
public:
    struct Msg
    {
        //发送人，内容
        std::string name, msg;
        Msg(std::string name_ = "", std::string msg_ = "") :name(name_), msg(msg_) {}
    };
    mychat(std::string name_ = "unname")
    {
        name = name_;
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        send_socket = socket(AF_INET, SOCK_DGRAM, 0);

        //239.255.255.250:1901组播
        memset(&group_addr, 0, sizeof(group_addr));
        group_addr.sin_family = AF_INET;
        group_addr.sin_addr.s_addr = inet_addr("239.255.255.250");
        group_addr.sin_port = htons(1901);

        //接收端口 1901
        recv_socket = socket(AF_INET, SOCK_DGRAM, 0);
        memset(&recv_addr, 0, sizeof(recv_addr));
        recv_addr.sin_family = AF_INET;
        recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        recv_addr.sin_port = htons(1901);

        int ret = bind(recv_socket, (struct sockaddr*)&recv_addr, sizeof(recv_addr));
        if (ret == -1)
        {
            perror("bind failed!");
        }
        
        //加入组播
        memset(&mreq, 0, sizeof(mreq));
        mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        ret = setsockopt(recv_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
        
    }
    ~Client()
    {
        setsockopt(recv_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
        WSACleanup();
    }
    void start_receive()
    {
        auto receive = [&]()->void
            {
                while (true) {
                    char buf[1024]{};
                    sockaddr_in sender{};
                    socklen_t sender_len = sizeof(sender);
                    recvfrom(recv_socket, buf, sizeof(buf), 0, (struct sockaddr*)&sender, &sender_len);
                    auto get_vec = [](const std::string& s)
                        {
                            int st = 0;
                            std::vector<std::string> vec;
                            for (int i = 0; i < s.size(); i++)
                            {
                                if (s[i] == '\n')
                                {
                                    vec.emplace_back(s.substr(st, i - st));
                                    st = i + 1;
                                }
                            }
                            return vec;
                        };
                    std::vector<std::string> vmsg = get_vec(std::string(buf));
                    if (!vmsg.size() || vmsg[0] != "mychat") continue;
                    if (vmsg[1] == "discover")
                    {
                        std::string buf = "mychat\nalive\n" + name + "\n" + std::to_string(my_IP) + "\n";
                        sockaddr_in recv_add{};
                        recv_addr.sin_family = AF_INET;
                        recv_addr.sin_addr.s_addr = std::stoi(vmsg[3]);
                        recv_addr.sin_port = htons(1901);
                        int length = sendto(send_socket, buf.c_str(), buf.size(), 0, (struct sockaddr*)&recv_addr, sizeof(recv_addr));
                    }
                    else if (vmsg[1] == "context")
                    {
                        std::lock_guard<std::mutex> lg(latch_msg_buf);
                        msg_buf.emplace(vmsg[2], vmsg[4]);
                    }
                    else if (vmsg[1] == "alive")
                    {
                        std::lock_guard<std::mutex> lg(latch_user_list);
                        auto it = std::find(user_list.begin(), user_list.end(), vmsg[2]);
                        if (it == user_list.end())
                        {
                            user_list.emplace_back(vmsg[2]);
                        }
                    }
                    else if (vmsg[1] == "bye")
                    {
                        std::lock_guard<std::mutex> lg(latch_user_list);
                        auto it = std::find(user_list.begin(), user_list.end(), vmsg[2]);
                        if (it != user_list.end())
                        {
                            user_list.erase(it);
                        }
                    }
                }
            };
        std::thread recv_t(receive);
    }
    void send(const std::string &msg)
    {
        std::string buf = "mychat\ncontext\n" + name + "\n" + std::to_string(my_IP) + "\n" + msg + "\n";
        sendto(send_socket, buf.c_str(), buf.size(), 0, (struct sockaddr*)&group_addr, sizeof(group_addr));
    }
    std::vector<std::string> get_user_list()
    {
        std::lock_guard<std::mutex> lg(latch_msg_buf);
        std::string buf = "mychat\ndiscover\n" + name + "\n" + std::to_string(my_IP) + "\n";
        user_list.clear();
        sendto(send_socket, buf.c_str(), buf.size(), 0, (struct sockaddr*)&group_addr, sizeof(group_addr));
        Sleep(2);
        return user_list;
    }
    Msg get_msg()
    {
        std::lock_guard<std::mutex> lg(latch_msg_buf);
        Msg msg;
        if (!msg_buf.empty())
        {
            msg = msg_buf.front();
            msg_buf.pop();
        }
        return msg;
    }
private:
    std::queue<Msg> msg_buf;
    std::vector<std::string> user_list;
    std::mutex latch_msg_buf, latch_user_list;
    SOCKET send_socket, recv_socket;
    sockaddr_in group_addr, recv_addr;
    ip_mreq mreq;
    std::string name;
    int my_IP;
};

int main()
{
    return 0;
}

