#include "resource_scheduler.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>


void testResourceScheduler() {
    rpiresource::ResourceScheduler scheduler;

    // Test adding entries
    scheduler.add_entry_head(1);
    scheduler.add_entry_head(2);
    scheduler.add_entry_head(3);
    scheduler.printList();

    // Consume some resources
    int consumed = scheduler.consume_resource();
    assert(consumed == 3);
    consumed = scheduler.consume_resource();
    assert(consumed == 2);
    consumed = scheduler.consume_resource();
    assert(consumed == 1);
    consumed = scheduler.consume_resource();
    assert(consumed == 1);
    scheduler.printList();

    // Test producing resources
    scheduler.produce_resource(1);
    scheduler.produce_resource(2);
    scheduler.produce_resource(3);
    scheduler.printList();

    // Test consuming resources
    consumed = scheduler.consume_resource();
    assert(consumed == 2);
    scheduler.printList();

    consumed = scheduler.consume_resource();
    assert(consumed == 3);
    scheduler.printList();

    consumed = scheduler.consume_resource();
    assert(consumed == 3);
    scheduler.printList();

}

void consumer(rpiresource::ResourceScheduler& scheduler) {
    int next = scheduler.consume_resource();
    assert(next == 1);
    std::cout << "Consumer wakes up!" << std::endl;
}

void testSchedulerWait() {
    rpiresource::ResourceScheduler scheduler;
    scheduler.add_entry_head(1);
    scheduler.add_entry_head(2);
    
    //consumer all resources
    for (int i = 0; i < 16; i++){
        scheduler.consume_resource();
    }
    
    std::thread consumer_thread = std::thread(consumer,  std::ref(scheduler));
    std::this_thread::sleep_for(std::chrono::seconds(3));
    bool ret = scheduler.produce_resource(1);
    assert(ret);

    consumer_thread.join();
    ret = scheduler.produce_resource(1);
    assert(ret);
    ret = scheduler.produce_resource(2);
    assert(!ret);
    scheduler.printList();
    ret = scheduler.produce_resource(1);
    assert(!ret);

    
    scheduler.printList();
    
}

int main() {
    std::cout << "Test Resource Scheduler..." << std::endl;
    testResourceScheduler();
    std::cout << "Test Scheduler Wait and Signal..." << std::endl;
    testSchedulerWait();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
