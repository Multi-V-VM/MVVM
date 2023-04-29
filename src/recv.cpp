//
// Created by victoryang00 on 4/29/23.
//

#include "wamr.grpc.pb.h"
#include "wamr.pb.h"
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <string>

using namespace test;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

class WAMRClient {
public:
    WAMRClient(std::shared_ptr<grpc::Channel> channel) : _stub{WAMRService::NewStub(channel)} {}

    struct Student WAMRMethod(int id) {
        // Prepare request
        WAMRRequest request;
        request.set_id(id);

        // Send request
        Student response;
        ClientContext context;
        Status status;
        status = _stub->WAMRMethod(&context, request, &response);

        // Handle response
        if (status.ok()) {
            return response;
        } else {
            std::cerr << status.error_code() << ": " << status.error_message() << std::endl;
            return Student{};
        }
    }

private:
    std::unique_ptr<WAMRService::Stub> _stub;
};

int main() {
    std::string server_address{"localhost:2510"};
    WAMRClient client{grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials())};
    auto deserialized = client.WAMRMethod(0);

    std::cout << deserialized.name() << deserialized.id() << deserialized.phone().type()
              << deserialized.phone().number();

    return 0;
}