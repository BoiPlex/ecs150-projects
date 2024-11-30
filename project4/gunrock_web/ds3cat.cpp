#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

void printStandardError() {
  cerr << "Error reading file" << endl;
}


int main(int argc, char *argv[]) {
  if (argc != 3) {
    cerr << argv[0] << ": diskImageFile inodeNumber" << endl;
    return 1;
  }

  // Parse command line arguments
  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  int inodeNumber = stoi(argv[2]);

  // Get inode
  inode_t inode;
  if (fileSystem->stat(inodeNumber, &inode) < 0 || inode.type != UFS_REGULAR_FILE || inode.size <= 0) {
    printStandardError();
    delete disk;
    delete fileSystem;
    return 1;
  }

  // Get file blocks
  vector<int> blockNums;
  int requiredBlocks = (inode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
  for (int i = 0; i < DIRECT_PTRS && i < requiredBlocks; i++) {
    if (inode.direct[i] != 0) {
      blockNums.push_back(inode.direct[i]);
    }
  }

  // Get file data
  unsigned char buffer[inode.size + 1];
  buffer[inode.size] = '\0';
  if (fileSystem->read(inodeNumber, buffer, inode.size) < 0) {
    printStandardError();
    delete disk;
    delete fileSystem;
    return 1;
  }

  // Start printing
  cout << "File blocks" << endl;
  for (int blockNum : blockNums) {
    cout << blockNum << endl;
  }

  cout << endl;
  
  cout << "File data" << endl;
  cout << buffer;

  delete disk;
  delete fileSystem;

  return 0;
}
