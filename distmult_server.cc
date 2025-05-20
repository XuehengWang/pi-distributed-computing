// Converted version of your gRPC-based server to Cap'n Proto style (MatrixManagerImpl)
#include <capnp/ez-rpc.h>
//#include <capnp/message.h>
//#include "matrix.capnp.h"  // Cap'n Proto schema you must define and compile
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
    //computeThread_ = std::thread(&MatrixManagerImpl::pollResults, this);
  }

  ~MatrixManagerImpl() {
    stop_ = true;
    if (computeThread_.joinable())
      computeThread_.join();
  }

  kj::Promise<void> submitTask(MatrixManager::Server::SubmitTaskContext context) override {
    auto task = context.getParams().getTask();

    int bufferIndex = handler_->select_next_buffer();
    if (bufferIndex == -1) {
        std::cerr << "No available buffer for new task." << std::endl;
        return kj::READY_NOW;
    }

    int bufferId = bufferIndex / 4;
    int threadId = bufferIndex % 4;
    {
      std::lock_guard<std::mutex> lock(contextMapMutex_);
      contextMap_[bufferIndex] = std::make_unique<MatrixManager::Server::SubmitTaskContext>(kj::mv(context));
    }

    handler_->process_request(task, bufferId, threadId);

    return kj::READY_NOW;  // reply will happen later
  }
  
  kj::Promise<void> pollResultsOnce() {
    int bufferIndex = handler_->check_response();

    if (bufferIndex == -1) {
      // Wait a bit and try again (non-blocking)
      return kj::evalLater([this]() {
        return pollResultsOnce();
      });
    }

    std::unique_ptr<MatrixManager::Server::SubmitTaskContext> ctx;

  {
    std::lock_guard<std::mutex> lock(contextMapMutex_);
    auto it = contextMap_.find(bufferIndex);
    if (it != contextMap_.end()) {
      ctx = std::move(it->second);
      contextMap_.erase(it);
    }
  }

  if (ctx) {
    auto builder = ctx->getResults().initResult();
    handler_->serialize_result(bufferIndex, builder);
    // Automatically replies when Promise completes
  }

  handler_->add_resource(bufferIndex % 4);

  return pollResultsOnce();
  }

  
  private:
  MatrixClass* handler_;
  std::thread computeThread_;
  std::atomic<bool> stop_ = false;
  std::mutex contextMapMutex_;
  //std::unordered_map<int, kj::Own<MatrixManager::Server::SubmitTaskContext>> contextMap_;
  std::unordered_map<int, std::unique_ptr<MatrixManager::Server::SubmitTaskContext>> contextMap_;


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
  auto serviceImpl = kj::heap<MatrixManagerImpl>(handler.get());
  auto* serviceRaw = serviceImpl.get();  // save raw pointer before move

  capnp::EzRpcServer server(kj::mv(serviceImpl), address);
  auto& waitScope = server.getWaitScope();
  serviceRaw->pollResultsOnce();  // safe, as EzRpcServer owns the object now

  // Wait forever so the event loop keeps running
  kj::NEVER_DONE.wait(waitScope);

  return 0;
}
