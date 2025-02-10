#include "resource_scheduler.h"

namespace rpiresource {

ResourceScheduler::ResourceScheduler() : head(nullptr), tail(nullptr) {}

ResourceScheduler::~ResourceScheduler() {
    std::lock_guard<std::mutex> lock(list_lock_);
    while (head) {
        resource_t* temp = head;
        head = head->next;
        delete temp;
    }
}

//for first time joining the list, resource_count is initialized to 8
//insert to head
void ResourceScheduler::add_entry_head(int32_t rpi_id) {
    auto* new_entry = new resource_t(8, rpi_id);
    std::lock_guard<std::mutex> lock(list_lock_);

    if (!head) {
        head = new_entry;
        tail = new_entry;
    } else {
        new_entry->next = head;
        head = new_entry;
    }
}

//Consumer: get head node, consume 1 unit of resource, order list (remove it needed)
int32_t ResourceScheduler::consume_resource() {
    std::unique_lock<std::mutex> lock(list_lock_);
    while (!head) {
        list_cv_.wait(lock, [this] { return head; });
        // TODO: wait here?
        // return -1;
    } 
    //else {
        head->resource_count--;
        int32_t consumed_id = head->rpi_id;

        if (head->resource_count == 0) {
            // Remove the head node if resource count reaches 0
            resource_t* old_head = head;
            head = head->next;
            delete old_head;
            if (!head) {
                tail = nullptr;
            }
            return consumed_id;
        } 
        // check if we need to move head to some later position: resource_count in descending order
        resource_t *prev = nullptr;
        resource_t *cur = head->next;

        while (cur) {
            if (cur->resource_count > head->resource_count) {
                prev = cur;
                cur = cur->next;
            } else { //cur->resource_count <= head->resource_count, do not need to check further
                break;
            }
            
        }

        if (prev) { //we need to move head to later position
            resource_t* old_head = head;
            head = head->next;
            old_head->next = cur; // insert after last entry with resrouce_count > head
            prev->next = old_head;
            if (!old_head->next) {
                tail = old_head;
            }
        }
        return consumed_id;
    //}
}


bool ResourceScheduler::produce_resource(int32_t rpi_id_add) {
    //traverse the queue to check if this rpi_id exists
    //since only one producer thread, and it does not change the queue now
    //do we need lock during traversing the queue?
    std::unique_lock<std::mutex> lock(list_lock_);
    if (!head) {
        // signal = true; //signal writer thread
        auto* new_entry = new resource_t(1,rpi_id_add);
        head = new_entry;
        tail = new_entry;
        list_cv_.notify_one();
        return true; //empty, need to signal writer
    }
    resource_t* cur = head;
    resource_t* prev = nullptr;
    bool found = false;
    while (cur) {
        if (cur->rpi_id == rpi_id_add) {
            found = true;
            break;
        } else {
            prev = cur;
            cur = cur->next;
        }
    }
    //prev: the node before found previously (for removing found later)
    if (!found) {
        //append it to tail, with resource = 1
        auto* new_entry = new resource_t(1,rpi_id_add);
        tail->next = new_entry;
        tail = new_entry;
        return false;
    } else {
        resource_t *target = cur;
        target->resource_count++;
        if (target == head) {
            return false;    
        } 
        if (prev->resource_count >= target->resource_count) {
            //do not need to reorder queue
            return false;
        }
        // remove target first
        prev->next = target->next;  
        if (target == tail) {
            tail = prev;
        }

        // reorder the queue
        cur = head;
        // resource_t *prev = nullptr;
        resource_t *insert_prev = nullptr;
        //insert_prev: the new place that target will be inserted
        while (cur) {
            if (cur->resource_count > head->resource_count) {
                insert_prev = cur;
                cur = cur->next;
            } else {
                break;
            }
        }
        if (insert_prev) {
            target->next = insert_prev->next;
            insert_prev->next = target;
        } else { //it can be the head
            target->next = head;
            head = target;
        }   
        return false;          
    }
}

void ResourceScheduler::printList() {
    std::lock_guard<std::mutex> lock(list_lock_);

    resource_t* current = head;
    while (current) {
        std::cout << "[rpi " << current->rpi_id << ": " << current->resource_count << "] -> ";
        current = current->next;
    }
    std::cout << "NULL" << std::endl;
}

} //namespace clientresource 
