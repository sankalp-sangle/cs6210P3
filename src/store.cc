#include "threadpool.h"

#include <iostream>
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>

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

Threadpool* threadpool;

class ServerImpl final {
public:
	~ServerImpl() {
		server_ -> Shutdown();
		// Always shutdown the completion queue after the server.
		cq_ -> Shutdown();
	}

	ServerImpl(){}

	// There is no shutdown handling in this code.
	void Run() {
		string server_address("0.0.0.0:50051");

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
					// Spawn a new CallData instance to serve new clients while we process
					// the one for this CallData. The instance will deallocate itself as
					// part of its FINISH state.
					new CallData(service_, cq_);

					cout << "Inside Proceed()\n";

					// And we are done! Let the gRPC runtime know we've finished, using the
					// memory address of this instance as the uniquely identifying tag for
					// the event.
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
			// Block waiting to read the next event from the completion queue. The
			// event is uniquely identified by its tag, which in this case is the
			// memory address of a CallData instance.
			// The return value of Next should always be checked. This return value
			// tells us whether there is any kind of event or cq_ is shutting down.
			GPR_ASSERT(cq_ -> Next( & tag, & ok));
			GPR_ASSERT(ok);
			static_cast < CallData * > (tag) -> Proceed();
		}
	}
};

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

	ServerImpl server;
	server.Run();

	return EXIT_SUCCESS;
}

