#include<iostream>
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include <fcntl.h>

using namespace std;

int main(int argc, char **argv) {
	if (argc < 2) {
		cout << "Usage: a.out file_name" << endl;
		exit(0);
	}
	int fd = open(argv[1], O_RDWR, "rw+");
	if (fd < 0 ) {
		cout << "Error opening the file" << endl;
		exit(0);
	}

	const int n = 100;
	char arr[n  +1] = {0};
	int read_bytes  = read(fd, arr, n);
	cout << "read " << read_bytes << " bytes: " << arr << endl;
	return 0;

}
