#include <iostream>
#include <mutex>
#include <condition_variable>
#include <queue>

#define MAXDATASIZE 8192
#define MAXINFOSIZE 8188
#define MAX_CONNECTED_NO 10

#define REQUEST 1
#define RESPONSE 2
#define INSTRUCTION 3

#define TIME 1
#define NAME 2
#define CLIENTS 3
#define TEXT 4

struct Message
{
    std::uint8_t mode;
    std::uint8_t func;
    char data[MAXINFOSIZE];
};

union message
{
    struct Message message_;
    char data[MAXDATASIZE];
};

class message_queue
{
    std::mutex mutex;
    std::condition_variable cond_var;
    std::queue<message*> queue;
public:
    void push(message *item)
    {
        message *item_ = new message;
        *item_ = *item;
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(item_);
        cond_var.notify_one();
    }
    message* front()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cond_var.wait(lock, [&]{ return !queue.empty(); });
        return queue.front();
    }
    void pop()
    {
        std::lock_guard<std::mutex> lock(mutex);
        message *item_ = queue.front();
        delete item_;
        queue.pop();
    }
};