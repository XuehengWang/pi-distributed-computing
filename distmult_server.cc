// Converted version of your gRPC-based server to Cap'n Proto style (MatrixManagerImpl)
#include <capnp/ez-rpc.h>
#include <capnp/message.h>
#include "matrix.capnp.h"  // Cap'n Proto schema you must define and compile
#include <unordered_map>

#include <thread>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>

#include "task_handler.h"
#include "matrix_handler.h"

using matrixclass::MatrixClass;

//Change the name of this back to what is was
class MatrixManagerImpl final : public MatrixManager::Server {
public:
  MatrixManagerImpl(MatrixClass* handler) : handler_(handler) {
    handler_->initialize_buffers();
    computeThread_ = std::thread(&MatrixManagerImpl::pollResults, this);
  }

  ~MatrixManagerImpl() {
    stop_ = true;
    if (computeThread_.joinable())
      computeThread_.join();
  }

  kj::Promise<void> submitTask(SubmitTaskContext context) override {
    auto task = context.getParams().getTask();
    auto results = context.getResults();
  
    // Step 1: Select buffer
    int bufferIndex = handler_->select_next_buffer();
    if (bufferIndex == -1) {
      std::cerr << "No available buffer for new task." << std::endl;
      return kj::READY_NOW;
    }
  
    int bufferId = bufferIndex / 4;
    int threadId = bufferIndex % 4;
  
    // Step 2: Store context so pollResults can complete the response later
    {
      std::lock_guard<std::mutex> lock(contextMapMutex_);
      contextMap_[bufferIndex] = std::move(context);
    }
  
    // Step 3: Process the request using buffers
    handler_->process_request(task, bufferId, threadId);
  
    return kj::READY_NOW;
  }
  
private:
  void pollResults() {
    while (!stop_) {
      int bufferIndex = handler_->check_response();
      if (bufferIndex == -1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      int bufferId = bufferIndex / 4;
      int threadId = bufferIndex % 4;

      MatrixManager::SubmitTaskContext ctx;
      {
        std::lock_guard<std::mutex> lock(contextMapMutex_);
        auto it = contextMap_.find(bufferIndex);
        if (it == contextMap_.end()) {
          std::cerr << "Missing context for buffer index " << bufferIndex << std::endl;
          continue;
        }
        ctx = kj::mv(it->second);
        contextMap_.erase(it);
      }

      auto builder = ctx.getResults<MatrixResult>();
      handler_->serialize_result(bufferIndex, builder);

      ctx.sendReturn();
      handler_->add_resource(threadId);
    }
  }

  MatrixClass* handler_;
  std::thread computeThread_;
  std::atomic<bool> stop_ = false;
  std::mutex contextMapMutex_;
  std::unordered_map<int, MatrixManager::SubmitTaskContext> contextMap_;
};

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0] << " <task_type> <matrix_size> <address>\n";
    return 1;
  }

  std::string taskType = argv[1];
  int matrixSize = std::stoi(argv[2]);
  std::string address = argv[3];

  std::unique_ptr<MatrixClass> handler;
  if (taskType == "matrix") {
    handler = std::make_unique<MatrixClass>(matrixSize);
  } else {
    std::cerr << "Unsupported task type: " << taskType << std::endl;
    return 1;
  }

  capnp::EzRpcServer server(kj::heap<MatrixManagerImpl>(handler.get()), address);
  auto& waitScope = server.getWaitScope();
  kj::NEVER_DONE.wait(waitScope);

  return 0;
}
