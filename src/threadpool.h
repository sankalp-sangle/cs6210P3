#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <iostream>

using namespace std;

static int debug_level_threadpool = 0;

// Idea taken from
// https://stackoverflow.com/questions/15752659/thread-pooling-in-c11
class Threadpool {
private:
    int numThreads;
    vector<thread> threadPool;

    queue<function<void(void)>> eventQueue;
    mutex queueMutex;
    
    condition_variable condition;

    // Function that every threads runs. will keep 
    void thread_loop_function(){
        function<void(void)> currentJob;
        while(true) {
            {
                unique_lock<mutex> lock(queueMutex);

                condition.wait(lock, [this]() {
                    return !eventQueue.empty();
                });

                currentJob = eventQueue.front();
                eventQueue.pop();
            }
            cout << "Thread is executing a job\n";
            currentJob();
        }
    }

public:
    Threadpool(int requestedThreads){
        if(debug_level_threadpool > 1)
			cout << "Threadpool constructor start" << endl;

        // Taper at max threads possible
        numThreads = max((int) thread::hardware_concurrency(), requestedThreads);

        if(debug_level_threadpool > 1)
			cout << "Threadpool constructor maxthreads " <<  thread::hardware_concurrency() << endl;

        if(debug_level_threadpool > 1)
			cout << "Threadpool constructor threads " << numThreads << endl;

        for(int i = 0; i < numThreads; i++)
            threadPool.push_back(thread([this,i]{ this->thread_loop_function(); }));
    }

    void add_job(function<void(void)> job) {
        {
            if(debug_level_threadpool > 1)
				cout << "Threadpool addjob start" << endl;

            unique_lock<mutex> lock(queueMutex);
            eventQueue.push(job);
        }
        if(debug_level_threadpool > 1)
			cout << "Added a job to the queue\n";

        condition.notify_one();
    }
};
