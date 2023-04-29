//
// Created by victoryang00 on 4/29/23.
//

#include <iostream>
#include <string>
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include "wamr.pb.h"


class WAMRClient {
public:
    WAMRClient(std::shared_ptr<grpc::Channel> channel) : _stub{WAMRService::NewStub(channel)} {}

    std::string SampleMethod(const std::string& request_sample_field) {
        // Prepare request
        WAMRRequest request;
        request.set_id(request_sample_field);

        // Send request
        SampleResponse response;
        ClientContext context;
        Status status;
        status = _stub->SampleMethod(&context, request, &response);

        // Handle response
        if (status.ok()) {
            return response.response_sample_field();
        } else {
            std::cerr << status.error_code() << ": " << status.error_message() << std::endl;
            return "RPC failed";
        }
    }

private:
    std::unique_ptr<SampleService::Stub> _stub;
};


int main()
{
    
    Student deserialized;
    if ( !deserialized.ParseFromString( serialized ) )
    {
        std::cerr << "ERROR: Unable to deserialize!\n";
        return -1;
    }

    std::cout << "Deserialization:\n\n";
    deserialized.PrintDebugString();

    deserialized.name();
    deserialized.id();
    deserialized.phone().type();
    deserialized.phone().number();
    
    return 0;
}