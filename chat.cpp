#include <vector>
#include <string>
#include <iostream>
#include <Winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <thread>
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

class Client
{
public:
    struct Msg
    {
        //发送人，内容
        std::string name, msg;
    };
    Client(std::string name_)
    {
        name = name_;
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        //初始化发送socket，端口为8888
        send_socket = socket(AF_INET, SOCK_DGRAM, 0);
        memset(&send_addr, 0, sizeof(send_addr));
        send_addr.sin_family = AF_INET;
        send_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        send_addr.sin_port = htons(8888);

        int ret = bind(send_socket, (struct sockaddr*)&send_addr, sizeof(send_addr));
        if (ret == -1)
        {
            perror("Bind send socket failed !");
        }

        //初始化组地址信息 使用239.255.255.250:1901组播
        memset(&group_addr, 0, sizeof(group_addr));
        group_addr.sin_family = AF_INET;
        group_addr.sin_addr.s_addr = inet_addr("239.255.255.250");
        group_addr.sin_port = htons(1901);

        //初始化接收端口 1901
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
        std::thread recv_t(recv_socket);
    }
    ~Client()
    {
        setsockopt(recv_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
        WSACleanup();
    }
    void send(const std::string &msg)
    {
        send_context(msg, name, IP);
    }
    std::vector<std::string> get_user_list()
    {
        send_discover(name, IP);
        return user_list;
    }
    std::vector<Msg> get_history_msg()
    {
        return history_msg;
    }
private:
    std::vector<Msg> history_msg; //记得补个锁
    std::vector<std::string> user_list; //记得补个锁
    SOCKET send_socket, recv_socket;
    sockaddr_in send_addr, group_addr, recv_addr;
    ip_mreq mreq;
    std::string name;
    int IP;
    void send_context(std::string msg, std::string name, int ip)
    {

    }
    void send_alive(std::string name, int ip)
    {

    }
    void send_discover(std::string name, int ip)
    {

    }
    void send_bye(std::string name, int ip)
    {

    }
    void recv_socket()
    {
        char buf[1024];
        int length = 0;
        sockaddr_in sender{};
        socklen_t sender_len = sizeof(sender);
        while (true) {
            memset(buf, 0, sizeof(buf));
            length = recvfrom(recv_socket, buf, sizeof(buf), 0, (struct sockaddr*)&sender, &sender_len);
            buf[length] = '\0';
            //printf("%s %d : %s\n", inet_ntoa(sender.sin_addr), ntohs(sender.sin_port), buf);
            auto get_vec = [](const std::string & s)
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
                send_alive(name, IP);
            }
            else if (vmsg[1] == "context")
            {
                history_msg.emplace_back(vmsg[2], vmsg[4]);
            }
            else if (vmsg[1] == "alive")
            {
                send_alive(name, IP);
            }
            else if (vmsg[1] == "bye")
            {
                send_bye(name, IP);
            }
        }
    }
};

int main()
{
    return 0;
}

