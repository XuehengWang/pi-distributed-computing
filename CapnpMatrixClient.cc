#include "CapnpMatrixClient.h"
#include <iostream>
#include <cstring>  // for memcpy

CapnpMatrixClient::CapnpMatrixClient(MatrixManager::Client stub,
                                     int clientId,
                                     std::queue<int>& resultQueue,
                                     std::mutex& resultLock,
                                     std::condition_variable& resultCv)
  : stub_(stub),
    clientId_(clientId),
    busy_(false),
    resultQueue_(resultQueue),
    resultLock_(resultLock),
    resultCv_(resultCv) {}

bool CapnpMatrixClient::isBusy() const {
  return busy_;
}

void CapnpMatrixClient::markFree() {
  busy_ = false;
}

int CapnpMatrixClient::getClientId() const {
  return clientId_;
}

MatrixManager::Client& CapnpMatrixClient::getStub() {
  return stub_;
}

void CapnpMatrixClient::submitTask(utils::task_node_t* task, kj::WaitScope& waitScope) {
  if (busy_) {
    std::cerr << "[Client " << clientId_ << "] Busy, skipping task.\n";
    return;
  }

  busy_ = true;

  auto req = stub_.submitTaskRequest();
  auto taskMsg = req.initTask();

  taskMsg.setTaskId(task->task_id);
  taskMsg.setOps("MULTIPLICATION");
  taskMsg.setN(task->n);

  taskMsg.setInputA(kj::arrayPtr(
      reinterpret_cast<const capnp::byte*>(task->left_matrix->data),
      sizeof(double) * task->n * task->n));

  taskMsg.setInputB(kj::arrayPtr(
      reinterpret_cast<const capnp::byte*>(task->right_matrix->data),
      sizeof(double) * task->n * task->n));

  try {
    std::cout << "[Client " << clientId_ << "] Sending task ID: " << task->task_id << std::endl;
    auto resp = req.send().wait(waitScope);
    std::cout << "[Client " << clientId_ << "] Response received.\n";

    if (!resp.hasResult()) {
      std::cerr << "[Client " << clientId_ << "] No result in response.\n";
      busy_ = false;
      return;
    }

    auto resultMsg = resp.getResult();
    std::cout << "[Client " << clientId_ << "] Got result for taskId: "
              << resultMsg.getTaskId() << std::endl;

    // Allocate and copy result matrix
    task->result_matrix = new utils::matrix_t(task->n);
    memcpy(task->result_matrix->data,
           resultMsg.getResult().begin(),
           sizeof(double) * task->n * task->n);

    {
      std::lock_guard<std::mutex> lock(resultLock_);
      resultQueue_.push(task->task_id);
    }
    resultCv_.notify_one();

  } catch (const kj::Exception& e) {
    std::cerr << "[Client " << clientId_ << "] Exception in submitTask: "
              << e.getDescription().cStr() << std::endl;
  }

  markFree();
}
