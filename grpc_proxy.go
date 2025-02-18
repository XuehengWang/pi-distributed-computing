package main

import (
    "context"
    "fmt"
    "log"
    "net"

    pb "path/to/your/generated/grpc/code"
    drpcpb "path/to/your/generated/drpc/code"

    "google.golang.org/grpc"
    "storj.io/drpc"
    "storj.io/drpc/drpcconn"
)

// ProxyServer struct
type ProxyServer struct {
    pb.UnimplementedComputeServiceServer
    drpcClient drpcpb.DRPCChatServiceClient
}

// NewProxyServer initializes the gRPC proxy
func NewProxyServer(drpcAddr string) (*ProxyServer, error) {
    conn, err := drpcconn.New("tcp", drpcAddr, drpcconn.Config{})
    if err != nil {
        return nil, fmt.Errorf("failed to connect to drpc server: %w", err)
    }

    return &ProxyServer{
        drpcClient: drpcpb.NewDRPCChatServiceClient(conn),
    }, nil
}

// Compute handles streaming requests and responses
func (s *ProxyServer) Compute(stream pb.ComputeService_ComputeServer) error {
    drpcStream, err := s.drpcClient.Chat(context.Background())
    if err != nil {
        return fmt.Errorf("failed to start drpc stream: %w", err)
    }

    // Goroutine to forward messages from client to drpc server
    go func() {
        for {
            req, err := stream.Recv()
            if err != nil {
                log.Println("gRPC Client stream closed:", err)
                return
            }

            drpcReq := &drpcpb.MatrixRequest{
                Id:   req.Id,
                Data: req.Data,
            }

            if err := drpcStream.Send(drpcReq); err != nil {
                log.Println("Error forwarding request to drpc:", err)
                return
            }
        }
    }()

    // Forward responses from drpc back to gRPC client
    for {
        drpcRes, err := drpcStream.Recv()
        if err != nil {
            log.Println("drpc stream closed:", err)
            return err
        }

        grpcRes := &pb.MatrixResponse{
            Id:     drpcRes.Id,
            Result: drpcRes.Result,
        }

        if err := stream.Send(grpcRes); err != nil {
            log.Println("Error sending response to gRPC client:", err)
            return err
        }
    }
}

func main() {
    // Connect to `drpc` server running on port 8080
    proxyServer, err := NewProxyServer("localhost:8080")
    if err != nil {
        log.Fatalf("Failed to create proxy server: %v", err)
    }

    grpcServer := grpc.NewServer()
    pb.RegisterComputeServiceServer(grpcServer, proxyServer)

    listener, err := net.Listen("tcp", ":50051")
    if err != nil {
        log.Fatalf("Failed to listen: %v", err)
    }

    log.Println("gRPC Proxy Server running on port 50051")
    if err := grpcServer.Serve(listener); err != nil {
        log.Fatalf("Failed to serve gRPC: %v", err)
    }
}

