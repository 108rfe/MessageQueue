#ifndef _MESSAGE_QUEUE_H_
#define _MESSAGE_QUEUE_H_

#include <queue>
#include <string>
#include <thread>
#include <chrono>
#include "TinyThread/TinyThread.h"

using namespace std;

//////////////////////////////////////////////////////////////////////////
// Interface for MessageQueue events
//
class IMessageQueueEvents
{
public:
	virtual void on_start() = 0;
	virtual void on_stop() = 0;
	virtual void on_hwm() = 0;
	virtual void on_lwm() = 0;
};

//////////////////////////////////////////////////////////////////////////
// Class for storing message with type T and priority
//
template<typename T>
class Message
{
	// Message data
	T data;
public:
	// Priority of message 
	unsigned short priority;
	// Initialize object of its class type
	Message(const Message& msg) : priority(msg.priority), data(msg.data) { }
	// Initialize object of its class type
	// Use the explicit keyword to prevent the compiler from using the constructor for implicit conversions
	explicit Message(const T& data, unsigned short priority) : data(data), priority(priority) { }
	// operator =
	Message& operator = (const Message& msg)
	{
		data = msg.data;
		priority = msg.priority;
		return *this;
	}
	// Get data of message
	const T& get_msg_data() const
	{
		return data;
	}
	// The compare method for priority_queue in ">" case
	const bool operator > (const Message &rd) const
	{
		return (priority > rd.priority);
	}
	// The compare method for priority_queue in "<" case
	const bool operator < (const Message &rd) const
	{
		return (priority < rd.priority);
	}
};

// Return codes to control msg queue status
enum RetCode {
	OK = 0,
	HWM = -1,
	NO_SPACE = -2,
	STOPPED = -3
};

//////////////////////////////////////////////////////////////////////////
// Class MessageQueue
//
// Support the HWM/LWM mechanism (high water mark/low water mark), message priorities and the “get” blocking call (the call is blocking before message appears in the queue)
// 
template<typename T>
class MessageQueue
{
	// TinyThread: the recursivethr_mutex uses WinApi CriticalSection for Win32 and pthreadthr_mutex_* for POSIX
	tthread::recursivethr_mutex thr_mutex;
	// TinyThread: the queue is with blocking get, therefore use condition_variable to notify from get to put
	tthread::condition_variable thr_cv;

	// Queue parameters
	int hwm, lwm, qSize;

	// A particular Interface for MessageQueue events
	IMessageQueueEvents *pEvent;

	// The third template parameter for priority_queue is the comparator. Set it to use "less"
	typedef Message<T> Msg;
	priority_queue< Msg, vector<Msg>, less<Msg> > pr_queue;

	bool bAlreadyStopped; // Is queue stopped
public:

	// Initialize object of its class type
	MessageQueue(int qSize, int lwm, int hwm) :qSize(qSize), lwm(lwm), hwm(hwm), pEvent(NULL), bAlreadyStopped(false) {	};

	// Destructor
	~MessageQueue();

	// Send message to the queue by priority
	RetCode put(const T& message, int priority);

	// Receive message from the queue 
	RetCode get(T &message);

	// Create thread to call run_safe
	void run();

	// Stop working
	void stop();

	// Callback for the events
	void set_event(IMessageQueueEvents *pIMessageQueueEvents);

	// Check queue status
	const bool isStopped() const
	{
		return bAlreadyStopped;
	}

};

//////////////////////////////////////////////////////////////////////////
//  Class Writer
//  
//  The owner creates Writer object by constructor that must have pointer on MessageQueue 
//  Then owner calls run to start. 
//  After that object creates thread that generates messages and checks status that can be paused, resumed or aborted 
//  The control of status is happened in implemented functions of IMessageQueueEvents interface
//
template<typename T>
class Writer : public IMessageQueueEvents
{
	MessageQueue<T> *pQueue;
	tthread::thread *pThread;

	enum WriterState {
		IDLE = 0,
		RUN,
		PAUSED,
		ABORTED
	};
	WriterState wr_stat;
public:
	// Initialize object of its class type
	// Use the explicit keyword to prevent the compiler from using the constructor for implicit conversions
	explicit Writer(MessageQueue<T>* pQueue) :pThread(NULL), pQueue(pQueue), wr_stat(IDLE) { }
	// Destructor
	~Writer();
private:
	// Thread procedure which run Writer under the thread
	static void run_safe(void *param);

public:

	// Start write messages to the queue
	void run();

public:
	void on_start()
	{
		run();
	}
	void on_stop()
	{
		// Stop write messages to the queue
		wr_stat = ABORTED;
	}
	// The on_hwm notification method enables suspension of the Writers if the queue is overflown
	void on_hwm()
	{
		// Make a pause in work
		if (wr_stat == RUN)
			wr_stat = PAUSED;
	}
	// The on_lwm notification method enables recommencement of the Writers if the queue is free again
	void on_lwm()
	{
		// Resume work
		if (wr_stat == PAUSED)
			wr_stat = RUN;
	}
};


//////////////////////////////////////////////////////////////////////////
// Class Reader
// 
// The Reader class object is permanently in MessageQueue messages waiting state. The Reader stream stops only if the MessageQueue.get method returns STOPPED.
// The handle_message procedure is called each time to process a received message e.g.to print message into console or a file.
//

template<typename T>
class Reader
{
	int timeout;
	MessageQueue<T> *pQueue;
	string s_prefix;
	tthread::thread *pThread;

public:
	// Initialize object of its class type
	// Use the explicit keyword to prevent the compiler from using the constructor for implicit conversions
	explicit Reader(MessageQueue<T>* pQueue, int timeout_between_reading = 0, const char *scPrefizxForPrinting = NULL)
		: pThread(NULL),
		pQueue(pQueue),
		timeout(timeout_between_reading)
	{
		if (scPrefizxForPrinting)
			s_prefix = scPrefizxForPrinting;
		else
		{
			char s[100];
			sprintf_s(s, "Reader[0x%x]:", this);
			s_prefix = s;
			//s_prefix << "Reader(0x" << setbase(16) << (int)this << "):";
		}
	}

	// Destructor
	~Reader()
	{
		if (pThread)
		{
			// Block the calling thread until active thread has done its job
			pThread->join();
			// Delete thread which has done its job
			delete pThread;
		}
	}

	// Start via thread
	static void run_safe(void *param);
	// Create thread
	void run();

protected:
	// The handle_message procedure is called each time to process a received message e.g. to print message into console or a file
	void handle_message(const T& msg);
};

//////////////////////////////////////////////////////////////////////////
// Class EventManager
//
// Translate events from MessageQueue to Writer 
// 
class EventManager : public IMessageQueueEvents
{
	vector<IMessageQueueEvents*> events;
public:
	int Add(IMessageQueueEvents *pEvent);
public:
	void on_start();
	void on_stop();
	void on_hwm();
	void on_lwm();
};

#endif _MESSAGE_QUEUE_H_