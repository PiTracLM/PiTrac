/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "worker_thread.h"
#include <iostream>
#include <algorithm>

#include "logging_tools.h"
#include "gs_globals.h"

namespace golf_sim {


#define MSG_EXIT_THREAD			1
#define MSG_POST_USER_DATA		2
#define MSG_TIMER				3


namespace gs = golf_sim;


	GsThread::GsThread(const std::string& thread_name) : m_thread_(nullptr), thread_name_(thread_name)
	{
	}


	GsThread::~GsThread()
	{
		ExitThread();
	}

	bool GsThread::CreateThread()
	{
		if (!m_thread_)
			m_thread_ = std::unique_ptr<std::thread>(new std::thread(&GsThread::Process, this));
		return true;
	}

	std::thread::id GsThread::GetThreadId()
	{
		if (m_thread_ == nullptr) {
			GS_LOG_MSG(error, "GsThread::GetThreadId has null m_thread_");
		}
		return m_thread_->get_id();
	}

	std::thread::id GsThread::GetCurrentThreadId()
	{
		return std::this_thread::get_id();
	}

	void GsThread::ExitThread()
	{
		if (m_thread_ == nullptr) {
			return;
		}

		GS_LOG_TRACE_MSG(trace, "GsThread::ExitThread joining m_thread_");
		m_thread_->join();
		m_thread_ = nullptr;
		GS_LOG_TRACE_MSG(trace, "GsThread::ExitThread completed.");
	}

	void GsThread::Process()
	{
		// TBD - Base class probably doesn't need any processing
	}




	TimedCallbackThread::TimedCallbackThread(const std::string& thread_name, 
											long wait_time_ms, 
											void (*callback_function)(), 
											bool repeat_timer) :	GsThread(thread_name)
	{
		wait_time_ms_ = wait_time_ms;
		callback_function_ = callback_function;
		repeat_timer_ = repeat_timer;
		exit_timer_ = false;
	}

	TimedCallbackThread::~TimedCallbackThread()
	{
	}

	void TimedCallbackThread::ExitThread()
	{
		// TBD - Do whatever we need to make sure the timer is not still being waited on.
		repeat_timer_ = false;
		exit_timer_ = true;

		GsThread::ExitThread();
	}

	void TimedCallbackThread::Process()
	{
		GS_LOG_TRACE_MSG(trace, "TimedCallbackThread::Process() called.");

		long kSleepIncrementMs = 500;

		do {
			long remaining_time_ms = wait_time_ms_;

			// Don't go to sleep too long without checking back in
			while (!exit_timer_ && remaining_time_ms > 0) {

				long sleep_time = std::min((long)500, remaining_time_ms);

				// Sleep for a bit
				std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));

				remaining_time_ms = (std::max((long)0, remaining_time_ms - kSleepIncrementMs));
			}

			callback_function_();

		} while (repeat_timer_ && GolfSimGlobals::golf_sim_running_);


		GS_LOG_TRACE_MSG(trace, "TimedCallbackThread::Process() exiting.");
	}





	struct ThreadMsg
	{
		ThreadMsg(int i, std::shared_ptr<void> m) { id = i; msg = m; }
		int id;
		std::shared_ptr<void> msg;
	};


	// WorkerThread

	WorkerThread::WorkerThread(const std::string& thread_name) : GsThread(thread_name)
	{
	}


	// ~WorkerThread

	WorkerThread::~WorkerThread()
	{
		// Base class will ExitThread();
	}



	// ExitThread

	void WorkerThread::ExitThread()
	{
		if (!m_thread_) {
			return;
		}

		// Create a new ThreadMsg
		std::shared_ptr<ThreadMsg> threadMsg(new ThreadMsg(MSG_EXIT_THREAD, 0));

		// Put exit thread message into the queue
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_queue.push(threadMsg);
			m_cv.notify_one();
		}

		m_thread_->join();
		m_thread_ = nullptr;
	}


	// PostMsg

	void WorkerThread::PostMsg(std::shared_ptr<UserData> data)
	{
		if (!m_thread_) {
			GS_LOG_MSG(error, "WorkerThread::PostMsg has null m_thread_");
		}

		// Create a new ThreadMsg
		std::shared_ptr<ThreadMsg> threadMsg(new ThreadMsg(MSG_POST_USER_DATA, data));

		// Add user data msg to queue and notify worker thread
		std::unique_lock<std::mutex> lk(m_mutex);
		m_queue.push(threadMsg);
		m_cv.notify_one();
	}


	// TimerThread

	void WorkerThread::TimerThread()
	{
		while (!m_timerExit && GolfSimGlobals::golf_sim_running_)
		{
			// Sleep for 250mS then put a MSG_TIMER into the message queue
			std::this_thread::sleep_for(std::chrono::milliseconds(250));

			std::shared_ptr<ThreadMsg> threadMsg(new ThreadMsg(MSG_TIMER, 0));

			// Add timer msg to queue and notify worker thread
			std::unique_lock<std::mutex> lk(m_mutex);
			m_queue.push(threadMsg);
			m_cv.notify_one();
		}
	}


	// Process

	void WorkerThread::Process()
	{
		m_timerExit = false;
		std::thread timerThread(&WorkerThread::TimerThread, this);

		while (GolfSimGlobals::golf_sim_running_)
		{
			std::shared_ptr<ThreadMsg> msg;
			{
				// Wait for a message to be added to the queue
				std::unique_lock<std::mutex> lk(m_mutex);
				while (m_queue.empty())
					m_cv.wait(lk);

				if (m_queue.empty())
					continue;

				msg = m_queue.front();
				m_queue.pop();
			}

			switch (msg->id)
			{
			case MSG_POST_USER_DATA:
			{
				if (msg->msg == nullptr) {
					GS_LOG_MSG(error, "WorkerThread::Process found null msg pointer.");
				}

				auto userData = std::static_pointer_cast<UserData>(msg->msg);
				std::cout << userData->msg.c_str() << " " << userData->year << " on " << thread_name_ << std::endl;

				break;
			}

			case MSG_TIMER:
				std::cout << "Timer expired on " << thread_name_ << std::endl;
				break;

			case MSG_EXIT_THREAD:
			{
				m_timerExit = true;
				timerThread.join();
				return;
			}

			default:
				GS_LOG_MSG(error, "WorkerThread::Process received unknown message.");
			}
		}
	}

}