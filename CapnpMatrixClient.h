#pragma once

#include <capnp/ez-rpc.h>
#include "matrix.capnp.h"
#include "utils.h"
#include <mutex>
#include <queue>
#include <condition_variable>

class CapnpMatrixClient {
public:
  CapnpMatrixClient(capnp::EzRpcClient& client,
                    std::queue<int>& resultQueue,
                    std::condition_variable& resultCv,
                    std::mutex& resultMutex,
                    int submatrixSize);

  void submitTask(utils::task_node_t* task);

private:
  MatrixManager::Client stub_;
  kj::WaitScope& waitScope_;
  std::queue<int>& resultQueue_;
  std::condition_variable& resultCv_;
  std::mutex& resultMutex_;
  int submatrixSize_;
};
