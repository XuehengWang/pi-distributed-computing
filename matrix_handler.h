#ifndef MATRIX_HANDLER_H
#define MATRIX_HANDLER_H

#include "task_handler.h"
//#include <capnp/message.h>
//#include "matrix.capnp.h"

namespace matrixclass {

struct task_compute_data_t {
    int32_t task_id;
    utils::FunctionID ops;
    uint32_t n;
    double *inputA;
    double *inputB;
    double *result;

    task_compute_data_t(int32_t task_id, uint32_t n, utils::FunctionID ops)
        : task_id(task_id), ops(ops), n(n), inputA(nullptr), inputB(nullptr), result(nullptr) {}
};

struct task_result_t {
    uint32_t thread_id;
    uint32_t buffer_id;
    int32_t task_id;
    task_result_t(uint32_t thread_id, uint32_t buffer_id, int32_t task_id)
        : thread_id(thread_id), buffer_id(buffer_id), task_id(task_id) {}
};

struct alignas(64) matrix_buffer_t {
    task_compute_data_t data;
    matrix_buffer_t() : data(-1, 0, utils::FunctionID::ADDITION) {}
};

class MatrixClass : public TaskHandler {
public:
    MatrixClass(uint32_t n);
    ~MatrixClass();

    int select_next_buffer() override;
    void* get_buffer_request(int buffer_id, int thread_id) override;
    void* get_buffer_response(int buffer_id, int thread_id) override;
    int check_response() override;
    void add_resource(int thread_id) override;
    void initialize_buffers() override;
    int get_task_id(int buffer_id) override;

    void process_request(MatrixTask::Reader taskMsg, int buffer_id, int thread_id);
    void serialize_result(int buffer_id, MatrixResult::Builder& resultBuilder);

private:
    alignas(64) matrix_buffer_t buffers_[8];
    uint32_t n_;

    std::mutex input_locks_[4];
    std::condition_variable input_cv_[4];
    std::mutex output_lock_;
    std::condition_variable output_cv_;
    std::queue<task_result_t> output_queue_;
    std::queue<uint32_t> input_queue_[4];
    std::atomic<int> tasks_pending;

    uint32_t resources_[4];
    uint32_t last_buffer_[4];
    std::mutex resource_lock_;

    std::vector<std::thread> compute_threads_;
    std::atomic<bool> stop_flag_{false};

    void initialize_threads();
    void stop_threads();
    void pin_thread_to_core(uint32_t core_id);
};

}  // namespace matrixclass

#endif  // MATRIX_HANDLER_H
