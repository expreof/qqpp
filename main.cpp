#include <vector>
#include <httplib.h>

int main()
{
    httplib::Server server;
    server.Post("/", [](const httplib::Request &request, httplib::Response &response)
                { std::cout << "Received a request from " << request.remote_addr << "\n"; });
    server.listen("localhost", 3000);
}