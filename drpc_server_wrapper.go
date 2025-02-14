package main

import (
    "context"
    "fmt"
    "log"
    "sync"
    "time"

    pb "path/to/your/generated/code" // Update with the correct import path
    "storj.io/drpc"
    "net"
)

//Global server instance
var server *drpc.Server


// ComputeRPC handles the DRPC request processing
type ComputeRPC struct {
    pb.DRPCChatServiceServer
    mu          sync.Mutex
    taskHandler *TaskHandler
    stopThreads bool
}

// NewComputeRPC initializes the RPC handler
func NewComputeRPC(handler *TaskHandler) *ComputeRPC {
    c := &ComputeRPC{
        taskHandler: handler,
        stopThreads: false,
    }

    handler.initializeBuffers()

    // Start writer thread
    go c.writer()

    return c
}

func (c *ComputeRPC) Chat(stream pb.DRPCChatService_ChatStream) error {
    var wg sync.WaitGroup

    // Channel to send responses asynchronously
    responseChan := make(chan *pb.MatrixResponse)

    // Goroutine for writing responses
    wg.Add(1)
    go func() {
        defer wg.Done()
        for response := range responseChan {
            if err := stream.Send(response); err != nil {
                log.Println("Error sending response:", err)
                return
            }
        }
    }()

    // Read loop
    for {
        req, err := stream.Recv()
        if err != nil {
            log.Println("Stream closed:", err)
            break
        }

        log.Printf("Received request: ID=%d, Data=%v", req.Id, req.Data)

        // Process the request (equivalent to OnReadDone)
        c.processRequest(req, responseChan)
    }

    // Cleanup
    close(responseChan)
    wg.Wait()
    return nil
}

// processRequest assigns tasks to compute threads (equivalent to OnReadDone)
func (c *ComputeRPC) processRequest(req *pb.MatrixRequest, responseChan chan *pb.MatrixResponse) {
    c.mu.Lock()
    defer c.mu.Unlock()

    bufferID := req.Id / 4
    threadID := req.Id % 4

    log.Printf("Processing request in buffer %d, thread %d", bufferID, threadID)

    // Assign task to the appropriate compute thread
    c.taskHandler.processRequest(bufferID, threadID)

    // Schedule next read
    go c.NextRead()
}

// NextWrite checks for pending responses and sends them (equivalent to OnWriteDone)
func (c *ComputeRPC) NextWrite(responseChan chan *pb.MatrixResponse) {
    allID := c.taskHandler.checkResponse()

    if allID == -1 {
        log.Println("No response available yet, waiting...")
        time.Sleep(time.Millisecond * 100) // Short wait before retrying
        return
    }

    bufferID := allID / 4
    threadID := allID % 4

    response := c.taskHandler.getBufferResponse(bufferID, threadID)

    if response == nil {
        log.Println("Error: No valid response found")
        return
    }

    // Send response back to the client
    responseChan <- response

    // Release compute resources
    c.taskHandler.addResource(threadID)

    log.Printf("Sent response from thread %d, buffer %d", threadID, bufferID)

    // Schedule next write
    go c.NextWrite(responseChan)
}

// NextRead selects the next buffer and reads the request (equivalent to NextRead)
func (c *ComputeRPC) NextRead() {
    c.mu.Lock()
    defer c.mu.Unlock()

    currentBuffer := c.taskHandler.selectNextBuffer()
    if currentBuffer == -1 {
        log.Println("ERROR: No buffer available for receiving new request!")
        return
    }

    bufferID := currentBuffer / 4
    threadID := currentBuffer % 4

    log.Printf("Selected next buffer: %d for thread %d", bufferID, threadID)

    request := c.taskHandler.getBufferRequest(bufferID, threadID)
    if request == nil {
        log.Println("ERROR: Failed to get request buffer")
        return
    }

    // Simulating reading from a buffer
    log.Printf("Reading new task from buffer %d for thread %d", bufferID, threadID)
}

// writer continuously checks for responses and writes them (equivalent to writer function)
func (c *ComputeRPC) writer() {
    responseChan := make(chan *pb.MatrixResponse)
    for {
        if c.stopThreads {
            log.Println("Writer thread stopped.")
            return
        }

        c.NextWrite(responseChan)

        // Short delay before next write attempt
        time.Sleep(time.Millisecond * 100)
    }
}
// StartComputeRPCServer starts the DRPC server and listens for connections
//export StartComputeRPCServer
func StartComputeRPCServer(port *C.char) *C.char {
    goPort := C.GoString(port)
    address := fmt.Sprintf(":%s", goPort)

    server = drpc.NewServer()
    pb.RegisterDRPCChatServiceServer(server, NewComputeRPC(&TaskHandler{}))

    listener, err := net.Listen("tcp", address)
    if err != nil {
        log.Printf("Failed to listen on port %s: %v", goPort, err)
        return C.CString("Failed to start server")
    }

    log.Printf("DRPC Server running on %s", goPort)
    go func() {
        if err := server.Serve(context.Background(), listener); err != nil {
            log.Fatal("Failed to start server:", err)
        }
    }()

    return C.CString("Server started successfully")
}

//export StopComputeRPCServer
func StopComputeRPCServer() {
    if server != nil {
        server.Shutdown()
        log.Println("DRPC Server stopped")
    }
}

func main() {}
