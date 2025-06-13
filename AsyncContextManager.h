#pragma once

#include <kj/async-io.h>

struct AsyncThreadContext {
  kj::AsyncIoContext context;
  kj::AsyncIoProvider* ioProvider;
  kj::WaitScope& waitScope;  // ✅ store by reference

  AsyncThreadContext()
      : context(kj::setupAsyncIo()),
        ioProvider(context.provider.get()),
        waitScope(context.waitScope) {}  // ✅ reference initialized directly
};

class AsyncContextManager {
public:
  static AsyncThreadContext& getThreadLocalContext() {
    thread_local AsyncThreadContext ctx;
    return ctx;
  }
};
