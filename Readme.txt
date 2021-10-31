Adwait Bauskar, Sankalp Sangle. CS 6210 AOS Project 3.

We have heavily relied on two examples from the grpc.io documentation for structuring our code.

These examples are -
Server (for client to store communication): https://github.com/grpc/grpc/blob/master/examples/cpp/helloworld/greeter_async_server.cc
Client (for store to vendor communication): https://github.com/grpc/grpc/blob/master/examples/cpp/helloworld/greeter_async_client.cc

We have also implemented our threadpool taking inspiration from the following Stack Overflow answer:
https://stackoverflow.com/questions/15752659/thread-pooling-in-c11

Finally, our code is asynchronous in that we send out calls from our store to the vendors concurrently,
and then wait on a common Completion Queue for all the replies. This prevents serialization of the 
request response cycle and ensures that threads parallely process the queries.

The flow can be described as follows:
0. Threadpool is set up by providing number of threads as an argument.
1. Incoming request from the clients is detected by the master thread.
2. Master thread pushes the request to a concurrency safe job queue belonging to the threadpool.
3. Job is picked up from the queue by a worker thread.
4. This thread will then query all vendors asynchronously
in the manner described above and wait for responses. Once all responses are in, the worker thread returns the
collated response to the client.
5. The worker thread then goes back to waiting for incoming jobs on the threadpool job queue.