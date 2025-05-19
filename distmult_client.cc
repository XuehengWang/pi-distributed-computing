#include "CapnpMatrixClient.h" // make sure this points to the right file
#include "utils.h"
#include "resource_scheduler.h"
#include <capnp/ez-rpc.h>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <chrono>
#include <iostream>
#include <deque>
#include <set>
#include <atomic>

class CapnpClusterManager {
public:
  CapnpClusterManager(std::vector<std::string>& addresses, int matrixSize, int submatrixSize,
                      std::vector<task_node_t*>& allTasks, std::vector<task_node_t*>& initialTasks)
    : matrixSize_(matrixSize), submatrixSize_(submatrixSize), allTasks_(allTasks), initialTasks_(initialTasks), stopFlag_(false) {

    for (size_t i = 0; i < addresses.size(); ++i) {
      clients_.emplace_back(std::make_unique<capnp::EzRpcClient>(addresses[i]));
      auto client = std::make_shared<CapnpMatrixClient>(*clients_.back(), resultQueue_, resultCv_, resultLock_, submatrixSize);
      matrixClients_[i] = client;
      resourceScheduler_.add_entry_head(i);
    }

    writerThread_ = std::thread(&CapnpClusterManager::taskWriter, this);
    readerThread_ = std::thread(&CapnpClusterManager::resultReader, this);
  }

  ~CapnpClusterManager() {
    stopFlag_ = true;
    resultCv_.notify_all();
    if (writerThread_.joinable()) writerThread_.join();
    for (auto& t : clientThreads_) {
      if (t.joinable()) t.join();
    }
    if (readerThread_.joinable()) readerThread_.join();
  }

private:
  void taskWriter() {
    while (!stopFlag_) {
      task_node_t* task = nullptr;
      {
        std::unique_lock<std::mutex> lock(taskLock_);
        if (initialTasks_.empty()) break;
        task = initialTasks_.front();
        initialTasks_.pop_front();
      }

      int targetClient = resourceScheduler_.consume_resource();
      if (task && matrixClients_.count(targetClient)) {
        task->assigned_rpi = targetClient;
        matrixClients_[targetClient]->submitTask(task);
      }
    }
  }

  void resultReader() {
    int completed = 0;
    while (completed < allTasks_.size() && !stopFlag_) {
      int taskId;
      {
        std::unique_lock<std::mutex> lock(resultLock_);
        resultCv_.wait(lock, [&] { return !resultQueue_.empty() || stopFlag_; });
        if (stopFlag_) break;
        taskId = resultQueue_.front();
        resultQueue_.pop();
      }

      task_node_t* finishedTask = nullptr;
      for (auto* task : allTasks_) {
        if (task->task_id == taskId) {
          finishedTask = task;
          resourceScheduler_.produce_resource(task->assigned_rpi);
          break;
        }
      }

      if (!finishedTask) continue;
      completed++;

      task_node_t* parent = finishedTask->parent;
      if (!parent) continue; // root node

      {
        std::lock_guard<std::mutex> lock(parentLock_);
        if (parent->left_child == finishedTask) {
          parent->left = finishedTask->result;
          parent->left_matrix = finishedTask->result_matrix;
          parent->left.active = true;
        } else if (parent->right_child == finishedTask) {
          parent->right = finishedTask->result;
          parent->right_matrix = finishedTask->result_matrix;
          parent->right.active = true;
        }

        if (parent->left.get_status() && parent->right.get_status()) {
          std::lock_guard<std::mutex> lock(taskLock_);
          initialTasks_.push_back(parent);
        }
      }

      std::cout << "Task completed: " << taskId << std::endl;
    }
  }

  int matrixSize_;
  int submatrixSize_;
  std::vector<task_node_t*>& allTasks_;
  std::deque<task_node_t*> initialTasks_;

  std::mutex taskLock_;
  std::mutex parentLock_;
  std::vector<std::unique_ptr<capnp::EzRpcClient>> clients_;
  std::map<int, std::shared_ptr<CapnpMatrixClient>> matrixClients_;
  std::vector<std::thread> clientThreads_;
  std::thread writerThread_;

  std::queue<int> resultQueue_;
  std::mutex resultLock_;
  std::condition_variable resultCv_;
  std::thread readerThread_;

  rpiresource::ResourceScheduler resourceScheduler_;
  std::atomic<bool> stopFlag_;
};

int main(int argc, char** argv) {
  if (argc < 6) {
    std::cerr << "Usage: " << argv[0] << " <matrixSize> <submatrixSize> <taskIdStart> <numRPIs> <address1> ... <addressN>\n";
    return 1;
  }

  int matrixSize = std::stoi(argv[1]);
  int submatrixSize = std::stoi(argv[2]);
  int taskIdStart = std::stoi(argv[3]);
  int numRPIs = std::stoi(argv[4]);

  std::vector<std::string> addresses;
  for (int i = 0; i < numRPIs; ++i) {
    addresses.push_back(argv[5 + i]);
  }

  std::vector<task_node_t*> allTasks;
  std::vector<task_node_t*> initialTasks;

  matrix_t* fullMatrix = new matrix_t(matrixSize);
  create_tasks(matrixSize, submatrixSize, allTasks, initialTasks, fullMatrix);

  std::cout << "Created " << allTasks.size() << " total tasks, launching..." << std::endl;
  CapnpClusterManager manager(addresses, matrixSize, submatrixSize, allTasks, initialTasks);

  return 0;
}
