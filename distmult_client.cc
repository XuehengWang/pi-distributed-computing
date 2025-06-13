// Updated CapnpMatrixClient to use thread-local AsyncIoContext
#include "CapnpMatrixClient.h"
#include "utils.h"
#include "resource_scheduler.h"
#include "AsyncContextManager.h"  // <-- Add this header for shared context
#include <capnp/rpc-twoparty.h>  // TwoPartyClient
#include <kj/async-io.h>         // setupAsyncIo()
//#include <capnp/ez-rpc.h>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <thread>
#include <chrono>
#include <iostream>
#include <deque>
#include <set>
#include <atomic>

using utils::task_node_t;
using utils::matrix_t;


class CapnpClusterManager {
public:
  CapnpClusterManager(std::vector<std::string>& addresses, int matrixSize, int submatrixSize, int task_id_start)
  : matrixSize_(matrixSize), submatrixSize_(submatrixSize),
    task_id_start_(task_id_start), task_count_(0), stop_(false) {

      initialize_matrix_tasks(matrixSize_, submatrixSize_);
      std::this_thread::sleep_for(std::chrono::seconds(2));
      int clientId = 0;
      for (size_t rpi_id = 0; rpi_id < addresses.size(); ++rpi_id) {
        std::string address = addresses[rpi_id];
        resourceScheduler_.add_entry_head(rpi_id);  // once per RPi

        for (int j = 0; j < 1; ++j) {
          int threadId = rpi_id * 1 + j;  // unique thread ID
          clientThreads_.emplace_back([this, rpi_id, clientId, address]() {
            
            auto& ctx = AsyncContextManager::getThreadLocalContext();
            auto& ioProvider = *ctx.ioProvider;
            auto& waitScope = ctx.waitScope;
            
            std::string host, portStr;
            uint port;
            
            auto idx = address.find(':');
            if (idx == std::string::npos) {
              std::cerr << "Invalid address format: " << address << std::endl;
              return;
            }
            host = address.substr(0, idx);
            portStr = address.substr(idx + 1);
            port = static_cast<uint>(std::stoi(portStr));
            
            // Connect to server
            auto addr = ioProvider.getNetwork().parseAddress(host.c_str(), port).wait(waitScope);
            auto streamOwn = addr->connect().wait(waitScope);  // kj::Own<kj::AsyncIoStream>
            kj::AsyncIoStream& stream = *streamOwn;            // dereference to get reference

            auto client = std::make_unique<capnp::TwoPartyClient>(stream);  // ✅ OK constructor

            
            // Get service capability
            MatrixManager::Client stub = client->bootstrap().castAs<MatrixManager>();
            
            // Create CapnpMatrixClient with capability stub
            auto matrixClient = std::make_shared<CapnpMatrixClient>(
                stub, clientId, resultQueue_, resultLock_, resultCv_);
            
            // Store matrixClient in shared map
            {
              std::lock_guard<std::mutex> lock(clientInitLock_);
              clients_[clientId] = matrixClient;
            }

            while (!stop_) {
              if (matrixClient->isBusy()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
              }
      
              utils::task_node_t* task = nullptr;
              {
                std::unique_lock<std::mutex> lock(taskLock_);
                taskCv_.wait(lock, [this]() {
                  std::cout << "[Waiting for tasks... InitialTasks has " << initialTasks_.size() << std::endl;
                  return !initialTasks_.empty() || stop_;
                });
      
                if (!initialTasks_.empty()) {
                  task = initialTasks_.front();
                  initialTasks_.erase(initialTasks_.begin());
                }
              }
      
              if (task) {
                int grantedRpi = resourceScheduler_.consume_resource();
                if (grantedRpi != rpi_id) {
                    std::cout << "[Thread " << clientId << "] Resource not granted for task ID: " << task->task_id
                              << ", expected RPI ID: " << rpi_id << ", got: " << grantedRpi << std::endl;
                    resourceScheduler_.produce_resource(grantedRpi);
                    continue;
                }
      
                std::cout << "[Thread " << clientId << "] Consumed resource for task ID: " << task->task_id << std::endl;
      
                if (task->task_id == -1) {
                  std::cout << "[Thread " << clientId << "] Received finalization task.\n";
                  task->task_id = -1;
                  {
                    std::lock_guard<std::mutex> lock(taskLock_);
                    on_fly_tasks_[clientId] = task;
                  }
                } else {
                  task->task_id = task_id_start_ + task_count_++;
                  {                    std::lock_guard<std::mutex> lock(taskLock_);
                    on_fly_tasks_[task->task_id] = task;
                  }
                }
      
                task->assigned_rpi = rpi_id;
                matrixClient->submitTask(task, waitScope);
              } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
              }
            }
          });
      
          clientId++;  // increment per thread
        }
      }
    readerThread_ = std::thread(&CapnpClusterManager::resultReader, this);
  }

  ~CapnpClusterManager() {
    stop_ = true;
    resultCv_.notify_all();
    for (auto& t : clientThreads_) {
      if (t.joinable()) t.join();
    }
    if (readerThread_.joinable()) readerThread_.join();
  }

private:
  void initialize_matrix_tasks(int matrix_size, int submatrix_size) {
    whole_matrix_ = new matrix_t(matrix_size);
    create_tasks(matrix_size, submatrix_size, allTasks_, initialTasks_, whole_matrix_);
    remaining_tasks_ = (matrix_size / submatrix_size) * (matrix_size / submatrix_size) / 2;

  }

  void resultReader() {
    while (!stop_) {
      int result_id;
      {
        std::unique_lock<std::mutex> lock(resultLock_);
        resultCv_.wait(lock, [this] { return !resultQueue_.empty(); });
        //std::cout << "Result reader woke up, queue size: " << resultQueue_.size() << std::endl;
        result_id = resultQueue_.front();
        resultQueue_.pop();
      }
      process_matrix_result(result_id);
    }
  }

  void process_matrix_result(int task_id) {
    std::lock_guard<std::mutex> lock(taskLock_);
    auto it = on_fly_tasks_.find(task_id);
    if (it == on_fly_tasks_.end()) return;
    task_node_t* task = it->second;

    task_node_t* parent = task->parent;
    if (!parent) {
      int subtree = task->subtree_id;
      std::cout << "Subtree " << subtree << " completed!!" << std::endl;
      results_.push(task->result_matrix);
      on_fly_tasks_.erase(task_id);
      std::cout << "Length of results queue: " << on_fly_tasks_.size() << std::endl;
      if (on_fly_tasks_.empty()) {
        resourceScheduler_.produce_resource(task->assigned_rpi);
        sendFinalizationTask();
        return;
      }
      taskCv_.notify_one();
      resourceScheduler_.produce_resource(task->assigned_rpi);
      return;
    }
    {
      std::lock_guard<std::mutex> parentLock(parentLock_);
      if (parent->left_child == task) {
        parent->left = task->result;
        parent->left_matrix = task->result_matrix;
        parent->left.active = true;
      } else if (parent->right_child == task) {
        parent->right = task->result;
        parent->right_matrix = task->result_matrix;
        parent->right.active = true;
      }

      if (parent->left.get_status() && parent->right.get_status()) {
        initialTasks_.push_back(parent);
      }
    }
    allTasks_.push_back(task);
    on_fly_tasks_.erase(task_id);
    taskCv_.notify_one();
    resourceScheduler_.produce_resource(task->assigned_rpi);
  }

  void fill_dummy_matrix(matrix_t* mat) {
    int n = mat->n;
    for (int i = 0; i < n * n; ++i) {
      mat->data[i] = 0.0;  // or 1.0, or random if needed
    }
  }

  void sendFinalizationTask() {
    std::cout << "[Manager] All matrix tasks completed. Sending final task with ID -1 to all devices.\n";
  
    for (auto& [clientId, client] : clients_) {
      if (!client) continue;
    
      auto finalTask = new task_node_t(utils::MULTIPLICATION, submatrixSize_, matrixSize_);
      finalTask->task_id = -1;
      finalTask->assigned_rpi = clientId / 2;
      finalTask->n = submatrixSize_;
  
      // Allocate and fill dummy matrices
      finalTask->left_matrix = new matrix_t(finalTask->n);
      finalTask->right_matrix = new matrix_t(finalTask->n);
      fill_dummy_matrix(finalTask->left_matrix);
      fill_dummy_matrix(finalTask->right_matrix);
  
      assert(finalTask->left_matrix && finalTask->left_matrix->data);
      assert(finalTask->right_matrix && finalTask->right_matrix->data);
  
      //std::lock_guard<std::mutex> lock(taskLock_);
      initialTasks_.push_back(finalTask);
      taskCv_.notify_one();
    }
  }

  int task_id_start_, task_count_ = 0;
  int matrixSize_, submatrixSize_;
  std::atomic<int> remaining_tasks_;
  rpiresource::ResourceScheduler resourceScheduler_;
  std::vector<std::thread> clientThreads_;
  std::thread readerThread_;
  std::mutex clientInitLock_;
  std::map<int, std::shared_ptr<CapnpMatrixClient>> clients_;


  std::vector<task_node_t*> allTasks_, initialTasks_;
  std::unordered_map<int, task_node_t*> on_fly_tasks_;
  std::queue<matrix_t*> results_;
  std::queue<int> resultQueue_;

  std::mutex taskLock_, resultLock_, parentLock_;
  std::condition_variable taskCv_, resultCv_;
  std::atomic<bool> stop_;
  matrix_t* whole_matrix_ = nullptr;
};

int main(int argc, char** argv) {
  if (argc < 6) {
    std::cerr << "Usage: " << argv[0] << " <matrixSize> <submatrixSize> <taskIdStart> <numRPIs> <address1> ... <addressN>\n";
    return 1;
  }
  //currently we don't pass through task type
  int matrixSize = std::stoi(argv[1]);
  int submatrixSize = std::stoi(argv[2]);
  int taskIdStart = std::stoi(argv[3]);
  int numRPIs = std::stoi(argv[4]);
  std::vector<std::string> addresses;
  for (int i = 0; i < numRPIs; ++i) {
    addresses.push_back(argv[5 + i]);
  }

  CapnpClusterManager manager(addresses, matrixSize, submatrixSize, taskIdStart);
  //manager.waitUntilComplete();  // implement a condition that joins threads and returns when done
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  return 0;
}
