#include "CapnpMatrixClient.h"

CapnpMatrixClient::CapnpMatrixClient(capnp::EzRpcClient& client,
                                     std::queue<int>& resultQueue,
                                     std::condition_variable& resultCv,
                                     std::mutex& resultMutex,
                                     int submatrixSize)
    : stub_(client.getMain<MatrixManager>()),
      resultQueue_(resultQueue),
      resultCv_(resultCv),
      resultMutex_(resultMutex),
      submatrixSize_(submatrixSize) {}

void CapnpMatrixClient::submitTask(task_node_t* task) {
  auto req = stub_.submitTaskRequest();

  auto matrixTask = req.initTask();
  matrixTask.setTaskId(task->task_id);
  matrixTask.setOps("multiply");  // or use task->ops if enum is mapped
  matrixTask.setN(task->n);

  // Assuming task->left_matrix is set
  const double* data = task->left_matrix->data;
  kj::ArrayPtr<const capnp::byte> inputA(reinterpret_cast<const capnp::byte*>(data), sizeof(double) * task->n * task->n);
  matrixTask.setInputA(inputA);

  data = task->right_matrix->data;
  kj::ArrayPtr<const capnp::byte> inputB(reinterpret_cast<const capnp::byte*>(data), sizeof(double) * task->n * task->n);
  matrixTask.setInputB(inputB);

  req.send().then([&, task](capnp::Response<MatrixManager::SubmitTaskResults> result) {
    const auto& r = result.getResult();
    std::lock_guard<std::mutex> lock(resultMutex_);
    task->task_id = r.getTaskId();  // confirm match
    task->n = r.getN();

    const auto resultData = r.getResult();
    task->result_matrix = new utils::matrix_t(task->n, reinterpret_cast<const double*>(resultData.begin()));
    task->result = utils::Submatrix(0, 0, task->n, task->n);
    task->result.active = true;

    resultQueue_.push(task->task_id);
    resultCv_.notify_all();
  }).wait();  // Wait inline, or make this async if needed
}
