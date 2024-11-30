#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

#include "StringUtils.h"
#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

void printStandardError() {
  cerr << "Directory not found" << endl;
}

// Use this function with std::sort for directory entries
bool compareByName(const dir_ent_t& a, const dir_ent_t& b) {
    return std::strcmp(a.name, b.name) < 0;
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    cerr << argv[0] << ": diskImageFile directory" << endl;
    cerr << "For example:" << endl;
    cerr << "    $ " << argv[0] << " tests/disk_images/a.img /a/b" << endl;
    return 1;
  }

  // Parse command line arguments
  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  string directoryPath = string(argv[2]);
  directoryPath.erase(directoryPath.find_last_not_of(" \n\r\t") + 1);

  // Traverse the directory path
  vector<string> items = StringUtils::split(directoryPath, '/');
  int curInode = UFS_ROOT_DIRECTORY_INODE_NUMBER;

  for (unsigned int i = 0; i < items.size(); i++) {
    string curItem = items[i];
    if (curItem.empty()) {
      continue;
    }
    int nextInode = fileSystem->lookup(curInode, curItem);
    if (nextInode < 0) {
      printStandardError();
      delete disk;
      delete fileSystem;
      return 1;
    }
    curInode = nextInode;    
  }

  // Get the inode
  inode_t inode;
  int inodeNum = curInode;
  if (fileSystem->stat(inodeNum, &inode) < 0) {
    printStandardError();
    delete disk;
    delete fileSystem;
    return 1;
  }

  // Directory file
  if (inode.type == UFS_DIRECTORY) {
    vector<dir_ent_t> entries;

    // Read directory entries from inode
    unsigned char *buffer = new unsigned char[inode.size];
    if (fileSystem->read(inodeNum, buffer, inode.size) < 0) {
      printStandardError();
      delete[] buffer;
      delete disk;
      delete fileSystem;
      return 1;
    }

    // Cast and append to entries vector
    dir_ent_t *entryArray = reinterpret_cast<dir_ent_t *>(buffer);
    for (unsigned int i = 0; i < inode.size / sizeof(dir_ent_t); i++) {
      if (entryArray[i].inum != -1) {
        entries.push_back(entryArray[i]);
      }
    }
    delete[] buffer;

    // Sort and print entries
    sort(entries.begin(), entries.end(), compareByName);
    for (const dir_ent_t &entry : entries) {
      cout << entry.inum << "\t" << entry.name << endl;
    }
  }
  // Regular file
  else if (inode.type == UFS_REGULAR_FILE) {
    cout << inodeNum << "\t" << directoryPath.substr(directoryPath.find_last_of('/') + 1) << endl;
  }
  // Other file types
  else {
    printStandardError();
    delete disk;
    delete fileSystem;
    return 1;
  }

  // Deallocate memory
  delete disk;
  delete fileSystem;

  return 0;
}
