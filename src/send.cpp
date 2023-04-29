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
    Status WAMRMethod(ServerContext *context, const WAMRRequest *request, WAMRMemoryInstance *response) override {
        response->set_name("Test");
        response->set_id(123);
        response->set_age(24);

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