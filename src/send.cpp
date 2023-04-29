//
// Created by victoryang00 on 2/8/23.
//

#include <iostream>
#include <string>
#include "wamr.pb.h"
#include "wamr.grpc.h"
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using wamr::WAMRRequest;
using wamr::WAMRResponse;
using wamr::WAMRService;


class WAMRServiceImpl final : public WAMRService::Service {
    Status SampleMethod(ServerContext* context, const SampleRequest* request, SampleResponse* response) override {
        response->set_response_sample_field("Hello " + request->request_sample_field());
        return Status::OK;
    }
};


int main(){
    using namespace test;

    // Serialization
    Student s;
    s.set_name("Test");
    s.set_id(123);
    s.set_age(24);

    s.mutable_phone()->set_type(Student_PhoneType_DESK);
    s.mutable_phone()->set_number("+00 123 1234567");

    s.mutable_address()->set_address1("House # 1, Street # 1");
    s.mutable_address()->set_address2("House # 2, Street # 2");

    s.mutable_college()->set_name("XYZ College");
    s.mutable_college()->set_address("College Address Here");

    std::cout << "Serialization:\n\n" << s.DebugString() << "\n\n";
    //s.PrintDebugString();

    std::string serialized;
    if ( !s.SerializeToString( &serialized ) )
    {
        std::cerr << "ERROR: Unable to serialize!\n";
        return -1;
    }


}