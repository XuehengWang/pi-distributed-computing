#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <cstring> //memcpy
#include <cstdlib>
#include <unordered_map>
#include <chrono>
#include <queue>

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"

#include <grpc/grpc.h>
#include <grpcpp/alarm.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "distmult_service.pb.h"
#include "distmult_service.grpc.pb.h"
// #include "function_map.h"

#include "utils.h"
#include "resource_scheduler.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using distmult::DistMultService;
using distmult::MatrixRequest;
using distmult::MatrixResponse;

using utils::matrix_t;
using utils::task_node_t;
using utils::FunctionID;
using utils::random_int;
using rpiresource::ResourceScheduler;
using utils::Submatrix;

class DistMultClient {
 public:
  DistMultClient(std::shared_ptr<Channel> channel, std::queue<int>& result_queue, std::condition_variable& result_cv, std::mutex& result_lock)
      : stub_(DistMultService::NewStub(channel)), result_queue_(result_queue), result_cv_(result_cv), result_lock_(result_lock) {}
  
  void ComputeMatrix() {
    class ComputeRPC : public grpc::ClientBidiReactor<MatrixRequest, MatrixResponse> {
    public:
      explicit ComputeRPC(DistMultService::Stub* stub, std::queue<int>& result_queue, std::condition_variable& result_cv, std::mutex& result_lock, std::unordered_map<int, task_node_t*>& tasks
      , std::mutex& task_lock, std::condition_variable& task_cv, std::queue<int>& task_queue)
          : result_queue_(result_queue), result_cv_(result_cv), result_lock_(result_lock), on_fly_tasks(tasks), task_queue_(task_queue), task_cv_(task_cv), task_lock_(task_lock){
        
        stub->async()->ComputeMatrix(&context_, this);
        StartCall();
        LOG(INFO) << "Client RPC: RPC Call initiated";
        
        // writer_thread_ = std::thread(&ComputeRPC::writer, this);
        NextWrite();
        LOG(INFO) << "Client RPC: start writer and reader threads...";
        StartRead(&response_);
      }

      void OnWriteDone(bool ok) override {
        if (ok) {
          NextWrite();
          //LOG(info) << "Write done!"
        }
      }
        
      void OnReadDone(bool ok) override {
        // LOG(INFO) << "Current thread ID: " << std::this_thread::get_id();

        if (ok) {
          // task_id = 1, rpi_id, n, result
          int rpi_id;
          int task_id;
          int n;
          // get task
          task_node_t* task;
          {
            // std::unique_lock<std::mutex> lock(mu_);
            task_id = response_.task_id();
            
            n = response_.n();
            auto it = on_fly_tasks.find(task_id);
            //LOG(INFO) << (it==on_fly_tasks.end());
            task = it->second;
            rpi_id = task->assigned_rpi;
            on_fly_tasks.erase(task_id);
          }
         
          LOG(INFO) << "[ Client RPI " << rpi_id << " ] Received response for task_id: " << task_id
          << " with n: " << n << std::endl;
          
          //first push rpi_id to update resource
          {
            std::unique_lock<std::mutex> lock(result_lock_);
            result_queue_.push(rpi_id);
          }
          result_cv_.notify_one();

          // save output & send back task_id afterward
          //convertRepeatedToPtr(response_, task->result);
          matrix_t *mat = new matrix_t(response_);       
          task->result = Submatrix(0, 0, n);         
          task->result_matrix = mat;

          {
            std::unique_lock<std::mutex> lock(result_lock_);
            result_queue_.push(task_id);
          }
          result_cv_.notify_one();

          // on_fly_tasks.erase(task_id); 
          // LOG(INFO) << "ggg";
          StartRead(&response_);
        }

      }
        
      void Stop() {
        StartWritesDone();
        // LOG(INFO) << "Stop!!";
      }

      void OnDone(const Status& s) override {
        std::unique_lock<std::mutex> l(mu_);
        status_ = s;
        done_ = true;
        cv_.notify_one();
      }

      Status Await() {
        std::unique_lock<std::mutex> l(mu_);
        cv_.wait(l, [this] { return done_; });
        //writer_thread_.join();
        LOG(INFO) << "Finally! Client Done is " << done_;
        return std::move(status_);
      }

    private:
      void writer() {
        NextWrite();
      }

      void NextWrite() {
        int task_id;
        task_node_t *task;
        {
          std::unique_lock<std::mutex> lock(task_lock_); 
          while (task_queue_.empty()) { 
            task_cv_.wait(lock, [this] { return !task_queue_.empty() || done_; });
          }
          if (done_) {
            Stop();
            return;
          }
          task_id = task_queue_.front();
          // LOG(INFO) << "task queue is " << task_queue_.size() << ", task id is " << task_id;
          task_queue_.pop();
          if (task_id == -1) { //stop
            Stop();
            return;
          }
          auto it = on_fly_tasks.find(task_id);
          // LOG(INFO) << "found?";
          task = it->second;
        }

        // std::queue<int> temp_queue = task_queue_;  
        // std::cout << "Queue contents: ";
        // while (!temp_queue.empty()) {
        //     std::cout << temp_queue.front() << " ";
        //     temp_queue.pop();
        // }
        // std::cout << std::endl;

        // get task: move to above
        // if (it != on_fly_tasks.end()) {
        // task_node_t* task = it->second;
        create_request(task->task_id, task->ops, task->n, task->left, task->right, task->left_matrix, task->right_matrix);
        
        LOG(INFO) << "[ Client RPI " << task->assigned_rpi << " ] Sending request " << request_.task_id() << " with ops " << task->ops << ", input A[0] = " << request_.inputa()[0];
        // LOG(INFO) << "input A[n^2-1] = " << request_.inputa()[request_.n()*request_.n()-1];
        StartWrite(&request_);
      }

      void create_request(int task_id, FunctionID ops, int n, Submatrix subA, Submatrix subB, matrix_t *inputA, matrix_t *inputB) {
          request_.set_task_id(task_id);  
          request_.set_ops(ops);      
          request_.set_n(n); 
          request_.clear_inputa();
          request_.clear_inputb();
          // LOG(INFO) << "n = " << n << ", subA = "<< subA.row_start << ", ";  

          inputA->get_submatrix_data(subA, request_, "inputa");
          inputB->get_submatrix_data(subB, request_, "inputb");

          // for (size_t i = 0; i < n*n; ++i) {
          //   request_.add_inputa(inputA[i]);  
          // }
          // for (size_t i = 0; i < n*n; ++i) {
          //   request_.add_inputb(inputB[i]);  
          // }
          // return request;
      }

      // void convertRepeatedToPtr(const MatrixResponse& msg, int*& result_array, size_t& result_size) {
      //     const google::protobuf::RepeatedField<int32_t>& repeated_result = msg.result();
      //     result_size = repeated_result.size();
      //     result_array = new int[result_size];
          
      //     LOG(INFO) << "CONVERTED... result size is " << result_size;
      //     for (size_t i = 0; i < result_size; ++i) {
      //         LOG(INFO) << repeated_result[i];
      //         result_array[i] = repeated_result[i];
      //     }
      // }

      ClientContext context_;
      MatrixResponse response_;
      MatrixRequest request_;

      std::thread writer_thread_;

      std::mutex& result_lock_;
      std::condition_variable& result_cv_;
      std::queue<int>& result_queue_;

      std::unordered_map<int, task_node_t*>& on_fly_tasks;

      std::mutex& task_lock_;
      std::condition_variable& task_cv_;
      std::queue<int>& task_queue_;

      // for overall status
      std::mutex mu_;
      std::condition_variable cv_;
      Status status_;
      bool done_ = false;
    };

    ComputeRPC chatter(stub_.get(), result_queue_, result_cv_, result_lock_, on_fly_tasks, task_lock_, task_cv_, task_queue_);
    //std::shared_ptr<ComputeRPC> rpc_handle = std::make_shared<ComputeRPC>(stub_.get(), result_queue_, result_cv_, result_lock_);
    // LOG(INFO) << "Client thread await..."; 
    //ComputeRPC *handle = new ComputeRPC(stub_.get());
    Status status = chatter.Await();
    // if (!status.ok()) {
    //   std::cout << "Client rpc failed." << std::endl;
    // }
  }

  void Stop() {
    LOG(INFO) << "Client thread stop..."; 
    {
      std::unique_lock<std::mutex> lock(task_lock_);
      task_queue_.push(-1);
    }
    task_cv_.notify_one();
    // chatter.Stop();
  }

  //put the task into map for writer thread
  void add_task(task_node_t*& task) {
    int task_id = task->task_id;
    {
      std::unique_lock<std::mutex> lock(task_lock_);
      on_fly_tasks[task_id] = task;
      task_queue_.push(task_id);
    }
    task_cv_.notify_one();
    LOG(INFO) << "[ Client RPI " << task->assigned_rpi << " ]: add_task " << task->task_id;
    
    // std::queue<int> temp_queue = task_queue_;
    // std::cout << "Queue contents: ";
    // while (!temp_queue.empty()) {
    //     std::cout << temp_queue.front() << " ";
    //     temp_queue.pop();
    // }
    // std::cout << std::endl;
  }

  private:
  std::unique_ptr<DistMultService::Stub> stub_;
  std::queue<int>& result_queue_;
  std::condition_variable& result_cv_;
  std::mutex& result_lock_;
  std::unordered_map<int, task_node_t*> on_fly_tasks;
  std::mutex task_lock_;
  std::condition_variable task_cv_;
  std::queue<int> task_queue_;
};


class ClusterManager {
public:
    ClusterManager(std::string &task_type, int num_rpi, std::vector<std::string>& addresses): num_rpi_(0) {
      // LOG(INFO) << task_type;
      if (task_type == "matrix") {
        
        initialize_matrix_rpc(num_rpi, addresses);

        matrix_size_ = 1024; // fixed now
        submatrix_size_ = 128;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        initialize_matrix_tasks(matrix_size_, submatrix_size_);
        
      } else {
        std::cerr << "Error: Unsupported task type \"" << task_type << "\". Supported: \"matrix\".\n";
      }
      
      initialize_secondary_resources();
       
      //initialize_matrix_results();

      // start threads
      LOG(INFO) << "ClusterManager: Start reader and writer threads";
      writer_thread_ = std::thread(&ClusterManager::writer, this);
      reader_thread_ = std::thread(&ClusterManager::reader, this);
      random_id_ = 100;
    }
  
  ~ClusterManager() {
    if (writer_thread_.joinable()) {
      writer_thread_.join();
      LOG(INFO) << "Writer joined!";
    }
    if (reader_thread_.joinable()) {
      reader_thread_.join();
      LOG(INFO) << "Reader joined!";
    }

    for (std::thread& t : client_threads_) {
        if (t.joinable()) {
            t.join();
        }
        LOG(INFO) << "All client joined!";
    }
    client_map.clear();
  }

private:
    // just a place holder for results
    // void initialize_matrix_results(){
    //   for (size_t i = 0; i < (matrix_size_/submatrix_size_)*(matrix_size_/submatrix_size_); ++i) {
    //         //results_.push_back(matrix_t(submatrix_size_)); // Create and add a 3x3 matrix as an example
    //       results_.push(std::move(matrix_t(submatrix_size_)));
    //     }
    // }

    //initialize all DistMultClient for matrix task
    void initialize_matrix_rpc(int num_rpi_total, std::vector<std::string>& addresses) {
      
      for (int i = 0; i < num_rpi_total; i++) {

        std::string address = addresses[i];
        // create_matrix_rpc(num_rpi_, addresses[i]);
        std::thread rpc_thread([this, address, i]() {
            LOG(INFO) << "Started RPC for address: " << address;
            std::shared_ptr<DistMultClient> client = std::make_shared<DistMultClient>(
              grpc::CreateChannel(address, grpc::InsecureChannelCredentials()),
              result_queue_, result_cv_, result_lock_);
            client_map[i] = client;
            LOG(INFO) << "Client RPC started for RPI_id: " << i;
            client->ComputeMatrix();
            // UPDATE: should wait until the client rpc is done
            // create_matrix_rpc(num_rpi_, address); 
        });

        {
          std::lock_guard<std::mutex> lock(thread_mutex_);
          client_threads_.emplace_back(std::move(rpc_thread)); 
        }

        num_rpi_++;
      }
    }

    // // Start RPC session
    // void create_matrix_rpc(int rpi_id, const std::string& server_address) {
    //   // DistMultClient client(
    //   // grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()),
    //   // result_queue_, result_cv_, result_lock_);
    //   std::shared_ptr<DistMultClient> client = new std::shared_ptr<DistMultClient>(grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()),
    //   result_queue_, result_cv_, result_lock_);

    //   //ComputeRPC* rpc_handle = client.ComputeMatrix();
     
    //   // std::shared_ptr<ComputeRPC> rpc_handle = client.ComputeMatrix();
    //   client_map[rpi_id] = rpc_handle;
    //   LOG(INFO) << "Client RPC started for RPI_id: " << rpi_id;
    // }

    void initialize_matrix_tasks(int matrix_size, int submatrix_size) {
      whole_matrix_ = new matrix_t(1024); //initialize matrix data in 2D format
      //whole_matrix_->print_matrix();
      create_tasks(matrix_size, submatrix_size, all_tasks_, initial_tasks_, whole_matrix_);
      //LOG(INFO) << "Created " << all_tasks_.size() << " in total, " << "starting with " << initial_tasks_.size() << " multiplications...";
      std::cout << "Created " << all_tasks_.size() << " in total, " << "starting with " << initial_tasks_.size() << " multiplications..." << std::endl;
      remaining_tasks_ = (matrix_size/submatrix_size) *  (matrix_size/submatrix_size);//subtrees
      //remaining_tasks_ = all_tasks_.size();
    }

    void initialize_secondary_resources(){
      for (int i = 0; i < num_rpi_; i++) {
        resource_scheduler.add_entry_head(i);
      }
      LOG(INFO) << "Initialized all RPIs' resources: ";
      resource_scheduler.printList();
    }


    void writer() {
      while (!stop_) {
        task_node_t* select_task;
        {
          std::unique_lock<std::mutex> lock(task_lock_); 
          while (initial_tasks_.empty() && !stop_) { 
            task_cv_.wait(lock, [this] { return !initial_tasks_.empty() || stop_; });
          }

          if (stop_) {
            break;
          }
          select_task = initial_tasks_.front();
          // LOG(INFO) << "selected task is " << select_task;
          initial_tasks_.erase(initial_tasks_.begin()); // TODO: pop()
        }

        // get resource (select secondary node)
        int select_rpi = resource_scheduler.consume_resource();
        auto it = client_map.find(select_rpi);
        if (it != client_map.end()) {
          std::shared_ptr<DistMultClient> client = it->second;
          // LOG(INFO) << "Found RPC Client with rpi_id " << select_rpi;
          
          // task_id + assigned_rpi
          select_task->assigned_rpi = select_rpi;

          //int task_id = random_int(1000,100000);
          int task_id = 100 + random_id_ % (num_rpi_ * 20);
          random_id_++;
          select_task->task_id = task_id;
          LOG(INFO) << "Writer: Selected task " << select_task << ", assigned to " << select_rpi << " with task_id = " << select_task->task_id;
          
          // int* buffer = new int[submatrix_size_ * submatrix_size_];
          // select_task->result = buffer;

          on_fly_tasks[task_id] = select_task;

          // // prepare the request?
          // LOG(INFO) << "Finish preparing the task " << task_id << " on Manager, sending to RPC Client...";
          // // notify the secondary to work for task
          client->add_task(select_task); //task_node_t
        } else {
          LOG(ERROR) << "Client with rpi_id " << select_rpi << " not found!" << std::endl;
          for (const auto& pair : client_map) {
              std::cout << pair.first << ": " << pair.second << std::endl;
          }

        }
        
      }
    }
        
    void reader() {
      while (!stop_) {
        int result_to_process;
        {
          std::unique_lock<std::mutex> lock(result_lock_);
          while (result_queue_.empty()) { 
            result_cv_.wait(lock, [this] { return !result_queue_.empty();}); 
          }
          result_to_process = result_queue_.front();
          //result_queue_.erase(result_queue_.begin()); //TODO: optimize pop
          result_queue_.pop();
        }

        if (result_to_process <= num_rpi_) { //it's rpi_id, we will release resource
          resource_scheduler.produce_resource(result_to_process);
          continue;
        } else { //it's a task id, we will process real result
          process_matrix_result(result_to_process);

          // remaining_tasks_--;

          
          //LOG(INFO) << "Cluster: remaining tasks becomes " << remaining_tasks_;
          std::cout << "Finish: " << result_to_process << std::endl;

          //all tasks are done
          if (remaining_tasks_.load(std::memory_order_relaxed) == 0) {
            LOG(INFO) << "Reader: set stop to true";
            {
              std::unique_lock<std::mutex> lock(task_lock_); 
              stop_ = true;
            }
            task_cv_.notify_one();
            clean_up();
          }
        }
      }
    }

    void process_matrix_result(int task_id) {
      LOG(INFO) << "Cluster: (processing result...) on_fly_tasks length is " << on_fly_tasks.size();
      std::cout << "Cluster: (processing result...) on_fly_tasks length is " << on_fly_tasks.size() << std::endl;
      auto it = on_fly_tasks.find(task_id);
      if (it != on_fly_tasks.end()) {
        task_node_t* task = it->second;
        LOG(INFO) << "Retrieved task with task_id " << task_id << ", processing result...";
        //on_fly_tasks.erase(task_id);

        // convert result to matrix_t
        // matrix_t mat(matrix_t(submatrix_size_, task->result));
        
        // matrix_t mat* = new matrix_t(submatrix_size_, task->result);
        // std::shared_ptr<matrix_t> mat = std::make_shared<matrix_t>(submatrix_size_, task->result);

        // position of the result matrix
        int subtree = task->subtree_id;
       LOG(INFO) << "Received result of subtree " << subtree;
       std::cout << "Received result of subtree " << subtree << std::endl;
        //mat.print_matrix();
        //mat->print_matrix();

        // store temporary subtree to result 
        // results_[subtree] = mat;

        // size_t result_row_start = task->subtree_id % (matrix_size_/submatrix_size_);
        // size_t result_col_start = task->subtree_id / (matrix_size_/submatrix_size_);
        

        //intermediates_.push(std::move(matrix_t(submatrix_size_)));
        //int *result_matrix_pt = intermediates_.front().get_submatrix(0, 0, submatrix_size_);
        

        // get its parent
        task_node_t *parent = task->parent;
        if (!parent) { //root of subtree
          LOG(INFO) << "Subtree " << subtree << " completed!!";
          std::cout << "Subtree " << subtree << " completed!!" << std::endl;
          remaining_tasks_.fetch_sub(1, std::memory_order_relaxed);
          LOG(INFO) << "Remaining tasks becomes " << remaining_tasks_;
          std::cout << "Remaining tasks becomes " << remaining_tasks_ << std::endl;
          //results_.push(std::move(mat));
          results_.push(task->result_matrix);
         
          on_fly_tasks.erase(task_id);
          return;
        }
        
        task_node_t *left_child_ptr = parent->left_child;
        task_node_t *right_child_ptr = parent->right_child;
        
        //if (left_child_ptr == task.get()) {
        
        bool push = false;
        {
          std::lock_guard<std::mutex> lock(parent_lock_);
        
          if (left_child_ptr == task) {
            std::cout << "This task is a left child of its parent :)" << std::endl;
            LOG(INFO) << "This task is a left child of its parent :)";
            parent->left = task->result;
            parent->left_matrix =task->result_matrix;
            parent->left.active = true;
          } else if (right_child_ptr == task) {
            std::cout << "This task is a right child of its parent :)" << std::endl;
            LOG(INFO) << "This task is a right child of its parent :)";
            parent->right = task->result;
            parent->right_matrix =task->result_matrix;
            parent->right.active = true;
          } else {
            std::cout << "What? Not a child of its parent :(" << std::endl;
            LOG(ERROR) << "What? Not a child of its parent :(";
          }
          if (parent->left.get_status() && parent->right.get_status()) {
            push = true;
          }
        }

        // check if parent can be added to the task queue
        if (push) {
          std::cout << "Done both children -> Add parent to the task queue!" << std::endl;
          LOG(INFO) << "Done both children -> Add parent to the task queue!";
          {
            std::unique_lock<std::mutex> lock(task_lock_);
            //std::shared_ptr<task_node_t> parent_ptr(parent);
            initial_tasks_.push_back(parent);
            task_cv_.notify_one();
          }
        }
        
        //task->result_matrix->print_matrix();TODO
        all_tasks_.push_back(task); //TODO
        on_fly_tasks.erase(task_id);

      } else {
        std::cout << "Cannot find task_id: " << task_id << std::endl;
        LOG(INFO) << "Cannot find task_id: " << task_id;
      }
    }

    void clean_up() {
      LOG(INFO) << "In clean-up...";
      assert(initial_tasks_.empty());
      assert(on_fly_tasks.size() == 0);
      LOG(INFO) << "ALL RESULTS:";

      // while (!results_.empty()) {
      //   results_.front()->print_matrix(); // Call print() for the front element
      //   results_.pop();           // Remove the element after printing
      // }

      //whole_matrix_->print_matrix();

      task_cv_.notify_all();  
      // stop & close for all client rpc
      for (auto& [key, rpc] : client_map) {
        if (rpc) {
          rpc->Stop();
        }
      }
    }

    int random_id_;
    int matrix_size_;
    int submatrix_size_;
    std::mutex parent_lock_;

    int num_rpi_;
    // int remaining_tasks_; //(1024/128)^2
    std::atomic<int> remaining_tasks_{0};
    matrix_t *whole_matrix_;
    //std::vector<std::shared_ptr<matrix_t>> results_;
    std::queue<matrix_t*> results_;


    /* Tasks */
    // store all tasks in FIFO order they created
    // so the tasks in the front of the queue should not depend on later tasks
    // nevermind, the task depending on others will not be added initially
    std::vector<task_node_t*> all_tasks_;
    std::vector<task_node_t*> initial_tasks_;

    std::unordered_map<int, task_node_t*> on_fly_tasks;

    /* Computation unit of each RPI */
    ResourceScheduler resource_scheduler;
    
    /* Map: rpi_id -> corresponding rpc client */
    std::unordered_map<int, std::shared_ptr<DistMultClient>> client_map;

    std::thread reader_thread_;
    std::thread writer_thread_;
    std::condition_variable task_cv_;
    std::mutex task_lock_;

    /* For RPC clients to send results */
    std::queue<int> result_queue_; //smaller numbers of rpi_id (< num_rpi), others are task_id (> 100 now)
    std::mutex result_lock_;
    std::condition_variable result_cv_;

    std::atomic<int> stop_{false};
    std::mutex thread_mutex_;
    std::vector<std::thread> client_threads_;
    //std::queue<matrix_t> intermediates_; // store intermediate results
};

int main(int argc, char** argv) {

  absl::ParseCommandLine(argc, argv);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <task_type> <num_RPI> <address_1> ... <address_n>\n";
    return EXIT_FAILURE;
  }

  std::string task_type = argv[1];
  int num_RPI = std::stoi(argv[2]); // now we use fixed size of RPI workers

  if (argc != (3 + num_RPI)) {
    std::cerr << "Error: Number of addresses provided does not match num_RPI.\n";
    return EXIT_FAILURE;
  }

  std::vector<std::string> addresses;
  for (int i = 0; i < num_RPI; ++i) {
    std::string address = argv[3 + i];
    addresses.push_back(address);
    // manager.create_client(i, address);
  }

  ClusterManager manager(task_type, num_RPI, addresses);
  return EXIT_SUCCESS;
}
