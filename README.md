# RaspberryPi Cluster for Matrix Multiplication

## 1. Install gRPC dependencies

Refer to the official gRPC documentation for C++: [gRPC C++ Docs](https://grpc.io/docs/languages/cpp/)

### Local folder for packages
```bash
export MY_INSTALL_DIR=$HOME/.local
mkdir -p $MY_INSTALL_DIR
export PATH="$MY_INSTALL_DIR/bin:$PATH"
```

### `cmake` (later version than `apt-get`) and other dependencies
```bash
wget -q -O cmake-linux.sh https://github.com/Kitware/CMake/releases/download/v3.30.3/cmake-3.30.3-linux-x86_64.sh
sh cmake-linux.sh -- --skip-license --prefix=$MY_INSTALL_DIR
export PATH=~/.local/bin:$PATH
sudo apt update
sudo apt install -y build-essential autoconf libtool pkg-config
```

**NOTE:** change `$MY_INSTALL_DIR` and `PATH` as needed

### gRPC
```bash
git clone --recurse-submodules -b v1.66.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc
cd grpc
mkdir -p cmake/build
pushd cmake/build
cmake -DgRPC_INSTALL=ON  -DgRPC_BUILD_TESTS=OFF  -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR ../..
make -j$(nproc)
make install
popd

sudo apt install libgrpc++-dev libprotobuf-dev protobuf-compiler
```

## 2. Compile
```bash
mkdir build
cd build
cmake -DCMAKE_PREFIX_PATH=$MY_INSTALL_DIR ../.. 
make
```
- Or just use `cmake -DCMAKE_PREFIX_PATH=$HOME/.local ..`


## 3. Run servers (secondary)
- For example, to run three servers:
```bash
./distmult_server matrix 10.10.1.1:50051
./distmult_server matrix 10.10.1.1:50052
./distmult_server matrix 10.10.1.1:50053
```

## 4. Run client (primary)
```bash
./distmult_client matrix 3 10.10.1.1:50051 10.10.1.1:50052 10.10.1.1:50053
```

---

Others:
- Manually compile proto file, assuming `distmult.proto` is in the current directory
```bash
protoc --proto_path=. --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` distmult_service.proto
```

- Run tests 
```bash
ctest --output-on-failure
```
    - This is supposed to work using cmake, but now most tests are not integrated with cmake
    - Some tests are manually compiled and run:

```bash
// test_matrix_utils
g++ -std=c++17 -o test test_matrix_utils.cc utils.cc
// test
g++ -std=c++17 -pthread test_resource_scheduler.cc resource_scheduler.cc -o test
wow manually works
g++ -std=c++17 -D__THREAD test_resource_scheduler.cc resource_scheduler.cc -o test -lpthread
```

