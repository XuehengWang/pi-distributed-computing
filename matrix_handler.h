#ifndef MATRIX_HANDLER_H
#define MATRIX_HANDLER_H

#include "task_handler.h"

using distmult::MatrixRequest;
using distmult::MatrixResponse;

namespace matrixclass {

struct task_compute_data_t {
    int32_t task_id;
    // struct matrix_1d_t *MatrixA;
    // struct matrix_1d_t *MatrixB;
    // struct matrix_1d_t *result;
    utils::FunctionID ops;
    uint32_t n;
    double *inputA;
    double *inputB;
    double *result;
    //std::vector<double> *inputA;
    //std::vector<double> *inputB;
    //std::vector<double> *result;

    task_compute_data_t(int32_t task_id, uint32_t n, utils::FunctionID ops)
        : task_id(task_id), ops(ops), n(n), inputA(nullptr),inputB(nullptr), result(nullptr) {}
};

struct task_result_t {
    uint32_t thread_id;  //0-3
    uint32_t buffer_id;  //0 or 1
    int32_t task_id; 
    task_result_t(uint32_t thread_id, uint32_t buffer_id, int32_t task_id) 
        : thread_id(thread_id), buffer_id(buffer_id), task_id(task_id) {}
};


struct alignas(64) matrix_buffer_t {
    MatrixRequest request;
    task_compute_data_t data;
    MatrixResponse response;
    //struct task_result_t result;
    matrix_buffer_t() 
        : request(), 
          data(-1, 0, utils::FunctionID::ADDITION),
          response() {
            // // response.result does not have valid memory space
            // response.mutable_result()->Reserve(n);
    }
};

class MatrixClass : public TaskHandler {
public:
    MatrixClass(uint32_t n);

    ~MatrixClass();

    int select_next_buffer() override;
    void* get_buffer_request(int buffer_id, int thread_id) override;
    void* get_buffer_response(int buffer_id, int thread_id) override;
    void process_request(int buffer_id, int thread_id) override;
    int check_response() override;
    void add_resource(int thread_id) override;
    void initialize_buffers() override;

private:
    // We will use buffer_id (0/1) and compute_thread_id to locate buffer entry
    // There should not be threads working on the same buffer
    alignas(64) matrix_buffer_t buffers_[8];
    uint32_t n_;
    /* 
    For compute threads, can move to generic class later
    */
    std::mutex input_locks_[4]; //lock for 4 input queue
    std::condition_variable input_cv_[4];
    std::mutex output_lock_; //output queue
    std::condition_variable output_cv_;
    std::queue<task_result_t> output_queue_;
    std::queue<uint32_t> input_queue_[4]; //buffer_id
    std::atomic<int> tasks_pending;

    uint32_t resources_[4]; //resource count of each compute core
    uint32_t last_buffer_[4];
    std::mutex resource_lock_;
    
    std::vector<std::thread> compute_threads_;
    std::atomic<bool> stop_flag_{false}; //init to false

    void initialize_threads();
    void stop_threads();
    void pin_thread_to_core(uint32_t core_id); 
};

}  // namespace matrixclass

#endif  // MATRIX_HANDLER_H
