#ifndef RESOURCE_SCHEDULER_H
#define RESOURCE_SCHEDULER_H

#include <mutex>
#include <iostream>
#include <condition_variable>

namespace rpiresource {

// linked list entry
struct resource_t {
    int32_t rpi_id;
    uint32_t resource_count;
    resource_t *next;
   resource_t(uint32_t count, int32_t rpi_id) : resource_count(count), rpi_id(rpi_id), next(nullptr) {}
};

// ResourceManager class to manage resources

class ResourceScheduler {
public:
    ResourceScheduler();
    ~ResourceScheduler();

    void add_entry_head(int32_t rpi_id);   // Add a new RPI node
    int32_t consume_resource();           // Consume one unit of resource from the head
    bool produce_resource(int32_t rpi_id_add); // Add resource back to an RPI node
    void printList();                   

private:
    resource_t *head;
    resource_t *tail;
    std::mutex list_lock_;
    std::condition_variable list_cv_;
};

} //namespace

#endif 
