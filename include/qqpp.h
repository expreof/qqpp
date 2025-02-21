#ifndef QQPP_H
#define QQPP_H

#include <string>
#include <vector>
#include <queue>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <variant>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace qqpp
{
    inline namespace Events
    {
        using namespace std::literals;
        class PrivateMessage
        {
        public:
            explicit PrivateMessage(const nlohmann::json &bot_event) : bot_event(bot_event) {}
            auto user_id() const -> int64_t
            {
                return bot_event["user_id"].get<int64_t>();
            }
            auto message() const -> std::string
            {
                return bot_event["message"].dump();
            }

        private:
            nlohmann::json bot_event;
        };
        class GroupMessage
        {
        public:
            explicit GroupMessage(const nlohmann::json &bot_event) : bot_event(bot_event) {}
            auto group_id() const -> int64_t
            {
                return bot_event["group_id"].get<int64_t>();
            }
            auto user_id() const -> int64_t
            {
                return bot_event["user_id"].get<int64_t>();
            }
            auto message() const -> std::string
            {
                return bot_event["message"].dump();
            }

        private:
            nlohmann::json bot_event;
        };
    } // namespace Events
    using EventType = std::variant<std::monostate, PrivateMessage, GroupMessage>;

    template <typename... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };
    template <typename... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    template <typename EventType, typename EventHandler = std::function<void(const EventType &)>>
    class EventProcessor
    {
    public:
        void submit(const EventType &event)
        {
            {
                std::lock_guard lock{queue_mutex};
                event_queue.push(event);
            }
            queue_cv.notify_one();
        }

        void add_event_handler(EventHandler handler)
        {
            event_handlers.push_back(handler);
        }

        template <typename T>
        void on(auto &&handler)
        {
            add_event_handler(
                [&handler](const EventType &event)
                {
                    std::visit(
                        overloaded{
                            [handler](const T &event)
                            { handler(event); },
                            [](auto &&event) {}},
                        event);
                });
        }

        void run()
        {
            event_thread = std::jthread{
                [this](std::stop_token stop_token)
                { this->handle_events(stop_token); }};
        }

        void stop()
        {
            event_thread.request_stop();
            queue_cv.notify_all();
        }

    private:
        void handle_events(std::stop_token stop_token)
        {
            while (!stop_token.stop_requested())
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [this, &stop_token]
                              { return !event_queue.empty() || stop_token.stop_requested(); });

                while (!event_queue.empty())
                {
                    auto event = event_queue.front();
                    event_queue.pop();
                    lock.unlock();

                    process_event(event);

                    lock.lock();
                }
            }
        }

        void process_event(const EventType &event)
        {
            for (const auto &handler : event_handlers)
                handler(event);
        }

    private:
        std::vector<EventHandler> event_handlers;
        std::queue<EventType> event_queue;
        std::mutex queue_mutex;
        std::condition_variable queue_cv;
        std::jthread event_thread;
    };

    class Session
    {
    public:
        Session(const std::string &host_port, const std::string &server_host, int server_port)
            : client(host_port), server_host(server_host), server_port(server_port)
        {
            server.Post("/",
                        [this](const httplib::Request &req, httplib::Response &res)
                        {
                            nlohmann::json bot_event = nlohmann::json::parse(req.body);
                            auto message = [&bot_event]() -> EventType
                            {
                                if (bot_event.contains("post_type") == false)
                                    return std::monostate{};
                                if (bot_event["post_type"] != "message"sv)
                                    return std::monostate{};
                                if (bot_event.contains("message_type") == false)
                                    return std::monostate{};
                                if (bot_event["message_type"] == "private"sv)
                                    return PrivateMessage{bot_event};
                                if (bot_event["message_type"] == "group"sv)
                                    return GroupMessage{bot_event};
                                return std::monostate{};
                            }();

                            event_processor.submit(message);
                        });
            server.Get("/stop",
                       [this](const httplib::Request &req, httplib::Response &res)
                       {
                           event_processor.stop();
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

        // 注册事件处理器
        template <typename EventT>
        void on(auto &&handler)
        {
            event_processor.on<EventT>(std::forward<decltype(handler)>(handler));
        }

    private:
        httplib::Client client;
        httplib::Server server;
        std::string server_host;
        int server_port;

        EventProcessor<EventType> event_processor;
    };

    void Session::run()
    {
        event_processor.run();
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

} // namespace qqpp

#endif // QQPP_H