#include <vector>
#include <string>
#include <iostream>
#include <Winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <thread>
#include <mutex>
#include <queue>
#pragma comment(lib, "Ws2_32.lib")

class mychat
{
public:
    struct User
    {
        int IP;
        std::string name;
        bool is_alive;
        bool operator ==(const User& rhs)
        {
            return IP == rhs.IP && name == rhs.name;
        }
        User(int IP_ = 0, std::string name_ = "", bool is_alive_ = true) : IP(IP_), name(name_), is_alive(is_alive_){}
    };
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

        //获取本机ip
        char hostname[255]{};
        gethostname(hostname, sizeof(hostname));
        PHOSTENT hostinfo = gethostbyname(hostname);
        while (*(hostinfo->h_addr_list) != NULL)
        {
            auto nip = *(struct in_addr*)*hostinfo->h_addr_list;
            my_IP = nip.S_un.S_addr;
            hostinfo->h_addr_list++;
        }
        printf("IP = %d.%d.%d.%d\n", my_IP & 255, (my_IP >> 8) & 255, (my_IP >> 16) & 255, (my_IP >> 24) & 255);

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
        recv_addr.sin_addr.s_addr = my_IP;
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

        std::thread recv_t([&]()->void //接受并处理组播信息
            {
                while (true) {
                    char buf[1024]{};
                    sockaddr_in sender{};
                    socklen_t sender_len = sizeof(sender);
                    recvfrom(recv_socket, buf, sizeof(buf), 0, (struct sockaddr*)&sender, &sender_len);
                    //printf("receive msg:%s\n", buf);

                    auto get_vec = [](const std::string& s) //解析收到的报文
                        {
                            int st = 0;
                            std::vector<std::string> vec;
                            for (int i = 0; i < s.size(); i++)
                            {
                                if (s[i] == '\n')
                                {
                                    vec.push_back(s.substr(st, i - st));
                                    st = i + 1;
                                }
                            }
                            return vec;
                        };

                    std::vector<std::string> vmsg = get_vec(std::string(buf));
                    if (!vmsg.size() || vmsg[0] != "mychat") continue; //根据收到的报文类别分类响应
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
                        msg_buf.push({ vmsg[2], vmsg[4] });
                    }
                    else if (vmsg[1] == "alive")
                    {
                        std::lock_guard<std::mutex> lg(latch_user_list);
                        auto it = std::find(user_list.begin(), user_list.end(), User(std::stoi(vmsg[3]), vmsg[2]));
                        if (it == user_list.end())
                        {
                            user_list.push_back({ std::stoi(vmsg[3]), vmsg[2] });
                            new_user.push({ std::stoi(vmsg[3]), vmsg[2] });
                        }
                        else
                        {
                            it->is_alive = true;
                        }
                    }
                    else if (vmsg[1] == "bye")
                    {
                        std::lock_guard<std::mutex> lg(latch_user_list);
                        std::lock_guard<std::mutex> lg2(latch_off_user);
                        auto it = std::find(user_list.begin(), user_list.end(), User(std::stoi(vmsg[3]), vmsg[2]));
                        if (it != user_list.end())
                        {
                            off_user.push(*it);
                            user_list.erase(it);
                        }
                    }
                }
            });
        recv_t.detach();
        std::thread check_user(
            [&]()->void
            {
                while (1)
                {
                    std::unique_lock<std::mutex> lg(latch_user_list);
                    for (auto& user : user_list)
                    {
                        user.is_alive = false;
                        std::string buf = "mychat\ndiscover\n" + name + "\n" + std::to_string(my_IP) + "\n";
                        sockaddr_in recv_add{};
                        recv_addr.sin_family = AF_INET;
                        recv_addr.sin_addr.s_addr = user.IP;
                        recv_addr.sin_port = htons(1901);
                        sendto(send_socket, buf.c_str(), buf.size(), 0, (struct sockaddr*)&recv_addr, sizeof(recv_addr));
                    }
                    lg.unlock();
                    Sleep(3000);
                    std::vector<User> new_list;
                    lg.lock();
                    for (auto& user : user_list)
                    {
                        if (user.is_alive)
                        {
                            new_list.push_back(user);
                        }
                        else
                        {
                            std::lock_guard<std::mutex> lg(latch_off_user);
                            off_user.push(user);
                        }
                    }
                    user_list = std::move(new_list);
                }
            }
        );
        check_user.detach();
        std::string buf;
        // 在线
        buf = "mychat\nalive\n" + name + "\n" + std::to_string(my_IP) + "\n";
        sendto(send_socket, buf.c_str(), buf.size(), 0, (struct sockaddr*)&group_addr, sizeof(group_addr));
        //发现
        buf = "mychat\ndiscover\n" + name + "\n" + std::to_string(my_IP) + "\n";
        sendto(send_socket, buf.c_str(), buf.size(), 0, (struct sockaddr*)&group_addr, sizeof(group_addr));
    }
    ~mychat()
    {
        std::string buf = "mychat\nbye\n" + name + "\n" + std::to_string(my_IP) + "\n";
        sendto(send_socket, buf.c_str(), buf.size(), 0, (struct sockaddr*)&group_addr, sizeof(group_addr));
        setsockopt(recv_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
        WSACleanup();
    }
    void send(const std::string &msg)
    {
        std::string buf = "mychat\ncontext\n" + name + "\n" + std::to_string(my_IP) + "\n" + msg + "\n";
        sendto(send_socket, buf.c_str(), buf.size(), 0, (struct sockaddr*)&group_addr, sizeof(group_addr));
    }
    std::vector<User> get_user_list()
    {
        std::lock_guard<std::mutex> lg(latch_user_list);
        //std::string buf = "mychat\ndiscover\n" + name + "\n" + std::to_string(my_IP) + "\n";
        //user_list.clear();
        //sendto(send_socket, buf.c_str(), buf.size(), 0, (struct sockaddr*)&group_addr, sizeof(group_addr));
        //Sleep(5000);
        return user_list;
    }
    User get_new_user()
    {
        std::lock_guard<std::mutex> lg(latch_new_user);
        User user;
        if (!new_user.empty())
        {
            user = new_user.front();
            new_user.pop();
        }
        return user;
    }
    User get_off_user()
    {
        std::lock_guard<std::mutex> lg(latch_off_user);
        User user;
        if (!off_user.empty())
        {
            user = off_user.front();
            off_user.pop();
        }
        return user;
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
    std::queue<User> new_user, off_user;
    std::vector<User> user_list;
    std::mutex latch_msg_buf, latch_user_list, latch_new_user, latch_off_user;
    SOCKET send_socket, recv_socket;
    sockaddr_in group_addr, recv_addr;
    ip_mreq mreq;
    std::string name;
    int my_IP;
};

void chat_page(mychat &chat)
{
    system("cls");
    std::mutex print;
    std::thread check_new_user(
        [&]()
        {
            while (1)
            {
                std::lock_guard<std::mutex> lg(print);
                auto user = chat.get_new_user();
                if (user.name != "")
                {
                    std::cout << "new user:" << user.name << '\n';
                }
            }
        }
    );
    check_new_user.detach();

    std::thread check_off_user(
        [&]()
        {
            while (1)
            {
                std::lock_guard<std::mutex> lg(print);
                auto user = chat.get_off_user();
                if (user.name != "")
                {
                    std::cout << "offline user:" << user.name << '\n';
                }
            }
        }
    );
    check_off_user.detach();

    std::thread send_msg(
        [&]()
        {
            while (1)
            {
                std::string buf;
                std::getline(std::cin, buf);
                chat.send(buf);
            }
        }
    );
    send_msg.detach();
    
    std::thread recv_msg(
        [&]()
        {
            while (1)
            {
                std::lock_guard<std::mutex> lg(print);
                auto msg = chat.get_msg();
                if (msg.name != "")
                    std::cout << msg.name << ':' << msg.msg << '\n';
                if (msg.msg == "!b")
                {
                    break;
                }
            }
        }
    );
    recv_msg.join();
}

void user_list_page(mychat& chat)
{
    while (1)
    {
        system("cls");
        Sleep(2000);
        //std::lock_guard<std::mutex> lg(print);
        auto list = chat.get_user_list();
        std::cout << "-----\n";
        for (auto& s : list)
        {
            std::cout << s.name << '\n';
        }
        std::cout << "-----\n";
        std::string buf;
        std::cin >> buf;
        if (buf == "!b") break;
    }
}

int main()
{
    system("cls");
    std::string name;
    std::cout << "input username:";
    std::cin >> name;
    mychat chat(name);
    while (1)
    {
        system("cls");
        std::cout << "1 chat\n2 get online user list\n3 exit\n";
        int x;
        std::cin >> x;
        if (x == 1)
        {
            chat_page(chat);
        }
        else if (x == 2)
        {
            user_list_page(chat);
        }
        else if (x == 3)
            break;
    }
    /*std::thread get_list(
        [&]()
        {
            while (1)
            {
                Sleep(2000);
                std::lock_guard<std::mutex> lg(print);
                auto list = chat.get_user_list();
                std::cout << "-----\n";
                for (auto& s : list)
                {
                    std::cout << s.name << '\n';
                }
                std::cout << "-----\n";
            }
        }
    );
    get_list.detach();*/
    return 0;
}

