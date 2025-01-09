#ifndef TASK_HANDLER_H
#define TASK_HANDLER_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include <iostream>

#include "distmult_service.pb.h"
#include "distmult_service.grpc.pb.h"
#include "utils.h"


class TaskHandler {
public:
    virtual ~TaskHandler() = default;
    virtual int select_next_buffer() = 0;
    virtual void* get_buffer_request(int buffer_id, int thread_id) = 0;
    virtual void* get_buffer_response(int buffer_id, int thread_id) = 0;
    virtual void process_request(int buffer_id, int thread_id) = 0;
    virtual int check_response() = 0;
    virtual void add_resource(int thread_id) = 0;

};

#endif  // TASK_HANDLER_H