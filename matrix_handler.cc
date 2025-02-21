#include "matrix_handler.h"

namespace matrixclass {

constexpr int NUM_THREADS = 1;//std::thread::hardware_concurrency(); // Use all available cores
constexpr int BUFFER_SIZE = 3; // Updated buffer size

// Prevent false sharing in shared structures
struct alignas(64) PaddedResource {
    std::atomic<int> value;
    char padding[64 - sizeof(std::atomic<int>)];
};
//thread_local PaddedResource resources_per_thread;
//thread_local PaddedResource last_buffer_per_thread;

MatrixClass::MatrixClass(uint32_t n)
        : n_(n), tasks_pending(0), buffer_index_(0) { //use atomic index
    
    // init resource count to 2
    // last buffer id = 1, so first time use buffer 0
    for (int i = 0; i < NUM_THREADS; ++i) {
        resources_[i] = 3;
        last_buffer_[i] = 1;
    }
    initialize_threads();

}

MatrixClass::~MatrixClass() {
    stop_threads();
}


int MatrixClass::select_next_buffer() {

    std::lock_guard<std::mutex> lock(resource_lock_);
    int selected_buffer = buffer_index_.fetch_add(1) % BUFFER_SIZE; // Round-robin selection
    int thread_id = selected_buffer % NUM_THREADS;
    if (resources_[thread_id] <= 0) {
        //return -1;
	
        std::this_thread::sleep_for(std::chrono::seconds(1));
	selected_buffer = select_next_buffer();
   }
    resources_[thread_id]--;
    return selected_buffer;
    /**std::lock_guard<std::mutex> lock(resource_lock_);
    uint32_t max_resource = resources_[0];
    int max_thread_id = 0;
    for (int i = 0; i < NUM_THREADS; ++i) {
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
        
        int all_id = select_buffer * NUM_THREADS + max_thread_id;
        return all_id;
        //return &(buffers_[select_buffer * 4 + max_thread_id].request);
    }
    **/
}

// MatrixRequest *get_buffer_request(int buffer_id, int thread_id) override{
void* MatrixClass::get_buffer_request(int buffer_id, int thread_id) {
    return static_cast<void*>(&buffers_[buffer_id * NUM_THREADS + thread_id].request);
}

void* MatrixClass::get_buffer_response(int buffer_id, int thread_id) {
    return static_cast<void*>(&buffers_[buffer_id * NUM_THREADS + thread_id].response);
}

void MatrixClass::add_resource(int buffer_id) {
    std::lock_guard<std::mutex> lock(resource_lock_);
    //resources_[thread_id] += 1;
    resources_[buffer_id]++;
    //Free up space in the ring buffer
    //
    //buffer_index_.fetch_sub(1, std::memory_order_relaxed);
}

/* 
Update: preallocate the Request and eesponse messages
We first directly get the pointer to data at the begining, 
if the message size does not change later, we can avoid resizing
or reallocating memory during sending/receiving messages
*/
void MatrixClass::initialize_buffers() {
    for (int i = 0; i < BUFFER_SIZE; i++) {
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

    matrix_buffer_t &buffer = buffers_[buffer_id * NUM_THREADS + thread_id];
    //buffer_id = buffer_id * NUM_THREADS + thread_id;
    //matrix_buffer_t &buffer = buffers_[buffer_id];
    
    //MatrixRequest &request = buffer.request;
    if (buffer.request.task_id() == -1) {
        std::cout << "RECEIVED -1" << std::endl;
        stop_threads();
    }

    utils::FunctionID operation = static_cast<utils::FunctionID>(buffer.request.ops());

    if (operation == utils::FunctionID::ADDITION || operation == utils::FunctionID::MULTIPLICATION) {

        buffer.data.task_id = buffer.request.task_id();
        buffer.data.ops = operation;
        
    } else {
        std::cerr << "What?? ops is " << buffer.request.ops() << std::endl;
    }
    {
        // put into queue of the assigned compute thread
        std::unique_lock<std::mutex> lock(input_locks_[thread_id]);
        //std::unique_lock<std::mutex> lock(input_locks_[0]);
        input_queue_[thread_id].push(buffer_id);
        //input_queue_[0].push(buffer_id);
    }
    //input_cv_[0].notify_one();
    input_cv_[thread_id].notify_one();
}

int MatrixClass::check_response() {
    //task_result_t result;
    int all_id;
    {
        std::unique_lock<std::mutex> output_lock(output_lock_);
        //while (tasks_pending <= 0 && !stop_flag_) {
	output_cv_.wait(output_lock, [this] { return tasks_pending > 0 || stop_flag_; }); 
        //}
        task_result_t result = std::move(output_queue_.front());
        all_id = result.buffer_id * NUM_THREADS + result.thread_id;
        //all_id = result.buffer_id; //I WONDER IF THE ERROR STEMS FROM HERE???
        output_queue_.pop();
        tasks_pending--;
    }
    // assert(result->task_id != 0);
    //all_id = result.buffer_id + result.thread_id;
    // int all_id = result->buffer_id * 4 + result->thread_id;
             
    matrix_buffer_t &buffer = buffers_[all_id];
    MatrixResponse *response = &(buffer.response);

    response->set_task_id(buffer.data.task_id);
    //response->task_id = buffer.data.task_id;
    // result should be ready in the field result
    response->set_n(buffer.data.n);
    //response->n = buffer.data.n;

    return all_id;
}


void MatrixClass::initialize_threads() {
    for (uint32_t tid = 0; tid < NUM_THREADS; ++tid) {
        compute_threads_.emplace_back([this, tid]() {
        //pin_thread_to_core(tid + 1);
            //bli_init();
            //bli_thread_set_num_threads(4);
            bli_thread_set_num_threads(3);         // Set number of BLIS threads to 3
            //bli_thread_set_affinity_str("1:2:3");  // Pin BLIS threads to CPUs 1, 2, and 3
	    bli_thread_set_ways(1, 1, 3, 1, 1);
            double alpha = 1.0, beta = 0.0;
            // change to bli_dgemm(), so we do not need obj_t
            obj_t A_blis, B_blis, C_blis;

            //bool first_compute = true;
            int count = 0;
            //long long start_time;
            int n = 0;
            long long start_time, start_time_t, end_time_t, end_time, mul_time;
            // thread loop
            while (!stop_flag_) {
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
                }
                if (count == 0) {
                    // first_compute = false;
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    auto now_tt = std::chrono::high_resolution_clock::now();
                    start_time_t = std::chrono::duration_cast<std::chrono::microseconds>(now_tt.time_since_epoch()).count();
                }
 
                auto now = std::chrono::high_resolution_clock::now();
                start_time = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
                matrix_buffer_t &working_buffer = buffers_[buffer_id];
                // simple computation for test
                n = working_buffer.data.n;
                if (working_buffer.data.ops == utils::FunctionID::ADDITION){
                    for (int i = 0; i < n*n; i++) {
                        working_buffer.data.result[i] = working_buffer.data.inputA[i] + working_buffer.data.inputB[i];
                    }
                } else {

                    bli_dgemm(BLIS_NO_TRANSPOSE, BLIS_NO_TRANSPOSE, n, n, n,
                        &alpha, working_buffer.data.inputA, 1, n, working_buffer.data.inputB,
                        1, n, &beta, working_buffer.data.result, 1, n);
                }
                
                //simulate heavy work
                //std::this_thread::sleep_for(std::chrono::seconds(1));
                //std::this_thread::sleep_for(std::chrono::microseconds(200));

                // put the result into output queue
                // task_result_t result(tid, buffer_id, working_buffer.data.task_id);
                int task_id;
                { 
                    std::unique_lock<std::mutex> output_lock(output_lock_);
                    task_id = working_buffer.data.task_id;
                    output_queue_.push(std::move(task_result_t(tid, buffer_id, task_id)));
                    tasks_pending++;
   
                }
		auto now_t = std::chrono::high_resolution_clock::now();
            	end_time_t = std::chrono::duration_cast<std::chrono::microseconds>(now_t.time_since_epoch()).count();
		mul_time += end_time_t - start_time;
                output_cv_.notify_one(); //TODO: move out?
                count++; 		
            }
            //bli_finalize();
            long long duration_us = end_time_t - start_time_t;
            
            long long num_ops = count * (2 * std::pow(n, 3) - std::pow(n, 2));
            double gflops = (num_ops / duration_us) * 1e6 / 1e9;  
            std::this_thread::sleep_for(std::chrono::seconds(tid));
            std::cout << "GFLOPs of thread " << tid << " is " << gflops << std::endl;
            std::cout << "count is " << count << std::endl;
            std::cout << "n is " << n << std::endl;
            std::cout << "num_ops is " << num_ops << std::endl;
            std::cout << "duration is " << duration_us/1e6 << std::endl;
            
            std::cout << "multiplication duration is " << mul_time/1e6 << std::endl;


        });
    }
}
    
void MatrixClass::stop_threads() {
    stop_flag_ = true;
    std::cout << "In stopping threads... " << stop_flag_ << std::endl;
    for (int i = 0; i < NUM_THREADS; i++) {
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
