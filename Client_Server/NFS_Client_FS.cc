#include "NFS_Client_FS.h"

#include <iostream>
#include <stdio.h>

// include in one .cpp file
#include "../include/Fuse-impl.h"

using namespace std;

// ============================================================================

FileHandleMap ClientFS::file_handle_map_;
unique_ptr<NFS_Client> ClientFS::client_ptr;

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
    if (status.ok()) {
			cout << "File " << reply.path() << " opened." << endl << std::flush;
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl << std::flush;

			int sleep_time_ms = 50;
			const int RETRY_TIMEOUT = 200; /* 10 seconds */
			int iterations = 0;
			while(!status.ok() && iterations < RETRY_TIMEOUT) {
				Status status = stub_->Lookup(&context, file_path, &reply);
				iterations++;
				this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
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
			if (status.error_code() == 14 /* Server crashed */) {
				
			}
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl << std::flush;
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
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl << std::flush;
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
		if (status.ok()) {
		} else {
      cout << status.error_code() << ": " << status.error_message()
                << std::endl << std::flush;
			reply.set_size(0);
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
	// 	fh.handle().size()));
	// cout << "Handle size = " << fh.handle().size() << endl;
	fhp->handle_bytes + sizeof(file_handle)));
	cout << "Handle size = " << fhp->handle_bytes + sizeof(file_handle)
		<< " = " << fhp->handle_bytes << " + " << sizeof(file_handle) << endl;

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
	}

	return move(reply);
}

// =============================================================================
// ClientFS static function defined below.
// =============================================================================


int ClientFS::getattr(const char *path, struct stat *stbuf, struct fuse_file_info *)
{
	int res = 0;
	cout << "getattr request for file = " << path << endl << std::flush ;

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
	cout << "Open dir request for file = " << path << endl << std::flush ;

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
	cout << "Open request for file = " << path << endl << std::flush;

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
	cout << "read request for file = " << path << endl << std::flush;
	
	int actual_read;
	// Get the file handle for the given path
	FileHandleMap::iterator fh_itr = file_handle_map_.find(string(path));

	// File Handle not present
	if (fh_itr == file_handle_map_.end()) {
		cout << "No file handle present for " << path;
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
	cout << "Write request for file = " << path << endl << std::flush;

	if (path == nullptr) {
		return -EBADR;
	}	
	
	FileHandleMap::iterator fh_itr = file_handle_map_.find(string(path));
	if (fh_itr == file_handle_map_.end()) {
		return -EBADR;
	}
	
	WriteResponse wresp = client_ptr->Write_File(file_handle_map_[string(path)],
		buf, offset, size);
	
	return wresp.actual_bytes_written();
}

// ============================================================================

int ClientFS::create(const char * path, mode_t mode, struct fuse_file_info *fi) {
	cout << "File create request for file = " << path << " with mode = "
	<< std::hex <<  mode << std::dec << endl << std::flush;
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
	cout << "File delete request for file = " << path << endl << std::flush;

	if (path == nullptr) {
		return -EINVAL;
	}

	Integer ret_val = client_ptr->Delete_File(path);
	if (ret_val.data() == 0) {
		return 0;
	}

	return -ENOENT;
}

// ============================================================================

int ClientFS::mkdir (const char *path, mode_t mode) {
	cout << "Directory creation requested for folder = " << path << endl
			<< std::flush;

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
	cout << "Directory delete request for " << path << endl << std::flush;
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


// ============================================================================



// ============================================================================
