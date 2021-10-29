#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <iostream>

using namespace std;

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
        // Taper at max threads possible
        numThreads = max((int) thread::hardware_concurrency(), requestedThreads);

        for(int i = 0; i < numThreads; i++)
            threadPool.push_back(thread([this,i]{ this->thread_loop_function(); }));
    }

    void add_job(function<void(void)> job) {
        {
            unique_lock<mutex> lock(queueMutex);
            eventQueue.push(job);
        }
        cout << "Added a job to the queue\n";
        condition.notify_one();
    }
};
