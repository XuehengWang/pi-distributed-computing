#include "matrix_handler.h"

// using utils::FunctionID;

namespace matrixclass {


MatrixClass::MatrixClass(uint32_t n)
        : n_(n), tasks_pending(0) {
    
    // init resource count to 2
    // last buffer id = 1, so first time use buffer 0
    for (int i = 0; i < 4; ++i) {
        resources_[i] = 2;
        last_buffer_[i] = 1;
    }
    initialize_threads();

}

    MatrixClass::~MatrixClass() {
    stop_threads();
}


int MatrixClass::select_next_buffer() {
    std::lock_guard<std::mutex> lock(resource_lock_);
    uint32_t max_resource = resources_[3];
    int max_thread_id = 3;
    for (int i = 0; i < 4; ++i) {
        //std::cout << "resource from thread " << i << " is " << resources_[i] << std::endl;
        if (resources_[i] > max_resource) {
            max_resource = resources_[i];
            max_thread_id = i;
        }
    }

    //std::cout << "resource resource is: " << max_resource << std::endl;
    if (max_resource <= 0) {
        return -1;
    } else {
        
        // select a buffer
        resources_[max_thread_id] = resources_[max_thread_id] - 1;
        last_buffer_[max_thread_id] = last_buffer_[max_thread_id] + 1;
        uint32_t select_buffer = (last_buffer_[max_thread_id]) % 2;
        std::cout << "Select buffer " << select_buffer << " of compute thread " << max_thread_id << std::endl;
        
        int all_id = select_buffer * 4 + max_thread_id;
        return all_id;
        //return &(buffers_[select_buffer * 4 + max_thread_id].request);
    }

}

// MatrixRequest *get_buffer_request(int buffer_id, int thread_id) override{
void* MatrixClass::get_buffer_request(int buffer_id, int thread_id) {
    return static_cast<void*>(&buffers_[buffer_id * 4 + thread_id].request);
}

void* MatrixClass::get_buffer_response(int buffer_id, int thread_id) {
    return static_cast<void*>(&buffers_[buffer_id * 4 + thread_id].response);
}

void MatrixClass::add_resource(int thread_id) {
    std::lock_guard<std::mutex> lock(resource_lock_);
    resources_[thread_id] += 1;
}

void MatrixClass::process_request(int buffer_id, int thread_id) {
    matrix_buffer_t &buffer = buffers_[buffer_id * 4 + thread_id];
    
    MatrixRequest &request = buffer.request;

    utils::FunctionID operation = static_cast<utils::FunctionID>(request.ops());

    if (operation == utils::FunctionID::ADDITION || operation == utils::FunctionID::MULTIPLICATION) {

        //task_compute_data_t new_task(request.task_id(), request.n(), ADD); 
        //task_compute_data_t& new_task = buffer.data;

        buffer.data.n = request.n();
        buffer.data.task_id = request.task_id();
        buffer.data.ops = operation;


        // avoid input and result data copy
        google::protobuf::RepeatedField<int32_t>& input_field1 = *request.mutable_inputa();
        google::protobuf::RepeatedField<int32_t>& input_field2 = *request.mutable_inputb();
        std::cout << "buffer.data.inputA.size = " << input_field1.size() << std::endl;
        std::cout << "buffer.data.inputB.size = " << input_field2.size() << std::endl;

        int32_t* input_ptr1 = input_field1.mutable_data();
        int32_t* input_ptr2 = input_field2.mutable_data();

        // std::cout << "InputA elements: ";
        // for (int i = 0; i < input_field1.size(); ++i) {
        //     std::cout << input_ptr1[i] << " ";
        // }
        // std::cout << std::endl;

        // std::cout << "InputB elements: ";
        // for (int i = 0; i < input_field2.size(); ++i) {
        //     std::cout << input_ptr2[i] << " ";
        // }
        // std::cout << std::endl;

        buffer.data.inputA = reinterpret_cast<int32_t*>(input_ptr1);
        buffer.data.inputB = reinterpret_cast<int32_t*>(input_ptr2);
        
        std::cout << "buffer.data.inputA: " << buffer.data.inputA << std::endl;
        std::cout << "buffer.data.inputB: " << buffer.data.inputB << std::endl;
        std::cout << "n = " << buffer.data.n << std::endl;

        MatrixResponse& response = buffer.response;
        int n = buffer.data.n;
        //response.mutable_result()->Reserve(n);
        response.mutable_result()->Resize(n*n, 0);
        
        google::protobuf::RepeatedField<int32_t>& result_field = *response.mutable_result();
        int32_t* result_ptr = result_field.mutable_data();

        buffer.data.result = result_ptr; 

        std::cout << "buffer.data.result: " << buffer.data.result << std::endl;
        
        // all good
        // std::cout << "In processing data -> " << "1: " << int32_t(buffer.data.result[0]) << std::endl;
        // std::cout << "2: " << *buffer.data.result << std::endl; 
        // std::cout << "3: " << (int)*buffer.data.result << std::endl;
        //buffer.data = new_task;           
    } else {
        std::cerr << "What?? ops is " << request.ops() << std::endl;
    }
    {
        // put into queue of the assigned compute thread
        std::unique_lock<std::mutex> lock(input_locks_[thread_id]);
        input_queue_[thread_id].push(buffer_id);
    }
    input_cv_[thread_id].notify_one();
}

// int MatrixClass::check_response() {
//     // need manual unlock
//     std::cout << "task pending is " << tasks_pending << std::endl;
// //UPDATE_LAST
// check:
//     //{   
//         std::unique_lock<std::mutex> output_lock(output_lock_);
        
//     //}

//     if (tasks_pending > 0) {
//         task_result_t& result = output_queue_.front();
//         output_queue_.pop();
//         tasks_pending--;

//         output_lock.unlock();
        
//         // preapre response
//         int all_id = result.buffer_id * 4 + result.thread_id;
//         std::cout << "Prepare result... buffer_id = " << result.buffer_id << " thread_id = " << result.thread_id << std::endl;
        
//         matrix_buffer_t &buffer = buffers_[all_id];
//         MatrixResponse &response = buffer.response;

//         std::cout << "Response result[0]: " << response.result(0) << std::endl;

//         response.set_task_id(result.task_id);
//         // result should be ready in the field result
//         response.set_n(buffer.data.n);

//         // // Print the result field
//         // std::cout << "Task ID: " << result.task_id << ", n: " << buffer.data.n 
//         //         << ", Result: [";

//         // // Iterate and print the result field
//         // const auto& result_field = response.result(); // Access the repeated field
//         // for (int i = 0; i < result_field.size(); ++i) {
//         //     std::cout << result_field[i];
//         //     if (i < result_field.size() - 1) {
//         //         std::cout << ", "; // Print a comma for all except the last element
//         //     }
//         // }
//         // std::cout << "]" << std::endl;
//         return all_id;

//     } else {
//         output_cv_.wait(output_lock, [this] { return tasks_pending > 0; }); 
//         goto check;
//         //return -1;
//     }
// }

int MatrixClass::check_response() {
    task_result_t *result;
    std::cout << "task pending is " << tasks_pending << std::endl;
    int all_id;
    {
        std::unique_lock<std::mutex> output_lock(output_lock_);
        while (tasks_pending <= 0 && !stop_flag_) {
            output_cv_.wait(output_lock, [this] { return tasks_pending > 0 || stop_flag_; }); 
        }
        result = &(output_queue_.front());
        all_id = result->buffer_id * 4 + result->thread_id;
        output_queue_.pop();
        tasks_pending--;
    }
    std::cout << "stop_flag is " << stop_flag_ << std::endl;
    // assert(result->task_id != 0);

    // int all_id = result->buffer_id * 4 + result->thread_id;
             
    std::cout << "Check response... buffer_id = " << result->buffer_id << "(" <<result->task_id<< ") thread_id = " << result->thread_id << "; all id is " << all_id << std::endl;
        
    matrix_buffer_t &buffer = buffers_[all_id];
    MatrixResponse *response = &(buffer.response);

    // AHA! Catch here!
    // std::cout << "Response result[0]: " << response->result(0) << ", Set task_id " << result->task_id << std::endl;
    std::cout << "Response result[0]: " << response->result(0) << ", Set task_id " << buffer.data.task_id << std::endl;

    response->set_task_id(buffer.data.task_id);
    // result should be ready in the field result
    response->set_n(buffer.data.n);

    return all_id;
}


void MatrixClass::initialize_threads() {
    for (uint32_t tid = 0; tid < 4; ++tid) {
        compute_threads_.emplace_back([this, tid]() {
            pin_thread_to_core(tid);
            //matrix_buffer_t working_buffer;
            
            // thread loop
            while (!stop_flag_) {
                uint32_t buffer_id;
                {
                    std::unique_lock<std::mutex> lock(input_locks_[tid]);
                    while (input_queue_[tid].empty()) {
                        // wait for notify that a task is available
                        input_cv_[tid].wait(lock, [this, tid] { return !input_queue_[tid].empty(); });
                    }
                    
                    // get a task by buffer id
                    buffer_id = input_queue_[tid].front();
                    input_queue_[tid].pop();
                    std::cout << "Thread [" << tid << "]: gets a task from buffer " << buffer_id << std::endl;
                }//lock.unlock();
                
                matrix_buffer_t &working_buffer = buffers_[buffer_id * 4 + tid];
                std::cout << "Thread " << tid << " is processing task (" << working_buffer.data.task_id
                        << ") from buffer " << (int)buffer_id << std::endl;

                //std::cout << "Thread " << tid << std::endl;
                std::cout << " result[0] before = " << (int)*working_buffer.data.result << std::endl;
                //std::cout << " inputA[0] before = " << (int)*working_buffer.data.inputA << std::endl;
                //std::cout << " inputB[0] before = " << (int)*working_buffer.data.inputB << std::endl;

                // simple real computation
                //*(working_buffer.data.result) = *(working_buffer.data.input) + 1;
                int n = working_buffer.data.n;
                if (working_buffer.data.ops == utils::FunctionID::ADDITION){
                    std::cout << "ADDITION" << std::endl;
                    for (int i = 0; i < n*n; i++) {
                        //std::cout << working_buffer.data.result[i] << " = " << working_buffer.data.inputA[i] << " + " << working_buffer.data.inputB[i] << std::endl;
                        working_buffer.data.result[i] = working_buffer.data.inputA[i] + working_buffer.data.inputB[i];
                    }
                } else {

                    std::cout << "MULTIPLICATION" << std::endl;
                    for (int i = 0; i < n*n; i++) {
                        //std::cout << working_buffer.data.result[i] << " = " << working_buffer.data.inputA[i] << " * " << working_buffer.data.inputB[i] << std::endl;
                        working_buffer.data.result[i] = working_buffer.data.inputA[i] * working_buffer.data.inputB[i];
                    }
                }
                
                //simulate heavy work
                //std::this_thread::sleep_for(std::chrono::seconds(1));
                std::this_thread::sleep_for(std::chrono::microseconds(200));

                std::cout << "Thread " << tid << " results[0] = " << *(working_buffer.data.result) << std::endl;
                // put the result into output queue
                // task_result_t result(tid, buffer_id, working_buffer.data.task_id);
                int task_id;
                { 
                    std::unique_lock<std::mutex> output_lock(output_lock_);
                    task_id = working_buffer.data.task_id;
                    output_queue_.push(std::move(task_result_t(tid, buffer_id, task_id)));
                    tasks_pending++;
                    //std::cout << "Thread " << tid << " pushing" << std::endl;
   
                }
                output_cv_.notify_one(); //TODO: move out?
                std::cout << "Thread " << tid << " pushed task "<< working_buffer.data.task_id << " : " << task_id << ", tasks_pending increased to " << tasks_pending << std::endl;
            }
        });
    }
}
    
void MatrixClass::stop_threads() {
    stop_flag_ = true;
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
