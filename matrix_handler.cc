
#ifdef VOID
#undef VOID
#endif

#include "matrix_handler.h"

namespace matrixclass {

MatrixClass::MatrixClass(uint32_t n)
        : n_(n), tasks_pending(0) {
    for (int i = 0; i < 1; ++i) {
        resources_[i] = 3;
        last_buffer_[i] = 1;
    }
    initialize_buffers();
    initialize_threads();
}

MatrixClass::~MatrixClass() {
    stop_threads();
}

int MatrixClass::select_next_buffer() {
    std::lock_guard<std::mutex> lock(resource_lock_);
    uint32_t max_resource = resources_[0];
    int max_thread_id = 0;
    for (int i = 0; i < 1; ++i) {
        if (resources_[i] > max_resource) {
            max_resource = resources_[i];
            max_thread_id = i;
        }
    }
    if (max_resource <= 0) {
        std::cerr << "No available resources for any thread." << std::endl;
        return -1;
    } else {
        resources_[max_thread_id]--;
        last_buffer_[max_thread_id]++;
        uint32_t select_buffer = last_buffer_[max_thread_id] % 3;
        return select_buffer + max_thread_id;
    }
}

void* MatrixClass::get_buffer_request(int buffer_id, int thread_id) {
    return static_cast<void*>(buffers_[buffer_id + thread_id].data.inputA);
}

void* MatrixClass::get_buffer_response(int buffer_id, int thread_id) {
    return static_cast<void*>(buffers_[buffer_id + thread_id].data.result);
}

void MatrixClass::add_resource(int thread_id) {
    std::lock_guard<std::mutex> lock(resource_lock_);
    resources_[0] += 1;
}

void MatrixClass::initialize_buffers() {
    for (int i = 0; i < 3; i++) {
        matrix_buffer_t &buffer = buffers_[i];
        buffer.data.n = n_;
        buffer.data.inputA = new double[n_ * n_];
        buffer.data.inputB = new double[n_ * n_];
        buffer.data.result = new double[n_ * n_];
    }
}

int MatrixClass::get_task_id(int buffer_id) {
    return buffers_[buffer_id].data.task_id;
}

// void MatrixClass::process_request(MatrixTask::Reader taskMsg, int buffer_id, int thread_id) {
//     matrix_buffer_t &buffer = buffers_[buffer_id + thread_id];
//     buffer_id = buffer_id + thread_id;

//     if (taskMsg.getTaskId() == -1) {
//         stop_threads();
//         return;
//     }

//     buffer.data.task_id = taskMsg.getTaskId();
//     std::string opStr = taskMsg.getOps().cStr();
//     buffer.data.ops = utils::parseFunctionID(opStr);

//     auto inputA = taskMsg.getInputA();
//     auto inputB = taskMsg.getInputB();

//     KJ_IASSERT((inputA.size()/8) == n_ * n_);
//     KJ_IASSERT((inputB.size()/8) == n_ * n_);
//     memcpy(buffer.data.inputA, inputA.begin(), inputA.size());
//     memcpy(buffer.data.inputB, inputB.begin(), inputB.size());

//     {
//         std::unique_lock<std::mutex> lock(input_locks_[0]);
//         input_queue_[0].push(buffer_id);
//     }
//     input_cv_[0].notify_one();
// }

void MatrixClass::process_request(MatrixTask::Reader taskMsg, int buffer_id, int thread_id_dummy) {
    int thread_id = buffer_id / 3;
    if (taskMsg.getTaskId() == -1) {
        stop_threads();
        return;
    }
    matrix_buffer_t &buffer = buffers_[buffer_id];
    buffer.data.task_id = taskMsg.getTaskId();
    buffer.data.ops = utils::parseFunctionID(taskMsg.getOps().cStr());

    memcpy(buffer.data.inputA, taskMsg.getInputA().begin(), taskMsg.getInputA().size());
    memcpy(buffer.data.inputB, taskMsg.getInputB().begin(), taskMsg.getInputB().size());

    {
        std::unique_lock<std::mutex> lock(input_locks_[thread_id]);
        input_queue_[thread_id].push(buffer_id);
    }
    input_cv_[thread_id].notify_one();
}

int MatrixClass::check_response() {
    task_result_t *result;
    int all_id;
    {
        std::unique_lock<std::mutex> output_lock(output_lock_);
        while (tasks_pending <= 0 && !stop_flag_) {
            output_cv_.wait(output_lock, [this] { return tasks_pending > 0 || stop_flag_; });
        }
        result = &(output_queue_.front());
        all_id = result->buffer_id + result->thread_id;

        output_queue_.pop();
        tasks_pending--;
    }
    return all_id;
}

void MatrixClass::serialize_result(int buffer_id, MatrixResult::Builder& resultBuilder) {
   // std::cout << "Serializing result for buffer ID: " << buffer_id << std::endl;
    matrix_buffer_t& buffer = buffers_[buffer_id];
    resultBuilder.setTaskId(buffer.data.task_id);
    resultBuilder.setN(buffer.data.n);
    auto outputList = resultBuilder.initResult(sizeof(double) * buffer.data.n * buffer.data.n);
    KJ_IASSERT(buffer.data.result != nullptr, "buffer.data.result is null");
    int n = buffer.data.n;
    memcpy(outputList.begin(), buffer.data.result, sizeof(double) * buffer.data.n * buffer.data.n);
}

void MatrixClass::initialize_threads() {
    for (uint32_t tid = 0; tid < 1; ++tid) {
        compute_threads_.emplace_back([this, tid]() {
            bli_init();
            bli_thread_set_num_threads(3);
            bli_thread_set_ways(1, 1, 3, 1, 1);
            double alpha = 1.0, beta = 0.0;
            int count = 0, n = 0;
            long long start_time, end_time;

            while (!stop_flag_) {
                uint32_t buffer_id;
                {
                    std::unique_lock<std::mutex> lock(input_locks_[tid]);
                    while (input_queue_[tid].empty() && !stop_flag_) {
                        input_cv_[tid].wait(lock, [this, tid] { return !input_queue_[tid].empty() || stop_flag_; });
                    }
                    if (stop_flag_) break;
                    buffer_id = input_queue_[tid].front();
                    input_queue_[tid].pop();
                }

                if (count == 0) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    auto now = std::chrono::high_resolution_clock::now();
                    start_time = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
                }

                matrix_buffer_t &working_buffer = buffers_[buffer_id];
                n = working_buffer.data.n;

                if (working_buffer.data.ops == utils::FunctionID::ADDITION) {
                    for (int i = 0; i < n * n; i++) {
                        working_buffer.data.result[i] = working_buffer.data.inputA[i] + working_buffer.data.inputB[i];
                    }
                } else {
                    std::cout << "Performing matrix multiplication for buffer ID: " << buffer_id << std::endl;
                    bli_dgemm(BLIS_NO_TRANSPOSE, BLIS_NO_TRANSPOSE, n, n, n,
                              &alpha, working_buffer.data.inputA, 1, n,
                                       working_buffer.data.inputB, 1, n,
                              &beta,  working_buffer.data.result, 1, n);
                }

                int task_id;
                {
                    std::unique_lock<std::mutex> output_lock(output_lock_);
                    task_id = working_buffer.data.task_id;
                    output_queue_.push(task_result_t(tid, buffer_id, task_id));
                    tasks_pending++;
                }

                auto now = std::chrono::high_resolution_clock::now();
                end_time = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
                output_cv_.notify_one();
                count++;
            }

            long long duration_us = end_time - start_time;
            long long num_ops = count * (2 * std::pow(n, 3) - std::pow(n, 2));
            double gflops = (num_ops / duration_us) * 1e6 / 1e9;
            std::this_thread::sleep_for(std::chrono::seconds(tid));
            std::cout << "GFLOPs of thread " << tid << " is " << gflops << std::endl;
        });
    }
}

void MatrixClass::stop_threads() {
    stop_flag_ = true;
    for (int i = 0; i < 1; i++) {
        input_cv_[i].notify_all();
    }
    for (auto& thread : compute_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void MatrixClass::pin_thread_to_core(uint32_t core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

} // namespace matrixclass