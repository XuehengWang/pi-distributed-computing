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

#include <blis/blis.h>

//#include <capnp/message.h>

#ifdef VOID
#undef VOID
#endif
#include "matrix.capnp.h"
#include "utils.h"

class TaskHandler {
public:
    virtual ~TaskHandler() = default;
    virtual int select_next_buffer() = 0;
    virtual void* get_buffer_request(int buffer_id, int thread_id) = 0;
    virtual void* get_buffer_response(int buffer_id, int thread_id) = 0;
    virtual int check_response() = 0;
    virtual void add_resource(int thread_id) = 0;
    virtual void initialize_buffers() = 0;
    virtual int get_task_id(int buffer_id) = 0;  // Helper to get task ID from buffer

    // Cap'n Proto specific extensions
    virtual void process_request(MatrixTask::Reader taskMsg, int buffer_id, int thread_id) = 0;
    virtual void serialize_result(int buffer_id, MatrixResult::Builder& resultBuilder) = 0;
};

#endif  // TASK_HANDLER_H
