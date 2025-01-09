#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include "TaskQueue.h"

void producer(TaskQueue<int>& queue, int start, int count) {
    for (int i = 0; i < count; ++i) {
        int value = start + i;
        queue.push(value);
    }
}

void consumer(TaskQueue<int>& queue, std::vector<int>& consumed, int consume_count) {
    for (int i = 0; i < consume_count; ++i) {
        int value = queue.pop();
        consumed.push_back(value);
    }
}

int main() {
    TaskQueue<int> queue;

    int num_produce = 10;
    int num_consumers = 2;
    int consume_count_per_thread = num_produce / num_consumers;

    std::vector<int> results1, results2;

    std::thread producer_thread(producer, std::ref(queue), 0, num_produce);
    std::thread consumer_thread1(consumer, std::ref(queue), std::ref(results1), consume_count_per_thread);
    std::thread consumer_thread2(consumer, std::ref(queue), std::ref(results2), consume_count_per_thread);

    producer_thread.join();
    consumer_thread1.join();
    consumer_thread2.join();

    results1.insert(results1.end(), results2.begin(), results2.end());
    std::sort(results1.begin(), results1.end());

    for (int i = 0; i < num_produce; ++i) {
        assert(results1[i] == i && "Mismatch in results!");
    }

    std::cout << "All tests passed!" << std::endl;
    return 0;
}
