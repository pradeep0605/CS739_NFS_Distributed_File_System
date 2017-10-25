#include "NFS_Client_FS.h"

#include <iostream>
#include <stdio.h>

// include in one .cpp file
#include "../include/Fuse-impl.h"

using namespace std;

// Enable the below macro to enable debug prints.
// #define DBG_PRINTS_ENABLED 1

// ============================================================================

int Reconnect_To_Server() {
	Integer request;
	request.set_data(0xbadadd);

	int sleep_time_ms = 50;
	const int RETRY_TIMEOUT = 300; /* 15 seconds */
	int iterations = 0;
	Status status = Status::CANCELLED;
	do {
		if (iterations++ >= RETRY_TIMEOUT) {
			return -1;
		}
		cout << "Reconnect try iteration: " << iterations << endl;

		this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
		// Reinitialize client_ptr; i.e., reconnect to server
		// ClientFS::client_ptr->channel_->~Channel();
		// std::shared_ptr<Channel> channel = ClientFS::Initialize_Client();
		// initialize stub again
		//ClientFS::channel_.WaitForConnected();
		/*
		gpr_timespec timeOut;
		timeOut.tv_sec = 0;
		timeOut.tv_nsec = 1000 * 200; // 50 Miliseconds
		timeOut.clock_type = GPR_TIMESPAN;
		ClientFS::client_ptr->channel_->WaitForConnected(timeOut);
		*/
		status = ClientFS::client_ptr->Ping_Server();
		#ifdef DBG_PRINTS_ENABLED
		cout << "Error code = " << status.error_code() << endl;
		#endif
	} while(!status.ok());
	//}	while(ClientFS::client_ptr->channel_->GetState(true) != GRPC_CHANNEL_READY);

	// Successfully reconnected to server
	return 0;
}
// ============================================================================

FileHandleMap ClientFS::file_handle_map_;
unique_ptr<NFS_Client> ClientFS::client_ptr;

#ifdef DBG_PRINTS_ENABLED
void print_FileHandle(FileHandle& fh) {
  cout << "Path = " << fh.path() << "\tmount_id = " << fh.mount_id() << "\t";
	const file_handle *fhp = reinterpret_cast<const file_handle *>
				(fh.handle().c_str());
	cout << "handle_bytes = " << fhp->handle_bytes << "\t handle = [";
	for(unsigned int i = 0;  i < fhp->handle_bytes; ++i) {
		cout << std::hex << (unsigned int) fhp->f_handle[i] << " "; 
	}
	cout << "]" << endl << std::dec << std::flush;
}
#endif
// ============================================================================

FileHandle NFS_Client::Lookup_File(string&& path) {
    // Data we are sending to the server.
LookupMessage file_path;
    file_path.set_path(path);

    // Container for the data we expect from the server.
    FileHandle reply;
    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = stub_->Lookup(&context, file_path, &reply);

    // Act upon its status.
    if (!status.ok()) {
			if (status.error_code() == CONN_ERR_CODE) {
				if (Reconnect_To_Server() != 0) {
					cerr << __LINE__ << ": " << "Unable to reconnect to the server"
						<< endl << std::flush;
				}
			}
		}
		//  When iterations >= RETRY_TIMEOUT, an empty reply is sent.
		return move(reply);
}

// ============================================================================

Buffer NFS_Client::Read_Directory(string &&path) {
    LookupMessage file_path;
    file_path.set_path(path);
		
		Buffer reply;
		ClientContext context;

		Status status = stub_->Readdir(&context, file_path, &reply);
		if (!status.ok()) {
			if (status.error_code() == CONN_ERR_CODE) {
				if (Reconnect_To_Server() != 0) {
					cerr << __LINE__ << ": " << "Unable to reconnect to the server"
						<< endl << std::flush;
				}
			}
		}
		return move(reply);
}

// ============================================================================

ReadResponse NFS_Client::Read_File(FileHandle &fh, off_t offset, size_t size) {
	ReadRequest read_req;
	// Populate read request with given values
	read_req.mutable_fh()->set_path(fh.path());
	read_req.mutable_fh()->set_mount_id(fh.mount_id());
	const file_handle *fhp = reinterpret_cast<const file_handle *>
		(fh.handle().c_str());
	read_req.mutable_fh()->set_handle(string((const char*)fhp, 
		fhp->handle_bytes +	sizeof(file_handle)));
	read_req.set_offset(offset);
	read_req.set_size(size);

	ReadResponse resp;
	ClientContext context;

	Status status = stub_->Read(&context, read_req, &resp);
	if (!status.ok()) {
     	cerr << __LINE__ << " : " << "Error in reading file: " << 
				status.error_code() << ": " << status.error_message()
                << std::endl << std::flush;
			if (status.error_code() == CONN_ERR_CODE) {
				if (Reconnect_To_Server() != 0) {
					cerr << __LINE__ << ": " << "Unable to reconnect to the server"
						<< endl << std::flush;
				}
			}
	}

	return move(resp);
}

// ============================================================================

Buffer NFS_Client::Get_File_Attributes(string &&path) {
    LookupMessage file_path;
    file_path.set_path(path);
		
		Buffer reply;
		ClientContext context;

		Status status = stub_->Getattr(&context, file_path, &reply);

		if (!status.ok()) {
			// No need to handle error. The file might not acutally exist and it is
			// all cool. All we need to do is the set the size of the reply to zero to
			// indicate that the stat of the requested file couldn't be found.
			reply.set_size(0);
			if (status.error_code() == CONN_ERR_CODE) {
				if (Reconnect_To_Server() != 0) {
					cerr << __LINE__ << ": " << "Unable to reconnect to the server"
						<< endl << std::flush;
				}
			}
		}
		return move(reply);
}

// ============================================================================

WriteResponse NFS_Client::Write_File(FileHandle& fh, const char *buf,
												off_t offset, size_t size) {
	WriteRequest request;
	request.mutable_fh()->set_path(fh.path());
	request.mutable_fh()->set_mount_id(fh.mount_id());
	const file_handle *fhp =
		reinterpret_cast<const file_handle *> (fh.handle().c_str());
	request.mutable_fh()->set_handle(string((const char*)fhp,
	fhp->handle_bytes + sizeof(file_handle)));

	#ifdef DBG_PRINTS_ENABLED
	cout << "Handle size = " << fhp->handle_bytes + sizeof(file_handle)
		<< " = " << fhp->handle_bytes << " + " << sizeof(file_handle) << endl;
	#endif

	request.set_offset(offset);
	request.set_size(size);

	request.mutable_buff()->set_data(buf, size);
	request.mutable_buff()->set_size(size);
	
	WriteResponse reply;
	ClientContext context;

	Status status = stub_->Write(&context, request, &reply);

	if (!status.ok()) {
		cerr << __LINE__ << ": " <<  status.error_code() << ": "
			<< status.error_message() << std::endl << std::flush;
			if (status.error_code() == CONN_ERR_CODE) {
				if (Reconnect_To_Server() != 0) {
					cerr << __LINE__ << ": " << "Unable to reconnect to the server"
						<< endl << std::flush;
				}
			}
	}

	return move(reply);
}

// ============================================================================
// Calls Asynchronous Write
// ============================================================================

WriteResponse NFS_Client::Write_File_Async(FileHandle& fh, const char *buf,
												off_t offset, size_t size) {
	WriteRequest request;
	request.mutable_fh()->set_path(fh.path());
	request.mutable_fh()->set_mount_id(fh.mount_id());
	const file_handle *fhp =
		reinterpret_cast<const file_handle *> (fh.handle().c_str());
	request.mutable_fh()->set_handle(string((const char*)fhp,
		fhp->handle_bytes + sizeof(file_handle)));

	#ifdef DBG_PRINTS_ENABLED
	cout << "Handle size = " << fhp->handle_bytes + sizeof(file_handle)
		<< " = " << fhp->handle_bytes << " + " << sizeof(file_handle) << endl;
	#endif

	request.set_offset(offset);
	request.set_size(size);

	request.mutable_buff()->set_data(buf, size);
	request.mutable_buff()->set_size(size);
	
	WriteResponse reply;
	ClientContext context;

	Status status = stub_->Write_Async(&context, request, &reply);

	if (!status.ok()) {
		cerr << __LINE__ << ": " <<  status.error_code() << ": "
			<< status.error_message() << std::endl << std::flush;
			if (status.error_code() == CONN_ERR_CODE) {
				if (Reconnect_To_Server() != 0) {
					cerr << __LINE__ << ": " << "Unable to reconnect to the server"
						<< endl << std::flush;
				}
			}
	}

	return move(reply);
}
// ============================================================================

Integer NFS_Client::Create_File(const char *path, mode_t mode) {
	FileCreateRequest request;
	request.set_path(string(path));
	request.set_mode(mode);

	Integer reply;
	ClientContext context;

	Status status = stub_->CreateFile(&context, request, &reply);

	if (!status.ok()) {
		cerr << __LINE__ << ": " <<  status.error_code() << ": "
			<< status.error_message() << std::endl << std::flush;
			if (status.error_code() == CONN_ERR_CODE) {
				if (Reconnect_To_Server() != 0) {
					cerr << __LINE__ << ": " << "Unable to reconnect to the server"
						<< endl << std::flush;
				}
			}
	}
	return move(reply);
}

// ============================================================================

Integer NFS_Client::Delete_File(string&& path) {
	LookupMessage request;
	request.set_path(path);

	Integer reply;
	ClientContext context;

	Status status = stub_->DeleteFile(&context, request, &reply);

	if (!status.ok()) {
		cerr << __LINE__ << ": " <<  status.error_code() << ": "
			<< status.error_message() << std::endl << std::flush;
			if (status.error_code() == CONN_ERR_CODE) {
				if (Reconnect_To_Server() != 0) {
					cerr << __LINE__ << ": " << "Unable to reconnect to the server"
						<< endl << std::flush;
				}
			}
	}
	return move(reply);
}

// ============================================================================

Integer NFS_Client::Create_Directory(string&& path, mode_t mode) {
	FileCreateRequest request;
	request.set_path(string(path));
	request.set_mode(mode);

	Integer reply;
	ClientContext context;

	Status status = stub_->CreateDirectory(&context, request, &reply);

	if (!status.ok()) {
		cerr << __LINE__ << ": " <<  status.error_code() << ": "
				<< status.error_message() << std::endl << std::flush;
		if (status.error_code() == CONN_ERR_CODE) {
			if (Reconnect_To_Server() != 0) {
				cerr << __LINE__ << ": " << "Unable to reconnect to the server"
					<< endl << std::flush;
			}
		}
	}

	return move(reply);
}

// ============================================================================

Integer NFS_Client::Delete_Directory(string&& path) {
	LookupMessage request;
	request.set_path(path);

	Integer reply;
	ClientContext context;

	Status status = stub_->DeleteDirectory(&context, request, &reply);
	
	if (!status.ok()) {
		cerr << __LINE__ << ": " <<  status.error_code() << ": "
				<< status.error_message() << std::endl << std::flush;
		if (status.error_code() == CONN_ERR_CODE) {
			if (Reconnect_To_Server() != 0) {
				cerr << __LINE__ << ": " << "Unable to reconnect to the server"
					<< endl << std::flush;
			}
		}
	}

	return move(reply);
}

// ============================================================================

Integer NFS_Client::Rename_File(string&& from, string&& to, unsigned int flags) {
	RenameRequest request;
	request.set_from_path(from);
	request.set_to_path(to);
	request.set_flags(flags);

	Integer reply;
	ClientContext context;

  Status status = stub_->RenameFile(&context, request, &reply);

	if (!status.ok()) {
		cerr << __LINE__ << ": " <<  status.error_code() << ": "
				<< status.error_message() << std::endl << std::flush;
	}
	return move(reply);
}

// ============================================================================

Integer NFS_Client::Fsync_File(FileHandle &fh) {
	Integer reply;
	ClientContext context;

	Status status = stub_->Fsync(&context, fh, &reply);

	if (!status.ok()) { 
		cerr << __LINE__ << ": " <<  status.error_code() << ": " 
		  << status.error_message() << std::endl << std::flush;
	}
	return move(reply);
}

// ============================================================================

Status NFS_Client::Ping_Server() {
	Integer request;
	request.set_data(0xbadadd);

	Integer reply;
	ClientContext context;
	
	#if 0
	gpr_timespec timeOut;
	timeOut.tv_sec = 0;
	timeOut.tv_nsec = 1000 * 50; // 50 Miliseconds
	timeOut.clock_type = GPR_TIMESPAN;
	context.set_deadline(timeOut);
	#endif
	Status status = stub_->Ping(&context, request, &reply);
	return move(status);
}

// =============================================================================
// ClientFS static function defined below.
// =============================================================================


int ClientFS::getattr(const char *path, struct stat *stbuf, struct fuse_file_info *)
{
	int res = 0;
	#ifdef DBG_PRINTS_ENABLED
	cout << "getattr request for file = " << path << endl << std::flush;
	#endif

	memset(stbuf, 0, sizeof(struct stat));
	Buffer buff = client_ptr->Get_File_Attributes(string(path));
	
	if (buff.size() == 0) {
		// file does not exit.
		return -ENOENT;
	}

	memcpy(stbuf, buff.data().c_str(), sizeof(struct stat));
	return res;
}

// ============================================================================

int ClientFS::readdir(const char *path, void *buf, fuse_fill_dir_t filler,
										off_t, struct fuse_file_info *,
                     enum fuse_readdir_flags) {
	#ifdef DBG_PRINTS_ENABLED
	cout << "Open dir request for file = " << path << endl << std::flush ;
	#endif

	Buffer buff = client_ptr->Read_Directory(string(path));


	string copy = buff.data();
	char *arr = (char*)(copy.c_str());
	char *str = strtok(arr, "\n");
	while (str != nullptr) {
		filler(buf, str, NULL, 0, FUSE_FILL_DIR_PLUS);
		str = strtok(NULL, "\n");
	}
	return 0;
}

// ============================================================================

int ClientFS::open(const char *path, struct fuse_file_info *fi)
{
	#ifdef DBG_PRINTS_ENABLED
	cout << "Open request for file = " << path << endl << std::flush;
	#endif

	if (path == nullptr) {
		return -EBADR;
	}

	FileHandleMap::iterator fh_itr = file_handle_map_.find(string(path));
	// File handle not present 
	if (fh_itr == file_handle_map_.end()) {
		FileHandle fh = client_ptr->Lookup_File(string(path));
		file_handle_map_[string(path)] = move(fh);
	}
	// If file handle already present, then all cool.
	return 0;
}

// ============================================================================

int ClientFS::read(const char *path, char *buf, size_t size, off_t offset,
		              struct fuse_file_info *fi)
{
	#ifdef DBG_PRINTS_ENABLED
	cout << "read request for file = " << path << endl << std::flush;
	#endif
	
	int actual_read;
	// Get the file handle for the given path
	FileHandleMap::iterator fh_itr = file_handle_map_.find(string(path));

	// File Handle not present
	if (fh_itr == file_handle_map_.end()) {
		cerr << __LINE__  << ": No file handle present for " << path;
		return -1;
	}

	ReadResponse rd_resp = client_ptr->Read_File(fh_itr->second, offset, size);
	// Copy the response to the bufferr
	actual_read = rd_resp.actual_read_bytes();
	if (actual_read < 0) {
		return actual_read;
	}

	memcpy(buf, rd_resp.buff().data().c_str(), actual_read);
	
	return actual_read;
}

// ============================================================================

int ClientFS::write(const char *path, const char *buf, size_t size,
								off_t offset, struct fuse_file_info *fi) {
	#ifdef DBG_PRINTS_ENABLED
	cout << "Write request for file = " << path << endl << std::flush;
	#endif

	if (path == nullptr) {
		return -EBADR;
	}	
	
	FileHandleMap::iterator fh_itr = file_handle_map_.find(string(path));
	if (fh_itr == file_handle_map_.end()) {
		return -EBADR;
	}
	
	#if 0
	WriteResponse wresp = client_ptr->Write_File(file_handle_map_[string(path)],
		buf, offset, size);
	#else 
	WriteResponse wresp = client_ptr->Write_File_Async(
		file_handle_map_[string(path)], buf, offset, size);
	#endif
	
	return wresp.actual_bytes_written();
}

// ============================================================================

int ClientFS::create(const char * path, mode_t mode, struct fuse_file_info *fi) {

	#ifdef DBG_PRINTS_ENABLED
	cout << "File create request for file = " << path << " with mode = "
		<< std::hex <<  mode << std::dec << endl << std::flush;
	#endif
	// Request server to create the file.
	if (path == nullptr) {
		return -EINVAL;
	}
	Integer ret_val = client_ptr->Create_File(path, mode);
	int ret = ret_val.data();
	if (ret < 0) { return ret;}
	// Lookup the file to get the file handle.
	ret = open(path, fi);
	return ret;
}

// ============================================================================

int ClientFS::unlink(const char *path) {
	#ifdef DBG_PRINTS_ENABLED
	cout << "File delete request for file = " << path << endl << std::flush;
	#endif

	if (path == nullptr) {
		return -EINVAL;
	}

	Integer ret_val = client_ptr->Delete_File(path);

	if (ret_val.data() == 0) {
		FileHandleMap::iterator itr = file_handle_map_.find(string(path));
		if (itr != file_handle_map_.end()) {
			file_handle_map_.erase(itr);
		}
		return 0;
	}

	return -ENOENT;
}

// ============================================================================

int ClientFS::mkdir (const char *path, mode_t mode) {
	#ifdef DBG_PRINTS_ENABLED
	cout << "Directory creation requested for folder = " << path << endl
			<< std::flush;
	#endif

	if (path == nullptr) {
		return -EINVAL;
	}

	Integer ret_val = client_ptr->Create_Directory(string(path), mode);
	if (ret_val.data() < 0) {
		return ret_val.data();
	}

	return 0;
}

// ============================================================================

int ClientFS::rmdir (const char *path) {
	#ifdef DBG_PRINTS_ENABLED
	cout << "Directory delete request for " << path << endl << std::flush;
	#endif
	if (path == nullptr) {
		return -EINVAL;
	}

	Integer ret_val = client_ptr->Delete_Directory(string(path));
	if (ret_val.data() < 0) {
		return ret_val.data();
	}
	return 0;
}


// ============================================================================

int ClientFS::rename (const char *from_path, const char *to_path,
		unsigned int flags) {
	#ifdef DBG_PRINTS_ENABLED
	cout << "Rename requested from " << from_path << " to " << to_path
			<< endl << std::flush;
	#endif

	if (from_path == nullptr || to_path == nullptr) {
		return -EINVAL;
	}

	Integer ret_val = client_ptr->Rename_File(string(from_path), string(to_path),
		flags);

	if (ret_val.data() < 0) {
		return ret_val.data();
	}
	
	// If file handle is already present for 'from_path' file and we're trying to
	// move it to 'to_path' then we'll have to delete the filehandle entry for
	// 'from_path' file. Moreover, the same handle should be used for 'to_path'.
	FileHandleMap::iterator itr = file_handle_map_.find(string(from_path));
	if (itr != file_handle_map_.end()) {
		file_handle_map_[string(to_path)] = move(itr->second);
		file_handle_map_.erase(itr);
	}

	return 0;
}

// ============================================================================

int ClientFS::fsync (const char *path, int, struct fuse_file_info *fi) {
	
	#ifdef DBG_PRINTS_ENABLED
	cout << "Fsync requested on file " << path << endl << std::flush;
	#endif

	// Get the file handle for the given path
	FileHandleMap::iterator fh_itr = file_handle_map_.find(string(path));

	// File Handle not present
	if (fh_itr == file_handle_map_.end()) {
	  cerr << __LINE__ << ": No file handle present for " << path;
		return -1;
	}
	
	Integer fsync_rc = client_ptr->Fsync_File(fh_itr->second);
	return fsync_rc.data();
}

// ============================================================================

int ClientFS::flush (const char *path, struct fuse_file_info *fi) {
	#ifdef DBG_PRINTS_ENABLED
	cout << "Flush requested" << endl << std::flush;
	#endif
 // Get the file handle for the given path
 FileHandleMap::iterator fh_itr = file_handle_map_.find(string(path));

 // File Handle not present
 if (fh_itr == file_handle_map_.end()) {
 		#ifdef DBG_PRINTS_ENABLED
			cerr << "No file handle present for " << path << endl << std::flush;
		#endif
		return -1;
	}
	
	Integer release_rc = client_ptr->Fsync_File(fh_itr->second);
	return release_rc.data();
}

// ============================================================================

int ClientFS::release (const char *path, struct fuse_file_info *fi) {
	#ifdef DBG_PRINTS_ENABLED
	cout << "Release requested" << endl << std::flush;
	#endif
	return ClientFS::flush(path, fi);
}
// ============================================================================
