/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

/* SPDX-License-Identifier: CPOL-1.02 */
// @see https://www.codeproject.com/Articles/1169105/Cplusplus-std-thread-Event-Loop-with-Message-Queue
// David Lafreniere, Feb 2017.

#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>

namespace golf_sim {


    class GsThread
    {
    public:
        // Constructor
        GsThread(const std::string& threadName);

        // Destructor
        virtual ~GsThread();

        // Called once to create the worker thread
        // @return True if thread is created. False otherwise. 
        virtual bool CreateThread();

        // Called once a program exits to exit the worker thread
        virtual void ExitThread();

        // Get the ID of this thread instance
        // @return The worker thread ID
        std::thread::id GetThreadId();

        // Get the ID of the currently executing thread
        // @return The current thread ID
        static std::thread::id GetCurrentThreadId();

        // Entry point for the worker thread
        virtual void Process();

    protected:
        std::unique_ptr<std::thread> m_thread_;
        std::string thread_name_;

    private:
        
        GsThread(const GsThread&) = delete;
        GsThread& operator=(const GsThread&) = delete;
    };



    class TimedCallbackThread : public GsThread
    {
    public:
        TimedCallbackThread(const std::string& threadName, long wait_time_ms, void (*callback_function_)(), bool repeat_timer = false );

        ~TimedCallbackThread();

        // Shuts down the timer thread
        void ExitThread() override;

        // Waits for the specified time and then calls the call back function
        void Process() override;

    private:

        long wait_time_ms_ = 0;
        void (*callback_function_)();
        // If true then after the timer expires and the callback is called, the 
        // timer thread resets and starts waiting again
        bool repeat_timer_ = false;

        // When true, stop the timer ASAP
        bool exit_timer_ = false;
    };

struct UserData
{
    std::string msg;
    int year;
};

struct ThreadMsg;

class WorkerThread : public GsThread
{
public:
    WorkerThread(const std::string &threadName);

    ~WorkerThread();

    // Shuts down the timer thread
    void ExitThread() override;

    // Add a message to the thread queue
    // @param[in] data - thread specific message information
    void PostMsg(std::shared_ptr<UserData> msg);

    // Entry point for the worker thread
    void Process() override;

private:
    // Entry point for timer thread
    void TimerThread();

    std::queue<std::shared_ptr<ThreadMsg>> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_timerExit;
};

}