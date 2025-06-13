#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>
#include <kj/debug.h>
//#include <kj/async-timer.h>
#include <unordered_map>
#include <iostream>
#include <memory>
#include <sched.h>

#include "task_handler.h"
#include "matrix_handler.h"
#include "protos/matrix.capnp.h"

using matrixclass::MatrixClass;

// === CPU Binding ===
void bindToCpu(int cpuId) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpuId, &cpuset);
  if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
    perror("sched_setaffinity");
  } else {
    std::cout << "Bound process to CPU " << cpuId << "\n";
  }
}

// === MatrixManagerImpl ===
class MatrixManagerImpl final : public MatrixManager::Server {
public:
  MatrixManagerImpl(TaskHandler* handler, kj::Timer& timer)
      : handler_(handler), timer_(timer) {
    handler_->initialize_buffers();
    std::cout << "MatrixManagerImpl initialized." << std::endl;
  }

  kj::Promise<void> submitTask(MatrixManager::Server::SubmitTaskContext context) override {
    auto task = context.getParams().getTask();
    int taskId = task.getTaskId();
    std::cout << "[SERVER] Received task ID: " << taskId << std::endl;

    int bufferIndex = handler_->select_next_buffer();
    if (bufferIndex < 0) {
      KJ_FAIL_REQUIRE("No buffer available");
    }

    handler_->process_request(task, bufferIndex, 0);  // stage data

    auto ctx = kj::mv(context);
    auto paf = kj::newPromiseAndFulfiller<void>();
    auto fulfiller = kj::mv(paf.fulfiller);#include "CapnpMatrixClient.h"
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


    auto pollFn = [this, fulfiller = kj::mv(fulfiller), ctx = kj::mv(ctx)]() mutable -> kj::Promise<void> {
      int readyBufferId = handler_->check_response();
      if (readyBufferId < 0) {
        return timer_.afterDelay(10 * kj::MILLISECONDS).then(
          [this, fulfiller = kj::mv(fulfiller), ctx = kj::mv(ctx)]() mutable {
            return submitTaskPoll(kj::mv(fulfiller), kj::mv(ctx));
        });
      } else {
        ::capnp::MallocMessageBuilder message;
        MatrixResult::Builder resultBuilder = message.initRoot<MatrixResult>();
        handler_->serialize_result(readyBufferId, resultBuilder);
        handler_->add_resource(readyBufferId / 3);
        ctx.getResults().setResult(resultBuilder.asReader());
        fulfiller->fulfill();
        return kj::Promise<void>(kj::READY_NOW);
      }
    };

    return timer_.afterDelay(1 * kj::MILLISECONDS)
        .then(kj::mv(pollFn))
        .then([promise = kj::mv(paf.promise)]() mutable {
          return kj::mv(promise);
        });
  }

private:
  kj::Promise<void> submitTaskPoll(kj::Own<kj::PromiseFulfiller<void>> fulfiller,
                                   MatrixManager::Server::SubmitTaskContext ctx) {
    return timer_.afterDelay(10 * kj::MILLISECONDS).then(
      [this, fulfiller = kj::mv(fulfiller), ctx = kj::mv(ctx)]() mutable {
        int readyBufferId = handler_->check_response();
        if (readyBufferId < 0) {
          return submitTaskPoll(kj::mv(fulfiller), kj::mv(ctx));
        } else {
          ::capnp::MallocMessageBuilder message;
          MatrixResult::Builder resultBuilder = message.initRoot<MatrixResult>();
          handler_->serialize_result(readyBufferId, resultBuilder);
          handler_->add_resource(readyBufferId / 3);
          ctx.getResults().setResult(resultBuilder.asReader());
          fulfiller->fulfill();
          return kj::Promise<void>(kj::READY_NOW);
        }
      });
  }

  TaskHandler* handler_;
  kj::Timer& timer_;
};

// === Main ===
int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0] << " <task_type> <matrix_size> <address>\n";
    return 1;
  }

  std::string taskType = argv[1];
  int matrixSize = std::stoi(argv[2]);
  std::string address = argv[3];

  bindToCpu(0);  // Optional: pin to core 0

  kj::AsyncIoContext ioContext = kj::setupAsyncIo();
  kj::WaitScope& waitScope = ioContext.waitScope;

  std::unique_ptr<TaskHandler> handler;
  if (taskType == "matrix") {
    handler = std::make_unique<MatrixClass>(matrixSize);
  } else {
    std::cerr << "Unsupported task type.\n";
    return 1;
  }

  // Create a timer using the correct timer source
  kj::Timer& timer = ioContext.provider->getTimer();

  // Instantiate service
  auto serverImpl = kj::heap<MatrixManagerImpl>(handler.get(), timer);

  // Setup TwoPartyServer
  capnp::TwoPartyServer server(capnp::Capability::Client(kj::mv(serverImpl)));

  // Parse address and listen for connections
  auto addr = ioContext.provider->getNetwork().parseAddress(address).wait(waitScope);
  auto listener = addr->listen();

  std::cout << "Listening on " << address << "..." << std::endl;

  // Accept loop
  server.listen(*listener).wait(waitScope);

  return 0;
}
