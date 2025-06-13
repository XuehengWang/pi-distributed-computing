#pragma once

#include "matrix.capnp.h"       // Replace with your actual schema
#include "utils.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

class CapnpMatrixClient {
public:
  CapnpMatrixClient(MatrixManager::Client stub,
                    int clientId,
                    std::queue<int>& resultQueue,
                    std::mutex& resultLock,
                    std::condition_variable& resultCv);

  void submitTask(utils::task_node_t* task, kj::WaitScope& waitScope);
  bool isBusy() const;
  void markFree();
  int getClientId() const;
  MatrixManager::Client& getStub();

private:
  MatrixManager::Client stub_;
  int clientId_;
  std::atomic<bool> busy_;
  std::queue<int>& resultQueue_;
  std::mutex& resultLock_;
  std::condition_variable& resultCv_;
};
