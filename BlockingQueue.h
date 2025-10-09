#pragma once
#include <mutex>
#include <condition_variable>
#include <deque>

template<typename T>
class BlockingQueue
{
public:
	//noncopyable
	BlockingQueue(const BlockingQueue&) = delete;
	void operator=(const BlockingQueue&) = delete;

	BlockingQueue()
	{}

	void enqueue(const T& x)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		queue_.emplace_back(x);
		notEmpty_.notify_one(); // wait morphing saves us
		// http://www.domaigne.com/blog/computing/condvars-signal-with-mutex-locked-or-not/
	}
	void wait_dequeue(T& v)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		// always use a while-loop, due to spurious wakeup
		while (queue_.empty())
		{
			notEmpty_.wait(lock);
		}
		//assert(!queue_.empty());
		v = std::move(queue_.front());
		queue_.pop_front();
	}

	size_t size() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return queue_.size();
	}
private:
	mutable std::mutex		mutex_;
	std::condition_variable	notEmpty_;
	std::deque<T>			queue_;
};