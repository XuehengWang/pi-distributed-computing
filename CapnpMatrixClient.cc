#include "CapnpMatrixClient.h"
#include <kj/debug.h>  // for KJ_REQUIRE or KJ_ASSERT

CapnpMatrixClient::CapnpMatrixClient(capnp::EzRpcClient& client,
                                     std::queue<int>& resultQueue,
                                     std::condition_variable& resultCv,
                                     std::mutex& resultMutex,
                                     int submatrixSize)
    : stub_(client.getMain<MatrixManager>()),
      waitScope_(client.getWaitScope()),
      resultQueue_(resultQueue),
      resultCv_(resultCv),
      resultMutex_(resultMutex),
      submatrixSize_(submatrixSize) {}

void CapnpMatrixClient::submitTask(utils::task_node_t* task) {
  auto req = stub_.submitTaskRequest();

  auto matrixTask = req.initTask();
  matrixTask.setTaskId(task->task_id);
  matrixTask.setOps("MULTIPLICATION");  // use the same strings as your enum parser
  matrixTask.setN(task->n);

  const double* data = task->left_matrix->data;
  kj::ArrayPtr<const capnp::byte> inputA(
      reinterpret_cast<const capnp::byte*>(data),
      sizeof(double) * task->n * task->n);
  matrixTask.setInputA(inputA);

  data = task->right_matrix->data;
  kj::ArrayPtr<const capnp::byte> inputB(
      reinterpret_cast<const capnp::byte*>(data),
      sizeof(double) * task->n * task->n);
  matrixTask.setInputB(inputB);

  req.send().then([&, task](capnp::Response<MatrixManager::SubmitTaskResults> result) {
    const auto& r = result.getResult();
    std::lock_guard<std::mutex> lock(resultMutex_);
    task->task_id = r.getTaskId();
    task->n = r.getN();

    const auto resultData = r.getResult();
    size_t bytes = resultData.size();
    size_t expectedBytes = sizeof(double) * task->n * task->n;

    KJ_REQUIRE(bytes == expectedBytes, "Unexpected result data size");

    // Allocate a new matrix and copy data safely
    task->result_matrix = new utils::matrix_t(task->n);
    memcpy(task->result_matrix->data, resultData.begin(), bytes);

    task->result = utils::Submatrix(0, 0, task->n, task->n);
    task->result.active = true;

    resultQueue_.push(task->task_id);
    resultCv_.notify_all();
  }).wait(waitScope_);
}
