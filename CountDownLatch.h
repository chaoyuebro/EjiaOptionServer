#ifndef MUDUO_BASE_COUNTDOWNLATCH_H
#define MUDUO_BASE_COUNTDOWNLATCH_H
#include <mutex>
#include <condition_variable>

class CountDownLatch
{
 public:
	 //noncopyable
	 CountDownLatch(const CountDownLatch&) = delete;
	 void operator=(const CountDownLatch&) = delete;

	explicit CountDownLatch(int count);

	void wait();

	void countDown();

	int getCount() const;

private:
	mutable std::mutex mutex_;
	std::condition_variable condition_;
	int count_;
};
#endif  // MUDUO_BASE_COUNTDOWNLATCH_H
