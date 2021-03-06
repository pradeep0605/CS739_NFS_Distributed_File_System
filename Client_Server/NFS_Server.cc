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
#include <cstring>

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
#include <sys/types.h>
#include <aio.h> 
#include <errno.h> 

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using NFS_DFS::NFS_Server;
using NFS_DFS::LookupMessage;
using NFS_DFS::FileHandle;
using NFS_DFS::ReadRequest;
using NFS_DFS::ReadResponse;
using NFS_DFS::Buffer;
using NFS_DFS::ReadRequest;
using NFS_DFS::ReadResponse;
using NFS_DFS::WriteRequest;
using NFS_DFS::WriteResponse;
using NFS_DFS::FileCreateRequest;
using NFS_DFS::Integer;
using NFS_DFS::RenameRequest;

using namespace std;

// Enable the below macro to enable debug prints.
// #define DBG_PRINTS_ENABLED 1

typedef struct file_handle file_handle;
typedef struct aiocb aiocb;

#ifdef DBG_PRINTS_ENABLED
void print_FileHandle(FileHandle& fh) {
	
	cout << "Path = " << fh.path() << "\tmount_id = " << fh.mount_id() << "\t";
	const file_handle *fhp = reinterpret_cast<const file_handle *>
					(fh.handle().c_str());
	cout << "handle_bytes = " << fhp->handle_bytes << "\t handle = [";
	for(unsigned int i = 0;  i < fhp->handle_bytes; ++i) {
		cout <<	std::hex << (unsigned int) fhp->f_handle[i] << " ";
	}
	cout << "]" << endl << std::dec << std::flush;
}
#endif
// ============================================================================

string root_prefix;

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
} while (0)

static int
open_mount_path_by_id(int mount_id)
{
	char *linep;
	size_t lsize;
	char mount_path[PATH_MAX];
	int mi_mount_id, found;
	ssize_t nread;
	FILE *fp;

	fp = fopen("/proc/self/mountinfo", "r");
	if (fp == NULL)
		errExit("fopen");

	found = 0;
	linep = NULL;
	while (!found) {
		nread = getline(&linep, &lsize, fp);
		if (nread == -1)
			break;

		nread = sscanf(linep, "%d %*d %*s %*s %s",
				&mi_mount_id, mount_path);
		if (nread != 2) {
			fprintf(stderr, "Bad sscanf()\n");
			exit(EXIT_FAILURE);
		}

		if (mi_mount_id == mount_id)
			found = 1;
	}
	free(linep);

	fclose(fp);

	if (!found) {
		fprintf(stderr, "Could not find mount point\n");
		exit(EXIT_FAILURE);
	}

	return open(mount_path, O_RDONLY);
}

// ============================================================================

// Logic and data behind the server's behavior.
class NFS_Server_Impl final : public NFS_Server::Service {

// ============================================================================
	Status Ping(ServerContext * context, const Integer *request, Integer *reply) {
		cout << "ping." << request->data() << endl << std::flush;
		if (request->data() != 0xbadadd) {
			return Status::CANCELLED;
		}

		reply->set_data(0xdeadbeef);
		return Status::OK;
	}

// ============================================================================
  Status Lookup(ServerContext* context, const LookupMessage* request,
                  FileHandle* reply) override {
		string file_path = root_prefix + request->path();
		
		#ifdef DBG_PRINTS_ENABLED
		cout << "Lookup for file " << file_path << endl << std::flush;
		#endif
		// Hopefully 16 bytes are enough for the handle, else realloc the structure.
		int fhsize = sizeof(file_handle) + 16; 
		file_handle *fhp = (file_handle *) malloc(fhsize);
		int flags = 0;
		int dirfd = AT_FDCWD;
		fhp->handle_bytes = 0;
		int mount_id;

		// populate actual handle_bytes from kernel.
		if (name_to_handle_at(dirfd, file_path.c_str(), fhp,
					&mount_id, flags) != -1 || errno != EOVERFLOW) {
				cerr << __LINE__ << " : " <<
					"Unexpected result from name_to_handle_at()\n";
					return Status::CANCELLED;
		}
	
		if (fhp->handle_bytes > 16) {
			fhsize = sizeof(file_handle) + fhp->handle_bytes;
			fhp = (file_handle *) realloc(fhp, fhsize);
		}

		if (name_to_handle_at(dirfd, file_path.c_str(), fhp,
					&mount_id, flags) == -1) {
				cerr << __LINE__ << " : " <<
					"Unexpected result from name_to_handle_at()\n";
					return Status::CANCELLED;
		}
		
		// Populate the file handle
		reply->set_path(file_path);
		reply->set_mount_id(mount_id);
		reply->set_handle(string(reinterpret_cast<char *>(fhp), fhsize));
		
		free(fhp);
    return Status::OK;
  }

// ============================================================================

	Status Read(ServerContext* context, const ReadRequest* request,
			ReadResponse* response) override {
		#ifdef DBG_PRINTS_ENABLED
		cout << "Read Request for file " << request->fh().path() << endl;
		#endif
		int fd, mount_id, mount_fd;
		size_t read_sz = request->size(), actual_read_sz = request->size();
		off_t offset = request->offset();
		FileHandle fh = request->fh();
		const file_handle *fhp = reinterpret_cast<const file_handle *> 
			(fh.handle().c_str());
		mount_id = fh.mount_id();

		char buffer[read_sz];
		response->mutable_buff()->set_size(0);
		response->set_actual_read_bytes(-1);

		// Optimize later by maintaining <fh, fd> map
		/* Open file using handle and mount point */
		mount_fd = open_mount_path_by_id(mount_id);
		fd = open_by_handle_at(mount_fd, (file_handle *) fhp, O_RDONLY);
		if (fd < 0) {
			if (errno == 116) {
				// stale file handle.
				response->set_error_code(116);
				cerr << __LINE__ << " : " << "stale file handle found. Error = " 
						<< response->error_code() << endl;

				return Status(grpc::StatusCode::INVALID_ARGUMENT, "Stale File Handle");
			} else {
				cerr << __LINE__ << " : " << "Unexpected result from open_by_handle_at()\n"
						<< "ERROR No. is " << errno << endl;
			}
			return Status::CANCELLED;
		}
	
		if (lseek(fd, offset, SEEK_SET) >= 0) /* get to pos */ {
			actual_read_sz = read(fd, buffer, read_sz);
		} else {
			cerr << __LINE__ << " : " << "Unexpected result from lseek()\n";
			return Status::CANCELLED;
		}
	
		if (actual_read_sz == -1){
			cerr << __LINE__ << " : " << "Read failed at server\n";
			return Status::CANCELLED;
		}
	
		// buff.set_size(read_sz);
		// buff.set_data(string(reinterpret_cast<char *>(buffer)));
		
		response->mutable_buff()->set_size(read_sz);
		response->mutable_buff()->set_data(string(reinterpret_cast<char *>(buffer),
				actual_read_sz));
		response->set_actual_read_bytes(actual_read_sz);

		close(fd);
		close(mount_fd);
		return Status::OK;
  }

// ============================================================================

  Status Write(ServerContext* context, const WriteRequest* request,
                  WriteResponse* reply) override {
		
		#ifdef DBG_PRINTS_ENABLED
		cout << "Sync write on file " << request->fh().path() << endl;
		#endif
		FileHandle fh = request->fh();

		const file_handle *fhp = reinterpret_cast<const file_handle *>
					(fh.handle().c_str());
		FileOpenMap::iterator file_itr = file_open_map_.find(fh.path());
		int mount_id = fh.mount_id();
		reply->set_actual_bytes_written(0);
		
		int fd;
		int mount_fd;
		if (file_itr == file_open_map_.end()) {
			mount_fd = open_mount_path_by_id(mount_id);
			fd = open_by_handle_at(mount_fd, (file_handle *) fhp, O_WRONLY);
			if (fd < 0) {
				if (errno == 116) {
					// stale file handle.
					reply->set_error_code(116);
					cerr << __LINE__ << " : " << "Stale file handle found. Error = " 
							<< reply->error_code() << endl;
					return Status(grpc::StatusCode::INVALID_ARGUMENT, "Stale File Handle");
				} else {
					cerr << __LINE__ << ": " << "Unable to open file \"" <<	fh.path()
							 << "\" for writing\n" << endl << std::flush;
				}
				reply->set_actual_bytes_written(-1);
				close(mount_fd);
				return Status::CANCELLED;
			}
		}

		if (lseek(fd, request->offset(), SEEK_SET) >= 0) {
			// seeks success.
			int written = write(fd, request->buff().data().c_str(), request->size());
			if (written < 0) {
				cerr << __LINE__ << ": " << "Unable to Write to file \"" <<	fh.path()
					   << endl << std::flush;
			} else {
				// if write is succuessful, fsync the data to disk.
				fsync(fd);
			}
			reply->set_actual_bytes_written(written);
		}

		close(mount_fd);
		close(fd);
    return Status::OK;
  }

// ============================================================================
// Asynchronous Write
// ============================================================================

  Status Write_Async(ServerContext* context, const WriteRequest* request,
                  WriteResponse* reply) override {
		
		#ifdef DBG_PRINTS_ENABLED
		cout << "Async Write on file " << request->fh().path() << endl;
		#endif
		FileHandle fh = request->fh();

		const file_handle *fhp = reinterpret_cast<const file_handle *>
					(fh.handle().c_str());
		FileOpenMap::iterator file_itr = file_open_map_.find(fh.path());
		int mount_id = fh.mount_id();
		reply->set_actual_bytes_written(0);
		
		int fd;
		int mount_fd;
		if (file_itr == file_open_map_.end()) {
			mount_fd = open_mount_path_by_id(mount_id);
			fd = open_by_handle_at(mount_fd, (file_handle *) fhp, O_WRONLY);
			if (fd < 0) {
				if (errno == 116) {
					// stale file handle.
					reply->set_error_code(116);
					cerr << __LINE__ << " : " << "Stale file handle found. Error = " 
							<< reply->error_code() << endl;
					return Status(grpc::StatusCode::INVALID_ARGUMENT, "Stale File	Handle");
				} else {
					cerr << __LINE__ << ": " << "Unable to open file \"" <<	fh.path()
							 << "\" for writing\n" << endl << std::flush;
				}
				reply->set_actual_bytes_written(-1);
				close(mount_fd);
				return Status::CANCELLED;
			}
		}

		// Prepare the structure for Async IO
		aiocb *serveraio = (aiocb* ) malloc(sizeof(aiocb));
		char *dyn_buff = (char * ) malloc(request->size() + 1);
		memcpy(dyn_buff, request->buff().data().c_str(), request->size());

		memset(serveraio, 0, sizeof(aiocb));
		
		serveraio->aio_offset = request->offset();
		serveraio->aio_fildes = fd;
		serveraio->aio_nbytes = request->buff().size();
		serveraio->aio_buf = (volatile void*) (dyn_buff);
		serveraio->aio_sigevent.sigev_notify = SIGEV_NONE;

		// Track all the allocated buffers in server
		#if 0
		file_write_buffer_map_[fh.path()].push_back(serveraio);
		#endif

		int written = aio_write(serveraio);
		#ifdef DBG_PRINTS_ENABLED
		cout << "Tried to Async Write. The return code is : " << written << endl;
		#endif
		if (written != 0) {
			cerr << __LINE__ << ": " << "Unable to Write to file \"" <<	fh.path()
					 << endl << std::flush;
		}
		
		reply->set_actual_bytes_written(request->size());
		close(fd);
		close(mount_fd);
    return Status::OK;
  }

// ============================================================================

Status Fsync(ServerContext* context, const FileHandle* fh, Integer* reply) {
	const file_handle *fhp = reinterpret_cast<const file_handle *> 
		(fh->handle().c_str());
	int mount_id = fh->mount_id();
	#ifdef DBG_PRINTS_ENABLED
	cout << "Commit on file " << fh->path() << " called" << endl;
	#endif
	
	int mount_fd = open_mount_path_by_id(mount_id);
	int fd = open_by_handle_at(mount_fd, (file_handle *) fhp, O_WRONLY);
	if (fd < 0) {
		if (errno == 116) {
			// stale file handle.
			reply->set_data(116);
			cerr << __LINE__ << " : " << "stale file handle found. Error = " 
				<< reply->data() <<	endl;
			return Status(grpc::StatusCode::INVALID_ARGUMENT, "Stale File Handle");
		} else {
		cerr << __LINE__ << " : " << "Unexpected result from open_by_handle_at()\n"
		   << "ERROR No. is " << errno << endl;
		}
  	return Status::CANCELLED;
	}

	int f_ret = fsync(fd);
	reply->set_data(f_ret);

	#if 0
	for (auto entry : file_write_buffer_map_[fh->path()]) {
		aiocb *serveraio  = entry;
		free((void*) serveraio->aio_buf);
		free(serveraio);
	}
	#endif

	close(fd);
	close(mount_fd);
	return Status::OK;
}


// ============================================================================

  Status Readdir(ServerContext* context, const LookupMessage* request,
                  Buffer* reply) override {
		string dir_path = root_prefix + request->path();
		int fd = open(dir_path.c_str(), O_RDONLY);
		DIR *dir_fd = fdopendir(fd);
		#ifdef DBG_PRINTS_ENABLED
		cout  << "Read dir request for directory " << dir_path << endl;
		#endif
		
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

// ============================================================================

  Status Getattr(ServerContext* context, const LookupMessage* request,
				Buffer* reply) override {
		string file_path = root_prefix + request->path();
		struct stat st;
		int ret = stat(file_path.c_str(), &st);
		#ifdef DBG_PRINTS_ENABLED
		cout << "Get attribute requested for file " << file_path << endl;
		#endif
		// No need to handle failures in stat because the file being requested might
		// not actually exit. So, it will return with an error.
		reply->set_size(sizeof(st));
		reply->set_data(string(reinterpret_cast<char*>(&st), sizeof(st)));
	
		return (ret == 0) ? Status::OK : Status::CANCELLED;
	}

// ============================================================================

	Status CreateFile(ServerContext* context, const FileCreateRequest *request,
		Integer *reply) override {
		mode_t mode = request->mode();
		reply->set_data(-1);
		
		#ifdef DBG_PRINTS_ENABLED
		cout << "File " << request->path() << " Created" << " with mode = "
				<< std::hex << mode << std::dec << endl << std::flush;
		#endif
		string file_path = root_prefix + request->path();
		int fd = creat(file_path.c_str(), mode);
		if (fd < 0) {
			cerr << __LINE__ << ": " << "Unable to create file with permission "
				<< std::hex << mode << std::dec << endl << std::flush;
			return Status::CANCELLED;
		}

		reply->set_data(fd);
		close(fd);
		return Status::OK;
	}

// ============================================================================

	Status DeleteFile(ServerContext* context, const LookupMessage *request,
	Integer *reply) override {
		string file_path = root_prefix + request->path();
		#ifdef DBG_PRINTS_ENABLED
			cout << "Delete file request for " << file_path << endl;
		#endif
		int ret = unlink(file_path.c_str());
		
		reply->set_data(ret);
	
		if (ret < 0) {
			cerr << __LINE__ << ": " << "Unable to delete the file " << file_path 
					<< endl << std::flush;
			return Status::CANCELLED;
		}
		return Status::OK;
	}

// ============================================================================

	Status CreateDirectory (ServerContext* context, const FileCreateRequest *request,
		Integer *reply) override {
		string file_path = root_prefix + request->path();
		mode_t mode  = request->mode();
		#ifdef DBG_PRINTS_ENABLED
			cout << "Create directory requested : " << file_path << endl;
		#endif
		int ret = mkdir(file_path.c_str(), mode);
		reply->set_data(ret);

		if (ret < 0) {
			cerr << __LINE__ << ": " << "Unable to create the direcoty " << file_path 
					<< endl << std::flush;
			return Status::CANCELLED;
		}
		return Status::OK;
	}

// ============================================================================

	Status DeleteDirectory (ServerContext *context, const LookupMessage *request,
		Integer *reply) override {
		string file_path = root_prefix + request->path();
		#ifdef DBG_PRINTS_ENABLED
			cout << "Delete directory on : " << file_path << endl;
		#endif
		int ret = rmdir(file_path.c_str());
		reply->set_data(ret);

		if (ret < 0) {
			cerr << __LINE__ << ": " << "Unable to Delete the direcoty " << file_path 
					<< endl << std::flush;
			return Status::CANCELLED;
		}
		return Status::OK;
	}

// ============================================================================
	
	Status RenameFile (ServerContext *context, const RenameRequest *request,
		Integer *reply) override {
		string from_file_path = root_prefix + request->from_path();
		string to_file_path = root_prefix + request->to_path();
		#ifdef DBG_PRINTS_ENABLED
			cout << "Rename request from : " << from_file_path << " to " 
				<< to_file_path << endl;
		#endif
			
		int ret = rename(from_file_path.c_str(), to_file_path.c_str());
		reply->set_data(ret);

		if (ret < 0) {
			cerr << __LINE__ << ": " << "Unable to rename from " << from_file_path 
					<< " to " << to_file_path << endl << std::flush;
			return Status::CANCELLED;
		}
		return Status::OK;
	}

// ============================================================================
	
	private:
	typedef unordered_map<string, int> FileOpenMap;
	FileOpenMap file_open_map_; // keeps trak of files open for writing.
	typedef unordered_map<string, vector<aiocb *>> FileWriteBufferMap;
	FileWriteBufferMap file_write_buffer_map_;
};

// ============================================================================


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

// ============================================================================

int main(int argc, char** argv) {
	if (argc < 2) {
		cout << "Usage: NFS_Sserver root_directory" << endl;
		exit(0);
	}
  RunServer(argv[1]);
  return 0;
}

// ============================================================================
