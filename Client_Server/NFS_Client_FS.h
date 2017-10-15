// Hello filesystem class definition

#ifndef __HELLOFS_H_
#define __HELLOFS_H_
#include <iostream>
#include <memory>
#include <string>
#include <grpc++/grpc++.h>
#include <memory>
#include <unordered_map>
#include "../include/Fuse.h"

#include "../include/Fuse-impl.h"
#include "NFS.grpc.pb.h"
#include "NFS_Client_FS.h"
#include <chrono>
#include <thread>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using NFS_DFS::NFS_Server;
using NFS_DFS::HelloRequest;
using NFS_DFS::HelloReply;
using NFS_DFS::LookupMessage;
using NFS_DFS::FileHandle;
using NFS_DFS::Buffer;

using namespace std;

typedef struct file_handle file_handle;
const char *const  server_ip = "10.128.0.2:50051";

// This class handles all grpc related calls.
class NFS_Client {
 public:
  NFS_Client(std::shared_ptr<Channel> channel)
      : stub_(NFS_Server::NewStub(channel)) {}
  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  FileHandle Lookup_File(string&& path);
	Buffer Read_Directory(string&& path);
	Buffer Get_File_Attributes(string&& path);
 private:
  std::unique_ptr<NFS_Server::Stub> stub_;
};


typedef unordered_map<string, FileHandle> FileHandleMap;
class ClientFS : public Fusepp::Fuse<ClientFS>
{
	static FileHandleMap file_handle_map_;
	static unique_ptr<NFS_Client> client_ptr;
public:

	static void Initialize_Client() {
		if (ClientFS::client_ptr != nullptr) {
			client_ptr = nullptr;
		}
		ClientFS::client_ptr = make_unique<NFS_Client>(grpc::CreateChannel(server_ip, 
		 grpc::InsecureChannelCredentials()));
	}
  ClientFS() {
	}

  ~ClientFS() {
	}

  static int getattr (const char *, struct stat *, struct fuse_file_info *);

  static int readdir(const char *path, void *buf,
                     fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi,
                     enum fuse_readdir_flags);
  
  static int open(const char *path, struct fuse_file_info *fi);

  static int read(const char *path, char *buf, size_t size, off_t offset,
                  struct fuse_file_info *fi);
};

#endif
