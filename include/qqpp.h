#ifndef QQPP_H
#define QQPP_H

#include <string>
#include <vector>
#include <queue>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace qqpp
{
    class Session
    {
    public:
        using EventHandler = std::function<void(const nlohmann::json &)>;

        Session(const std::string &host_port, const std::string &server_host, int server_port)
            : client(host_port), server_host(server_host), server_port(server_port)
        {
            server.Post("/",
                        [this](const httplib::Request &req, httplib::Response &res)
                        {
                            nlohmann::json bot_event = nlohmann::json::parse(req.body);
                            {
                                std::lock_guard lock{queue_mutex};
                                event_queue.push(bot_event);
                            }
                            queue_cv.notify_one();
                        });
            server.Get("/stop",
                       [this](const httplib::Request &req, httplib::Response &res)
                       {
                           event_thread.request_stop();
                           queue_cv.notify_all();
                           server.stop();
                       });
        }

        // session will listen on server_host_port
        void run();

        // 向用户发送私聊消息
        // user_id: 用户QQ号
        // message: 消息内容
        // 返回值: httplib::Result
        auto send_private_message(const std::string &user_id, const std::string &message) -> httplib::Result;

        // 向群发送消息
        // group_id: 群号
        // message: 消息内容
        // 返回值: httplib::Result
        auto send_group_message(const std::string &group_id, const std::string &message) -> httplib::Result;

        // 添加事件处理函数
        // handler: 事件处理函数
        void add_event_handler(EventHandler handler);

    private:
        void handle_events(std::stop_token stoken);
        void process_event(const nlohmann::json &event);

        httplib::Client client;
        httplib::Server server;
        std::string server_host;
        int server_port;
        std::queue<nlohmann::json> event_queue;
        std::vector<EventHandler> event_handlers;
        std::mutex queue_mutex;
        std::condition_variable queue_cv;
        std::jthread event_thread;
    };

    void Session::run()
    {
        event_thread = std::jthread{
            [this](std::stop_token stop_token)
            { this->handle_events(stop_token); }};
        server.listen(server_host, server_port);
    }

    auto Session::send_private_message(const std::string &user_id, const std::string &message) -> httplib::Result
    {
        nlohmann::json msg_array = nlohmann::json::parse(message);
        nlohmann::json payload = {
            {"user_id", user_id},
            {"message", msg_array}};
        return client.Post("/send_private_msg", payload.dump(), "application/json");
    }

    auto Session::send_group_message(const std::string &group_id, const std::string &message) -> httplib::Result
    {
        nlohmann::json msg_array = nlohmann::json::parse(message);
        nlohmann::json payload = {
            {"group_id", group_id},
            {"message", msg_array}};
        return client.Post("/send_group_msg", payload.dump(), "application/json");
    }

    void Session::add_event_handler(EventHandler handler)
    {
        event_handlers.push_back(handler);
    }

    void Session::handle_events(std::stop_token stoken)
    {
        while (!stoken.stop_requested())
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this, &stoken]
                          { return !event_queue.empty() || stoken.stop_requested(); });

            while (!event_queue.empty())
            {
                nlohmann::json event = event_queue.front();
                event_queue.pop();
                lock.unlock();

                process_event(event);

                lock.lock();
            }
        }
    }

    void Session::process_event(const nlohmann::json &event)
    {
        for (const auto &handler : event_handlers)
        {
            handler(event);
        }
    }
} // namespace qqpp

#endif // QQPP_H