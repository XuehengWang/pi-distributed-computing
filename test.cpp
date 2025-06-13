#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>
#include <kj/debug.h>
#include <iostream>

int main() {
    kj::AsyncIoContext ioContext = kj::setupAsyncIo();

    // Use the correct parseAddress function from ioContext.provider
    kj::Own<kj::NetworkAddress> addr = ioContext.provider->getNetwork().parseAddress("0.0.0.0", 50051).wait(ioContext.waitScope);

    // This will create a listener socket
    kj::Own<kj::ConnectionReceiver> listener = addr->listen();

    std::cout << "Cap'n Proto successfully bound to 0.0.0.0:50051" << std::endl;

    // Keep the server running
    kj::NEVER_DONE.wait(ioContext.waitScope);
    return 0;
}
