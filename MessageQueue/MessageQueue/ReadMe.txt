Issue:

It is required to implement a blocking priority queue, for asynchronous messaging between multiple "Writer" and "Reader" threads, and a test application demonstrating its functionality.
The MessageQueue class must support the HWM/LWM (high water mark/low water mark), message priorities, and a blocking get call (the call blocks until the appearance of a message in the queue).

Notes:

For cross-platform multithreading, the free Open Source implementation of TinyThread for WIN32/POSIX is used.
The open source TinyThread implements a fairly compatible subset of thread management features.

The MessageQueue class is created in the main sceleton and works with the specified message type through a template, provides a blocking queue sorted by priority.
This class provides asynchronous messaging and read blocking until the queue is updated or until the object stops working. Supports HWM/LWM and native state event interface.

The Reader and Writer classes have a run() method, which is called from the main() in times to check the queue status.

The EventManager class provides event propagation to the objects described via the interface.

In this version, five writers are used to generate messages. Next, three readers are used and all objects work for 7 seconds. During these seconds, it is possible to check the behavior of the message queue, which is collected in the log file from the stderr stream. The reader messages also put into a standard stream.

This example prints the trace to stderr. To create a separate trace file redirect 2 (stderr) to 1 (stdout), so launch: "message_queue.exe 2>trace.txt".