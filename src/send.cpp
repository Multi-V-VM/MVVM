//
// Created by victoryang00 on 2/8/23.
//

#include "wamr.grpc.pb.h"
#include "wamr.pb.h"
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <string>

using namespace test;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

class WAMRServiceImpl final : public WAMRService::Service {
    Status WAMRMethod(ServerContext *context, const WAMRRequest *request, Student *response) override {
        response->set_name("Test");
        response->set_id(123);
        response->set_age(24);

        response->mutable_phone()->set_type(Student_PhoneType_DESK);
        response->mutable_phone()->set_number("+00 123 1234567");

        response->mutable_address()->set_address1("House # 1, Street # 1");
        response->mutable_address()->set_address2("House # 2, Street # 2");

        response->mutable_college()->set_name("XYZ College");
        response->mutable_college()->set_address("College Address Here");
        return Status::OK;
    }
};

void RunServer() {
    std::string server_address{"localhost:2510"};
    WAMRServiceImpl service;

    // Build server
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server{builder.BuildAndStart()};

    // Run server
    std::cout << "Server listening on " << server_address << std::endl;
    server->Wait();
}

int main() {
    RunServer();
    return 0;
}