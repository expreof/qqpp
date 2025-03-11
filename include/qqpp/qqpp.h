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
#include <type_traits>

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

    inline namespace Message
    {
        class TextMessage
        {
        public:
            TextMessage() = default;
            explicit TextMessage(const std::string &text) : text{text} {}
            explicit TextMessage(std::string &&text) : text{std::move(text)} {}

            auto to_json() const -> nlohmann::json
            {
                nlohmann::json msg;
                msg["type"] = "text";
                msg["data"]["text"] = text;
                return msg;
            }
            template <typename T = nlohmann::json>
            auto serialize_to() const -> T
            {
                if constexpr (std::is_same_v<T, nlohmann::json>)
                    return to_json();
                else
                    return T{};
            }
            auto get_content() const -> std::string
            {
                return text;
            }

        private:
            std::string text;
        };

        class ImageMessage
        {
        public:
            ImageMessage() = default;
            explicit ImageMessage(const std::string &url) : url{url} {}
            explicit ImageMessage(std::string &&url) : url{std::move(url)} {}

            auto to_json() const -> nlohmann::json
            {
                nlohmann::json msg;
                msg["type"] = "image";
                msg["data"]["file"] = url;
                return msg;
            }

            template <typename T = nlohmann::json>
            auto serialize_to() const -> T
            {
                if constexpr (std::is_same_v<T, nlohmann::json>)
                    return to_json();
                else
                    return T{};
            }
            auto get_content() const -> std::string
            {
                return url;
            }

        private:
            std::string url;
        };

        using SingleMessage = std::variant<TextMessage, ImageMessage>;
        class MessageArray
        {
        public:
            MessageArray() = default;
            MessageArray(const std::string &messages)
            {
                nlohmann::json msg_array = nlohmann::json::parse(messages);
                for (const auto &msg : msg_array)
                {
                    auto message_type = msg.value("type", "");
                    if (message_type == "text")
                        attach(TextMessage{msg["data"]["text"].get<std::string>()});
                    else if (message_type == "image")
                        attach(ImageMessage{msg["data"]["url"].get<std::string>()});
                }
            }

            auto attach(auto &&message) -> MessageArray &
            {
                messages.push_back(std::forward<decltype(message)>(message));
                return *this;
            }

            // 追加消息
            // other: 另一个消息组
            // 返回值: 自身
            // NOTE: 调用者负责保证 other 不是自身
            auto attach(const MessageArray &other) -> MessageArray &
            {
                assert(this != &other);
                std::ranges::copy(other.messages, std::back_inserter(messages));
                return *this;
            }

            // 追加消息
            // other: 另一个消息组
            // 返回值: 自身
            // NOTE: 调用者负责保证 other 不是自身
            auto attach(MessageArray &&other) -> MessageArray &
            {
                assert(this != &other);
                std::ranges::move(other.messages, std::back_inserter(messages));
                return *this;
            }

            // 将消息转为可供发送的格式
            // T: 目标格式，默认为 nlohmann::json
            // 返回值: 消息序列化后的格式
            // TODO: 应支持多种后端，当前仅支持 OneBot 的消息数组形式，还有 CQ 等需要支持
            // TODO: 考虑将本函数的功能抽离出来，用一个接口类来实现，可以是调用者通过模板参数指定，也可以是运行时传入接口类实例指定
            template <typename T = nlohmann::json>
            auto serialize_to() const -> T
            {
                if constexpr (std::is_same_v<T, nlohmann::json>)
                {
                    T message_array;
                    auto visitor = [&message_array](const auto &message)
                    {
                        message_array.push_back(message.template serialize_to<T>());
                    };
                    for (const auto &message : messages)
                        std::visit(visitor, message);
                    return message_array;
                }
                else
                    return T{};
            }

        private:
            std::vector<SingleMessage> messages;
        };
    } // namespace Message

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
        void run()
        {
            event_processor.run();
            server.listen(server_host, server_port);
        }

        // 向用户发送私聊消息
        // user_id: 用户QQ号
        // message: 消息内容
        // 返回值: httplib::Result
        auto send_private_message(const std::string &user_id, const MessageArray &message) -> httplib::Result
        {
            nlohmann::json msg_array = message.serialize_to();
            nlohmann::json payload = {
                {"user_id", user_id},
                {"message", msg_array}};
            return client.Post("/send_private_msg", payload.dump(), "application/json");
        }

        // 向群发送消息
        // group_id: 群号
        // message: 消息内容
        // 返回值: httplib::Result
        auto send_group_message(const std::string &group_id, const MessageArray &message) -> httplib::Result
        {
            nlohmann::json msg_array = message.serialize_to();
            nlohmann::json payload = {
                {"group_id", group_id},
                {"message", msg_array}};
            return client.Post("/send_group_msg", payload.dump(), "application/json");
        }

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

} // namespace qqpp

#endif // QQPP_H