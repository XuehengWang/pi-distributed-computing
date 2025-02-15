#include "matrix_handler.h"

// using utils::FunctionID;

namespace matrixclass {

MatrixClass::MatrixClass(uint32_t n)
        : n_(n), tasks_pending(0) {
    
    // init resource count to 2
    // last buffer id = 1, so first time use buffer 0
    for (int i = 0; i < 1; ++i) {
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
    uint32_t max_resource = resources_[0];
    int max_thread_id = 0;
    for (int i = 0; i < 1; ++i) {
        std::cout << "resource from thread " << i << " is " << resources_[i] << std::endl;
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
        
        int all_id = select_buffer /** * 4 **/ + max_thread_id;
        return all_id;
        //return &(buffers_[select_buffer * 4 + max_thread_id].request);
    }

}

// MatrixRequest *get_buffer_request(int buffer_id, int thread_id) override{
void* MatrixClass::get_buffer_request(int buffer_id, int thread_id) {
    return static_cast<void*>(&buffers_[buffer_id /** * 4**/ + thread_id].request);
}

void* MatrixClass::get_buffer_response(int buffer_id, int thread_id) {
    return static_cast<void*>(&buffers_[buffer_id /** * 4**/ + thread_id].response);
}

void MatrixClass::add_resource(int thread_id) {
    std::lock_guard<std::mutex> lock(resource_lock_);
    resources_[thread_id] += 1;
}

/* 
Update: preallocate the Request and eesponse messages
We first directly get the pointer to data at the begining, 
if the message size does not change later, we can avoid resizing
or reallocating memory during sending/receiving messages
*/
void MatrixClass::initialize_buffers() {
    for (int i = 0; i < 2; i++) {
        matrix_buffer_t &buffer = buffers_[i];
        MatrixRequest &request = buffer.request;

        // initialize buffer
        /* Input */
        // task_id and ops can change, but input size n is fixed now
        buffer.data.n = n_;
        google::protobuf::RepeatedField<double>& inputa = *request.mutable_inputa();
        google::protobuf::RepeatedField<double>& inputb = *request.mutable_inputb();
        inputa.Resize(n_*n_, 0.0f); //resize once
        inputb.Resize(n_*n_, 0.0f);
        double* inputa_ptr = inputa.mutable_data();
        double* inputb_ptr = inputb.mutable_data();
        buffer.data.inputA = inputa_ptr;
        buffer.data.inputB = inputb_ptr;

        /* Output */
        MatrixResponse& response = buffer.response;
        google::protobuf::RepeatedField<double>& output = *response.mutable_result();
        output.Resize(n_*n_, 0.0f);
    
        double* output_ptr = output.mutable_data();
        buffer.data.result = output_ptr;
    }
}

void MatrixClass::process_request(int buffer_id, int thread_id) {

    matrix_buffer_t &buffer = buffers_[buffer_id/** * 4**/ + thread_id];
    buffer_id = buffer_id /*** 4 **/ + thread_id;
    // matrix_buffer_t &buffer = buffers_[buffer_id * 4 + thread_id];
    
    MatrixRequest &request = buffer.request;
    if (request.task_id() == -1) {
        std::cout << "RECEIVED -1" << std::endl;
        stop_threads();
    }

    utils::FunctionID operation = static_cast<utils::FunctionID>(request.ops());

    if (operation == utils::FunctionID::ADDITION || operation == utils::FunctionID::MULTIPLICATION) {

        //task_compute_data_t& new_task = buffer.data;

        //buffer.data.n = request.n();
        
        // verify successfully
        // google::protobuf::RepeatedField<double>& inputa = *request.mutable_inputa();
        // assert(inputa.size() == (buffer.data.n)*(buffer.data.n));

        buffer.data.task_id = request.task_id();
        buffer.data.ops = operation;

/*
        // avoid input and result data copy
        google::protobuf::RepeatedField<double>& input_field1 = *request.mutable_inputa();
        google::protobuf::RepeatedField<double>& input_field2 = *request.mutable_inputb();
        // std::cout << "buffer.data.inputA.size = " << input_field1.size() << std::endl;
        // std::cout << "buffer.data.inputB.size = " << input_field2.size() << std::endl;

        double* input_ptr1 = input_field1.mutable_data();
        double* input_ptr2 = input_field2.mutable_data();
        
        buffer.data.inputA = reinterpret_cast<double*>(input_ptr1);
        buffer.data.inputB = reinterpret_cast<double*>(input_ptr2);
        
        // std::cout << "InputA elements: ";
        // for (int i = 0; i < input_field1.size(); ++i) {
        //     std::cout << input_ptr1[i] << " ";
        // }
        // std::cout << std::endl;
        
        // std::cout << "buffer.data.inputA: " << buffer.data.inputA << std::endl;
        // std::cout << "buffer.data.inputB: " << buffer.data.inputB << std::endl;
        // std::cout << "n = " << buffer.data.n << std::endl;

        MatrixResponse& response = buffer.response;
        int n = buffer.data.n;
        //response.mutable_result()->Reserve(n);
        response.mutable_result()->Resize(n*n, 0);
        
        google::protobuf::RepeatedField<double>& result_field = *response.mutable_result();
        double* result_ptr = result_field.mutable_data();

        buffer.data.result = result_ptr; 
*/
        
        // all work
        //std::cout << "buffer.data.result: " << buffer.data.result << std::endl;
        //std::cout << "In processing data -> " << "1: " << int(buffer.data.result[0]) << std::endl;
        //std::cout << "2: " << *buffer.data.result << std::endl; 
    } else {
        std::cerr << "What?? ops is " << request.ops() << std::endl;
    }
    {
        // put into queue of the assigned compute thread
        //std::unique_lock<std::mutex> lock(input_locks_[thread_id]);
        std::unique_lock<std::mutex> lock(input_locks_[0]);
        //input_queue_[thread_id].push(buffer_id);
        input_queue_[0].push(buffer_id);
    }
    input_cv_[0].notify_one();
    //input_cv_[thread_id].notify_one();
}

int MatrixClass::check_response() {
    task_result_t *result;
    std::cout << "task pending is " << tasks_pending << std::endl;
    int all_id;
    {
        std::unique_lock<std::mutex> output_lock(output_lock_);
        while (tasks_pending <= 0 && !stop_flag_) {
	   std::cout << "We are waiting" << std::endl; 
	   output_cv_.wait(output_lock, [this] { return tasks_pending > 0 || stop_flag_; }); 
        }
        result = &(output_queue_.front());
        all_id = result->buffer_id/** * 4**/ + result->thread_id;
        //all_id = result->buffer_id; //I WONDER IF THE ERROR STEMS FROM HERE???
        output_queue_.pop();
        tasks_pending--;
    }
    // assert(result->task_id != 0);

    // int all_id = result->buffer_id * 4 + result->thread_id;
             
    std::cout << "Check response... buffer_id = " << result->buffer_id << "(" <<result->task_id<< "), thread_id = " << result->thread_id << ", all id is " << all_id << std::endl;
        
    matrix_buffer_t &buffer = buffers_[all_id];
    MatrixResponse *response = &(buffer.response);

    // AHA! Catch here!
    std::cout << "Response result[0]: " << response->result(0) << ", Set task_id " << result->task_id << std::endl;
    std::cout << "Response result[0]: " << response->result(0) << ", Set task_id " << buffer.data.task_id << std::endl;

    response->set_task_id(buffer.data.task_id);
    //response->task_id = buffer.data.task_id;
    // result should be ready in the field result
    response->set_n(buffer.data.n);
    //response->n = buffer.data.n;

    return all_id;
}


void MatrixClass::initialize_threads() {
    for (uint32_t tid = 0; tid < 1; ++tid) {
        compute_threads_.emplace_back([this, tid]() {
        pin_thread_to_core(tid + 1);
            bli_init();
            //bli_thread_set_num_threads(4);
            bli_thread_set_num_threads(1);         // Set number of BLIS threads to 3
            //bli_thread_set_affinity_str("1:2:3");  // Pin BLIS threads to CPUs 1, 2, and 3
	    bli_thread_set_ways(1, 1, 1, 1, 1);
            double alpha = 1.0, beta = 0.0;
            // change to bli_dgemm(), so we do not need obj_t
            obj_t A_blis, B_blis, C_blis;

            //bool first_compute = true;
            int count = 0;
            //long long start_time;
            int n = 0;
            long long start_time, end_time;
            // thread loop
            while (!stop_flag_) {
                std::cout << "stop flag of " << tid << " is " << stop_flag_ << std::endl;
                uint32_t buffer_id;
                {
                    std::unique_lock<std::mutex> lock(input_locks_[tid]);
                    while (input_queue_[tid].empty() && !stop_flag_) {
                        // wait for notify that a task is available
                        input_cv_[tid].wait(lock, [this, tid] { return !input_queue_[tid].empty() || stop_flag_; });
                    }
                    if (stop_flag_) {
                        break;
                    }
                    // get a task by buffer id
                    buffer_id = input_queue_[tid].front();
                    input_queue_[tid].pop();
                    std::cout << "Thread [" << tid << "]: gets a task from buffer " << buffer_id << std::endl;
                }
                if (count == 0) {
                    // first_compute = false;
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    auto now = std::chrono::high_resolution_clock::now();
                    start_time = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
                }
                
                matrix_buffer_t &working_buffer = buffers_[buffer_id];
                // simple computation for test
                n = working_buffer.data.n;
                if (working_buffer.data.ops == utils::FunctionID::ADDITION){
                    //std::cout << "ADDITION" << std::endl;
                    for (int i = 0; i < n*n; i++) {
                        working_buffer.data.result[i] = working_buffer.data.inputA[i] + working_buffer.data.inputB[i];
                    }
                } else {

                    //std::cout << "MULTIPLICATION" << std::endl;
                    //std::cout << "MULTIPLICATION" << std::endl;
                    bli_dgemm(BLIS_NO_TRANSPOSE, BLIS_NO_TRANSPOSE, n, n, n,
                        &alpha, working_buffer.data.inputA, 1, n, working_buffer.data.inputB,
                        1, n, &beta, working_buffer.data.result, 1, n);
		    //std::cout << "multiplicaiton done " << std::endl;
                }
                
                //simulate heavy work
                //std::this_thread::sleep_for(std::chrono::seconds(1));
                //std::this_thread::sleep_for(std::chrono::microseconds(200));

                std::cout << "Thread " << tid << " results[0] = " << *(working_buffer.data.result) << std::endl;
                // put the result into output queue
                // task_result_t result(tid, buffer_id, working_buffer.data.task_id);
                int task_id;
                { 
                    std::unique_lock<std::mutex> output_lock(output_lock_);
                    task_id = working_buffer.data.task_id;
                    output_queue_.push(std::move(task_result_t(tid, buffer_id, task_id)));
                    tasks_pending++;
                    std::cout << "Thread " << tid << " pushing" << std::endl;
   
                }
		        auto now = std::chrono::high_resolution_clock::now();
            	end_time = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

                output_cv_.notify_one(); //TODO: move out?
                count++; 		
 		std::cout << "Thread " << tid << " pushed task "<< working_buffer.data.task_id << " : " << task_id << ", tasks_pending increased to " << tasks_pending << std::endl;
            }
            //bli_finalize();
            long long duration_us = end_time - start_time;
            
            long long num_ops = count * (2 * std::pow(n, 3) - std::pow(n, 2));
            double gflops = (num_ops / duration_us) * 1e6 / 1e9;  
            std::this_thread::sleep_for(std::chrono::seconds(tid));
            std::cout << "GFLOPs of thread " << tid << " is " << gflops << std::endl;
            std::cout << "count is " << count << std::endl;
            std::cout << "n is " << n << std::endl;
            std::cout << "num_ops is " << num_ops << std::endl;
            std::cout << "duration is " << duration_us/1e6 << std::endl;
            


        });
    }
}
    
void MatrixClass::stop_threads() {
    stop_flag_ = true;
    std::cout << "In stopping threads... " << stop_flag_ << std::endl;
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
