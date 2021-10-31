#include "threadpool.h"

#include <iostream>
#include <vector>
#include <fstream>
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <utility>

#include "store.grpc.pb.h"
#include "vendor.grpc.pb.h"

using namespace std;

using store::Store;
using store::ProductQuery;
using store::ProductReply;
using store::ProductInfo;

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::Status;

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;

using vendor::Vendor;
using vendor::BidQuery;
using vendor::BidReply;

Threadpool* threadpool;
vector<string> vendorIPs;

static int debug_level = 2;

class VendorClient {
public:
    VendorClient() {
        for(auto vendorIP : vendorIPs){
            shared_ptr < Channel > channel = grpc::CreateChannel(vendorIP, grpc::InsecureChannelCredentials());
            stubs_.push_back(Vendor::NewStub(channel));
        }
    }

    // Assembles the client's payload, sends it and presents the response back
    // from the server.
    vector < pair < double, string > > getProductBid(string product_name) {
		if(debug_level > 1)
			cout << "getProductBid start" << endl;

        // Data we are sending to the server.
        BidQuery query;
        query.set_product_name(product_name);

        // Container for the data we expect from the server.
        BidReply reply;

        // The producer-consumer queue we use to communicate asynchronously with the
        // gRPC runtime.
        CompletionQueue cq;

        // Storage for the status of the RPC upon completion.
        Status status;

		if(debug_level > 1)
			cout << "getProductBid start" << endl;
		
		// Since a stub is of type unique pointer, no copy can be created for this.
		// Hence we cannot use index based iterator or every auto based iterators
		// The only solution here is to use explicit iterators
        for(auto itr = stubs_.begin(); itr != stubs_.end(); itr++){
			// Context for the client. It could be used to convey extra information to
			// the server and/or tweak certain RPC behaviors.
			ClientContext context;

			if(debug_level > 1)
				cout << "getProductBid rpc start" << endl;

            // stub_->PrepareAsyncSayHello() creates an RPC object, returning
            // an instance to store in "call" but does not actually start the RPC
            // Because we are using the asynchronous API, we need to hold on to
            // the "call" instance in order to get updates on the ongoing RPC.
            std::unique_ptr < ClientAsyncResponseReader < BidReply > > rpc(
                (*itr) -> PrepareAsyncgetProductBid( & context, query, & cq));

            // StartCall initiates the RPC call
            rpc -> StartCall();

            // Request that, upon completion of the RPC, "reply" be updated with the
            // server's response; "status" with the indication of whether the operation
            // was successful. Tag the request with the integer 1.
            rpc -> Finish( & reply, & status, (void * ) 1);

			if(debug_level > 1)
				cout << "getProductBid rpc async done" << endl;
        }

        // Act upon the status of the actual RPC.
        vector < pair < double, string > > toReturn;

        for(int i = 0; i < vendorIPs.size(); i++){
			if(debug_level > 1)
				cout << "getProductBid async response" << endl;

            void * got_tag;
            bool ok = false;
            // Block until the next result is available in the completion queue "cq".
            // The return value of Next should always be checked. This return value
            // tells us whether there is any kind of event or the cq_ is shutting down.
            GPR_ASSERT(cq.Next( & got_tag, & ok));

            // Verify that the result from "cq" corresponds, by its tag, our previous
            // request.
            GPR_ASSERT(got_tag == (void * ) 1);
            // ... and that the request was completed successfully. Note that "ok"
            // corresponds solely to the request for updates introduced by Finish().
            GPR_ASSERT(ok);

            if (status.ok()) 
                toReturn.push_back({reply.price(), reply.vendor_id()});
        }

        return toReturn;
    }

private:
    // Out of the passed in Channel comes the stub, stored here, our view of the
    // server's exposed services.
    vector < unique_ptr < Vendor::Stub > > stubs_;
};

class ServerImpl final {
public:
	~ServerImpl() {
		server_ -> Shutdown();
		// Always shutdown the completion queue after the server.
		cq_ -> Shutdown();
	}

	ServerImpl(){}

	// There is no shutdown handling in this code.
	void Run(char* ip) {
		string server_address(ip);

		ServerBuilder builder;
		// Listen on the given address without any authentication mechanism.
		builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
		// Register "service_" as the instance through which we'll communicate with
		// clients. In this case it corresponds to an *asynchronous* service.
		builder.RegisterService( & service_);
		// Get hold of the completion queue used for the asynchronous communication
		// with the gRPC runtime.
		cq_ = builder.AddCompletionQueue();
		// Finally assemble the server.
		server_ = builder.BuildAndStart();
		cout << "Server listening on " << server_address << endl;

		// Proceed to the server's main loop.
		HandleRpcs();
	}

private:
	unique_ptr < ServerCompletionQueue > cq_;
	Store::AsyncService service_;
	unique_ptr < Server > server_;

	// Class encompasing the state and logic needed to serve a request.
	class CallData {
    public:
        // Take in the "service" instance (in this case representing an asynchronous
        // server) and the completion queue "cq" used for asynchronous communication
        // with the gRPC runtime.
        CallData(Store::AsyncService * service, ServerCompletionQueue * cq): service_(service), cq_(cq), responder_( & ctx_), status_(CREATE) {
            // Invoke the serving logic right away.
            Proceed();
        }

		void Proceed() {
			if (status_ == CREATE) {
				// Make this instance progress to the PROCESS state.
				status_ = PROCESS;

				// As part of the initial CREATE state, we *request* that the system
				// start processing SayHello requests. In this request, "this" acts are
				// the tag uniquely identifying the request (so that different CallData
				// instances can serve different requests concurrently), in this case
				// the memory address of this CallData instance.
				service_ -> RequestgetProducts( & ctx_, & request_, & responder_, cq_, cq_,
					this);
			} else if (status_ == PROCESS) {
				threadpool->add_job([this](){
					if(debug_level > 1)
						cout << "Proceed start" << endl;

					// Spawn a new CallData instance to serve new clients while we process
					// the one for this CallData. The instance will deallocate itself as
					// part of its FINISH state.
					new CallData(service_, cq_);

					if(debug_level > 1)
						cout << "Proceed after calldata" << endl;

					string product_name = request_.product_name();

					VendorClient client;
                    vector < pair<double, string> > response = client.getProductBid(product_name);

					for(int i = 0; i < response.size(); i++) {
						ProductInfo *product = reply_.add_products();
						product->set_price(response[i].first);
                        product->set_vendor_id(response[i].second);
					}

					// And we are done! Let the gRPC runtime know we've finished, using the
					// memory address of this instance as the uniquely identifying tag for
					// the event.
					if(debug_level > 1)
						cout << "Proceed end\n" << endl;
					status_ = FINISH;
					responder_.Finish(reply_, Status::OK, this);
				});
			} else {
				GPR_ASSERT(status_ == FINISH);
				// Once in the FINISH state, deallocate ourselves (CallData).
				delete this;
			}
		}

	private:
		// The means of communication with the gRPC runtime for an asynchronous
		// server.
		Store::AsyncService * service_;
		// The producer-consumer queue where for asynchronous server notifications.
		ServerCompletionQueue * cq_;
		// Context for the rpc, allowing to tweak aspects of it such as the use
		// of compression, authentication, as well as to send metadata back to the
		// client.
		ServerContext ctx_;

		// What we get from the client.
		ProductQuery request_;
		// What we send back to the client.
		ProductReply reply_;

		// The means to get back to the client.
		ServerAsyncResponseWriter < ProductReply > responder_;

		// Let's implement a tiny state machine with the following states.
		enum CallStatus {
			CREATE,
			PROCESS,
			FINISH
		};
		CallStatus status_; // The current serving state.
	};

	// This can be run in multiple threads if needed.
	void HandleRpcs() {
		// Spawn a new CallData instance to serve new clients.
		new CallData( & service_, cq_.get());
		void * tag; // uniquely identifies a request.
		bool ok;
		while (true) {
			if(debug_level > 1)
				cout << "HandleRpcs start" << endl;
			// Block waiting to read the next event from the completion queue. The
			// event is uniquely identified by its tag, which in this case is the
			// memory address of a CallData instance.
			// The return value of Next should always be checked. This return value
			// tells us whether there is any kind of event or cq_ is shutting down.
			GPR_ASSERT(cq_ -> Next( & tag, & ok));

			if(debug_level > 1)
				cout << "HandleRpcs received" << endl;

			GPR_ASSERT(ok);
			if(debug_level > 1)
				cout << "HandleRpcs ok" << endl;

			static_cast < CallData * > (tag) -> Proceed();

			if(debug_level > 1)
				cout << "HandleRpcs done" << endl;
		}
	}
};

void get_vendor_ips(char* filename){
	string line;
	ifstream myfile(filename);
	if (myfile.is_open()) {
		while (getline (myfile,line)) {
			cout << "reading vendor ip " << line << endl;
			vendorIPs.push_back(line);
		}
		myfile.close();
	}
	else {
		cout << "Unable to open file"; 
	}
}

int main(int argc, char** argv) {
	if(argc != 4) {
		cout << "Incorrect usage. Expected is ./store <vendor_file_path>"
		" <IP Address:Port of store server>"
		" Maximum number of threads in threadpool\n";
		exit(0);
	}
	cout << "Received file path: " << argv[1] << "\n";
	cout << "Received IP:Port : " << argv[2] << "\n";
	cout << "Received max threads: " << argv[3] << "\n";

	threadpool = new Threadpool(atoi(argv[3]));
	get_vendor_ips(argv[1]);

	ServerImpl server;
	server.Run(argv[2]);

	return EXIT_SUCCESS;
}

