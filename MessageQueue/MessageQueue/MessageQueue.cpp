// message_queue.cpp : Defines the entry point for the console application.
//
#include <iostream>
#include <time.h>

#include "MessageQueue.h"

using namespace std;

//
// Minor method to return the tick count in milliseconds
//
unsigned getTickCount()
{
#ifdef _WIN32
	return GetTickCount();
#else
	struct timeval tv;
	gettimeofday(&tv, 0);
	return unsigned((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
#endif
}
const unsigned g_initialTick = getTickCount();
const unsigned g_sizeBuf = 1024;

//
// Minor method to put trace in stderr stream 
//
void PutTrace(const char* format_string, ...)
{
	int nSize = 0;
	char buf[g_sizeBuf];

	sprintf_s(buf, g_sizeBuf, "%.3f : ", (double)(getTickCount() - g_initialTick) / 1000);

	va_list args;
	va_start(args, format_string);
	nSize = vsnprintf_s(buf + strlen(buf), sizeof(buf) - strlen(buf), _TRUNCATE, format_string, args);
	//printf("nSize: %d, buf: %s\n", nSize, buf);
	va_end(args);

	fprintf(stderr, buf);
}

//////////////////////////////////////////////////////////////////////////
template<typename T> MessageQueue<T>::~MessageQueue()
{
	PutTrace("MessageQueue::~MessageQueue [0x%x]\n", this);
	// Notify about final stop
	thr_cv.notify_all();
}

// Send message to the queue with priority parameter
//
// Note: Below it is used the mechanism to avoid deadlock (when the blocked thread will remain waiting) by means:
//       1. The program uses the following classes from TinyThread: thread, recursive_mutex and condition_variable.
//       2. The lock_guard when an object is created, it attempts to acquire the mutex (by calling lock()),
//          and when the object is destroyed, it automatically releases the mutex (by calling unlock()).
//       3. The recursive_mutex allows to get the same mutex multiple times.
//       4. When a thread owns a recursive_mutex, all other threads will block (for calls to lock).
//       5. In condition_variable there must be at least one thread signaling that the condition has become true.
//          The signal can be sent with notify_one(), which will unblock one (any) of the waiting threads, or notify_all(), which will unblock all waiting threads.
//
template<typename T> RetCode MessageQueue<T>::put(const T& message, int priority)
{
	// Make sure a RAII ownership-wrapper object by lock the mutex
	tthread::lock_guard<tthread::recursivethr_mutex> lock(thr_mutex);

	// Put message and notify Reader about
	Msg msg(message, priority);
	pr_queue.push(msg);
	if (pr_queue.size() == 1)
	{
		thr_cv.notify_one();
	}

	// Invoke queue status event
	if (pEvent && pr_queue.size() >= (unsigned)hwm)
	{
		pEvent->on_hwm();
	}

	return OK;
}

//
// Receive message from the queue
//
template<typename T> RetCode MessageQueue<T>::get(T &message)
{
	// Make sure a RAII ownership-wrapper object by lock the mutex
	tthread::lock_guard<tthread::recursivethr_mutex> lock(thr_mutex);

	// Check that queue is stopped
	if (isStopped())
		return RetCode::STOPPED;

	// Abort if the queue is empty
	while (pr_queue.size() == 0)
	{
		// Will block until 'thr_cv' is notified
		thr_cv.wait(thr_mutex);
		if (pr_queue.size() == 0 && isStopped())
		{
			PutTrace("MessageQueue::get [0x%x] aborts after stop\n");
			return RetCode::STOPPED;
		}
		PutTrace("MessageQueue::get [0x%x] continues wait\n");
	}

	// Get message data from the queue 
	{
		message = pr_queue.top().get_msg_data();
		pr_queue.pop();

		// Invoke a queue event
		if (pEvent && pr_queue.size() <= (unsigned)lwm)
		{
			pEvent->on_lwm();
		}
	}

	return OK;
}

//
// Appeal to start working
//
template<typename T> void MessageQueue<T>::run()
{
	tthread::lock_guard<tthread::recursivethr_mutex> lock(thr_mutex);

	if (pEvent)
	{
		pEvent->on_start();
	}

	return;
}

//
// Appeal to stop working
//
template<typename T> void MessageQueue<T>::stop()
{
	tthread::lock_guard<tthread::recursivethr_mutex> lock(thr_mutex);

	bAlreadyStopped = true;

	// Send stop to writer
	if (pEvent)
	{
		pEvent->on_stop();
	}

	// Remove all the queue elements
	while (pr_queue.size() > 0)
	{
		pr_queue.pop();
	}

	thr_cv.notify_all();
}

// Set callback for the events
template<typename T> void MessageQueue<T>::set_event(IMessageQueueEvents *pIMessageQueueEvents)
{
	pEvent = pIMessageQueueEvents;
}

//////////////////////////////////////////////////////////////////////////
template<typename T>
Writer<T>::~Writer()
{
	if (pThread)
	{
		// Block the calling thread until active thread has done its job
		pThread->join();
		// Delete thread which has done its job
		delete pThread;
	}
}

template<typename T>
void Writer<T>::run_safe(void *param)
{
	char s[100];
	Writer<T> *pThis = (Writer<T>*)param;
	if (!pThis)
	{
		return;
	}

	pThis->wr_stat = RUN;

	// Seed the pseudo-random number generator with current thread ID as the value seed
	srand((int)pThis);

	while (true)
	{
		if (pThis->pQueue->isStopped())
		{
			PutTrace("Writer::run_safe [0x%x] aborted\n", pThis);
			break;
		}
		if (pThis->wr_stat == PAUSED)
		{
			PutTrace("Writer::run_safe [0x%x] is waiting for resume\n", pThis);
			// Waiting 100 milliseconds for resume
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		T message;
		int priority = rand() % (int)1000;

		if (typeid(T) == typeid(string))
		{
			sprintf_s(s, "msg %d with [0x%x]", priority, pThis);
			message = s;
			PutTrace("Writer[0x%x] with: %s\n", pThis, s);
		}
		else if (typeid(T) == typeid(int))
		{
			message = priority;
			PutTrace("Writer[0x%x] with: %d\n", pThis, s);
		}
		else
		{
			break;
		}

		// Put msg to the queue
		if (pThis->pQueue)
			pThis->pQueue->put(message, priority);
	}
}

// Launch put messages to the queue
template<typename T> void Writer<T>::run()
{
	if (pThread)
	{
		// To launch again run() need to wait the existing thread execution has completed and delete it
		//
		// Block the calling thread until active thread has done its job
		pThread->join();
		// Delete thread which has done its job
		delete pThread;
	}

	pThread = new tthread::thread(run_safe, this);
	return;
}

//////////////////////////////////////////////////////////////////////////
// Class Reader
// Start Reader via thread
//
template<typename T>
void Reader<T>::run_safe(void *param)
{
	Reader<T> *pThis = (Reader<T>*)param;
	PutTrace("Reader::run_safe [0x%x] start\n", pThis);
	if (!pThis)
	{
		return;
	}

	while (pThis->pQueue)
	{
		T msg;

		// Get message from the queue
		RetCode ret = pThis->pQueue->get(msg);

		if (ret == RetCode::STOPPED)
		{
			break;
		}

		pThis->handle_message(msg);
		std::this_thread::sleep_for(std::chrono::milliseconds(pThis->timeout));
	}
	PutTrace("Reader::run_safe [0x%x] finish\n", pThis);
}

template<typename T>
void Reader<T>::run()
{
	if (pThread)
	{
		// To launch again run() need to wait the existing thread execution has completed and delete it
		//
		// Block the calling thread until active thread has done its job
		pThread->join();
		// Delete thread which has done its job
		delete pThread;
	}

	pThread = new tthread::thread(run_safe, this);
	return;
}

// Handle message
template<typename T>
void Reader<T>::handle_message(const T& msg)
{
#if 1 // Do trace to stderr
	if (typeid(T) == typeid(string))
		PutTrace("Reader::handle_message[0x%x] %s\n", this, msg.c_str());
	else if (typeid(T) == typeid(int))
		PutTrace("Reader::handle_message [0x%x] %d\n'", this, msg);
#endif

	if (typeid(T) == typeid(string))
		printf("%s %s\n", s_prefix.c_str(), msg.c_str());
	else if (typeid(T) == typeid(int))
		printf("%s %d\n", s_prefix.c_str(), msg);
}

//////////////////////////////////////////////////////////////////////////
// Class EventManager
// Send events from MessageQueue to each Writer 
//
int EventManager::Add(IMessageQueueEvents *pEvent)
{
	events.push_back(pEvent);
	return events.size();
}

void EventManager::on_start()
{
	PutTrace("%s\n", __FUNCTION__);

	for (vector<IMessageQueueEvents*>::iterator it = events.begin(); it != events.end(); it++)
	{
		(*it)->on_start();
	}
}
void EventManager::on_stop()
{
	PutTrace("%s\n", __FUNCTION__);

	for (vector<IMessageQueueEvents*>::iterator it = events.begin(); it != events.end(); it++)
	{
		(*it)->on_stop();
	}
}
void EventManager::on_hwm()
{
	PutTrace("%s\n", __FUNCTION__);

	for (vector<IMessageQueueEvents*>::iterator it = events.begin(); it != events.end(); it++)
	{
		(*it)->on_hwm();
	}
}
void EventManager::on_lwm()
{
	PutTrace("%s\n", __FUNCTION__);

	for (vector<IMessageQueueEvents*>::iterator it = events.begin(); it != events.end(); it++)
	{
		(*it)->on_lwm();
	}
}

//////////////////////////////////////////////////////////////////////////
// Start the program in hosted environment
int main()
{
	// Define the message as a string type
	typedef string MessageType;

	// Create the Queue object with qSize, lwm and hwm
	MessageQueue<MessageType> q(70, 10, 90);

	EventManager event_mgr;
	q.set_event(&event_mgr);

	// Create five Writer objects to write message to queue
	Writer<MessageType> wr1(&q);
	Writer<MessageType> wr2(&q);
	Writer<MessageType> wr3(&q);
	Writer<MessageType> wr4(&q);
	Writer<MessageType> wr5(&q);

	// Init EventManager to translate events from MessageQueue to particular Writer instance
	event_mgr.Add(&wr1);
	event_mgr.Add(&wr2);
	event_mgr.Add(&wr3);
	event_mgr.Add(&wr4);
	event_mgr.Add(&wr5);

	// Create three Reader objects that will read messages from the queue
	Reader<MessageType> rd1(&q);
	Reader<MessageType> rd2(&q);
	Reader<MessageType> rd3(&q);

	PutTrace(" *** Run Writers to put new messages to the queue ***\n");

	// Start 5 Writer objects to put messages
	wr1.run();
	wr2.run();
	wr3.run();
	wr4.run();
	wr5.run();

	// Wait 2 sec while queue is overflown 
	std::this_thread::sleep_for(std::chrono::milliseconds(2000));

	PutTrace(" *** Run Readers while Writers put new messages to the queue ***\n");

	// Start 3 Reader objects while Writers put new messages to the queue
	rd1.run();
	rd2.run();
	rd3.run();

	PutTrace(" *** Run pause ***\n");

	// Pause to watch on console for 7 sec
	std::this_thread::sleep_for(std::chrono::milliseconds(7000));

	PutTrace(" *** Run stop ***\n");

	// Final stop
	q.stop();

	return 0;
}