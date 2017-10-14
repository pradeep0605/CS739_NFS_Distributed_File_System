/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <memory>
#include <string>

#include <grpc++/grpc++.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "NFS.grpc.pb.h"
#endif

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using NFS_DFS::NFS_Server;
using NFS_DFS::HelloRequest;
using NFS_DFS::HelloReply;
using NFS_DFS::LookupMessage;
using NFS_DFS::FileHandle;
using NFS_DFS::Buffer;

using namespace std;

string root_prefix;
// Logic and data behind the server's behavior.
class NFS_Server_Impl final : public NFS_Server::Service {
  Status Lookup(ServerContext* context, const LookupMessage* request,
                  FileHandle* reply) override {
		// cout << "Coming here " << endl;
    reply->set_path(root_prefix + request->path());
    return Status::OK;
  }

  Status Read(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    return Status::OK;
  }

  Status Write(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    return Status::OK;
  }

  Status Readdir(ServerContext* context, const LookupMessage* request,
                  Buffer* reply) override {
		string dir_path = root_prefix + request->path();
		int fd = open(dir_path.c_str(), O_RDONLY);
		DIR *dir_fd = fdopendir(fd);
		
		string directory_contents = "";
		struct dirent *dp;
		int errno;
		do {
			if ((dp = readdir(dir_fd)) != NULL) {
				directory_contents = directory_contents + string(dp->d_name) + "\n";
			}
		}	while (dp != nullptr);
		
		closedir(dir_fd);
		reply->set_size(directory_contents.size());
		reply->set_data(directory_contents);
    return Status::OK;
  }

  Status Getattr(ServerContext* context, const LookupMessage* request,
				Buffer* reply) override {
		string file_path = root_prefix + request->path();
		struct stat st;
		int status = 	stat(file_path.c_str(), &st);
		reply->set_size(sizeof(st));
		reply->set_data(string(reinterpret_cast<char*>(&st), sizeof(st)));
		return (status == 0) ? Status::OK : Status::CANCELLED;
	}
};

void RunServer(char *root) {
  std::string server_address("0.0.0.0:50051");
  // NFS_Server_Impl service = NFS_Server_Impl(move(string(root)));
  NFS_Server_Impl service;
	
	// Global
	root_prefix = string(root);

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
	if (argc < 2) {
		cout << "Usage: NFS_Sserver root_directory" << endl;
		exit(0);
	}
  RunServer(argv[1]);
  return 0;
}
