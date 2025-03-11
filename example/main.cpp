#include <string>
#include <iostream>
#include <format>
#include <qqpp/qqpp.h>

int main()
{
    using nlohmann::json;
    int64_t user_id{};
    std::cout << "Please input your user_id:\n";
    std::cin >> user_id;
    qqpp::Session session("http://127.0.0.1:12345", "0.0.0.0", 54321);

    session.on<qqpp::PrivateMessage>(
        [&session, user_id](const qqpp::PrivateMessage &event)
        {
            if (event.user_id() == user_id)
            {
                auto user_id = event.user_id();
                auto message_received = event.message();
                std::cout << std::format("Got message from user {}: {}", user_id, message_received) << std::endl;
                qqpp::MessageArray message_sent{message_received};
                message_sent.attach(qqpp::TextMessage{"\nHi, I got your message!"});
                session.send_private_message(std::to_string(user_id), message_sent);
                std::cout << std::format("Sent message to user {}, content: {}", user_id, message_sent.serialize_to().dump()) << std::endl;
            }
        });
    std::cout << "starting server!\n";
    session.run();
}