#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

void printBitmap(unsigned char* bitmap, int bytes) {
  for (int i = 0; i < bytes; i++) {
    cout << (unsigned int)bitmap[i] << " ";
  }
  cout << endl;
}


int main(int argc, char *argv[]) {
  if (argc != 2) {
    cerr << argv[0] << ": diskImageFile" << endl;
    return 1;
  }

  // Parse command line arguments
  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);

  // Super block
  super_t super;
  fileSystem->readSuperBlock(&super);

  // Inode bitmap
  int inodeBitmapBytes = super.inode_bitmap_len * UFS_BLOCK_SIZE;
  unsigned char inodeBitmap[inodeBitmapBytes];
  fileSystem->readInodeBitmap(&super, inodeBitmap);

  // Data bitmap
  int dataBitmapBytes = super.data_bitmap_len * UFS_BLOCK_SIZE;
  unsigned char dataBitmap[dataBitmapBytes];
  fileSystem->readDataBitmap(&super, dataBitmap);

  cout << "Super" << endl;
  cout << "inode_region_addr " << super.inode_region_addr << endl;
  cout << "inode_region_len " << super.inode_region_len << endl;
  cout << "num_inodes " << super.num_inodes << endl;
  cout << "data_region_addr " << super.data_region_addr << endl;
  cout << "data_region_len " << super.data_region_len << endl;
  cout << "num_data " << super.num_data << endl;
  cout << endl;

  cout << "Inode bitmap" << endl;
  printBitmap(inodeBitmap, super.num_inodes / 8);
  cout << endl;

  cout << "Data bitmap" << endl;
  printBitmap(dataBitmap, super.num_data / 8);

  // Super
  // inode_region_addr 3
  // inode_region_len 1
  // num_inodes 32
  // data_region_addr 4
  // data_region_len 32
  // num_data 32

  // Inode bitmap
  // 15 0 0 0 

  // Data bitmap
  // 15 0 0 0 


  delete disk;
  delete fileSystem;

  return 0;
}
