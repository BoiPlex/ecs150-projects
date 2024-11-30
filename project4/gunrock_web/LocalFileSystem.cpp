#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <assert.h>

#include "LocalFileSystem.h"
#include "ufs.h"

using namespace std;


LocalFileSystem::LocalFileSystem(Disk *disk) {
  this->disk = disk;
}

void LocalFileSystem::readSuperBlock(super_t *super) {
  unsigned char superBlock[UFS_BLOCK_SIZE];
  disk->readBlock(0, superBlock);

  memcpy(super, superBlock, sizeof(super_t));
}

void LocalFileSystem::readInodeBitmap(super_t *super, unsigned char *inodeBitmap) {
  for (int i = 0; i < super->inode_bitmap_len; i++) {
    disk->readBlock(super->inode_bitmap_addr + i, inodeBitmap + (i * UFS_BLOCK_SIZE));
  }
}

void LocalFileSystem::writeInodeBitmap(super_t *super, unsigned char *inodeBitmap) {
  for (int i = 0; i < super->inode_bitmap_len; i++) {
    disk->writeBlock(super->inode_bitmap_addr + i, inodeBitmap + (i * UFS_BLOCK_SIZE));
  }
}

void LocalFileSystem::readDataBitmap(super_t *super, unsigned char *dataBitmap) {
  for (int i = 0; i < super->data_bitmap_len; i++) {
    disk->readBlock(super->data_bitmap_addr + i, dataBitmap + (i * UFS_BLOCK_SIZE));
  }
}

void LocalFileSystem::writeDataBitmap(super_t *super, unsigned char *dataBitmap) {
  for (int i = 0; i < super->data_bitmap_len; i++) {
    disk->writeBlock(super->data_bitmap_addr + i, dataBitmap + (i * UFS_BLOCK_SIZE));
  }
}

void LocalFileSystem::readInodeRegion(super_t *super, inode_t *inodes) {
  for (int i = 0; i < super->inode_region_len; i++) {
    disk->readBlock(super->inode_region_addr + i, reinterpret_cast<unsigned char *>(inodes) + (i * UFS_BLOCK_SIZE));
  }
}

void LocalFileSystem::writeInodeRegion(super_t *super, inode_t *inodes) {
  for (int i = 0; i < super->inode_region_len; i++) {
    disk->writeBlock(super->inode_region_addr + i, reinterpret_cast<unsigned char *>(inodes) + (i * UFS_BLOCK_SIZE));
  }
}

int LocalFileSystem::lookup(int parentInodeNumber, string name) {
  super_t super;
  readSuperBlock(&super);

  // Read inode table
  inode_t *inodes = new inode_t[super.num_inodes];
  readInodeRegion(&super, inodes);

  // Validate parent inode
  if (parentInodeNumber < 0 || parentInodeNumber >= super.num_inodes || inodes[parentInodeNumber].type != UFS_DIRECTORY) {
    delete[] inodes;
    return -EINVALIDINODE;
  }

  inode_t &parentInode = inodes[parentInodeNumber];

  // Iterate over blocks to find name
  for (unsigned int i = 0; i < DIRECT_PTRS && parentInode.direct[i] != 0; i++) {
    unsigned char block[UFS_BLOCK_SIZE];
    int bytesRead = LocalFileSystem::read(parentInodeNumber, block, UFS_BLOCK_SIZE);
    if (bytesRead < 0) {
      delete[] inodes;
      return bytesRead; // Pass error from read
    }

    dir_ent_t *entries = reinterpret_cast<dir_ent_t *>(block);
    int numEntries = UFS_BLOCK_SIZE / sizeof(dir_ent_t);

    for (int j = 0; j < numEntries; j++) {
      // dir_ent_t entry = entries[i];
      if (entries[j].inum != -1 && name == entries[j].name) {
        int inodeNumber = entries[j].inum;
        delete[] inodes;
        return inodeNumber; // Found inode number
      }
    }
  }

  // // Iterate over blocks to find name
  // for (unsigned int i = 0; i < DIRECT_PTRS && parentInode.direct[i] != 0; i++) {
  //   unsigned char block[UFS_BLOCK_SIZE];
  //   disk->readBlock(parentInode.direct[i], block);

  //   dir_ent_t *entries = reinterpret_cast<dir_ent_t *>(block);
  //   int numEntries = UFS_BLOCK_SIZE / sizeof(dir_ent_t);

  //   for (int j = 0; j < numEntries; j++) {
  //     // dir_ent_t entry = entries[i];
  //     if (entries[j].inum != -1 && name == entries[j].name) {
  //       int inodeNumber = entries[j].inum;
  //       delete[] inodes;
  //       return inodeNumber; // Found inode number
  //     }
  //   }
  // }

  delete[] inodes;
  return -ENOTFOUND;
}

int LocalFileSystem::stat(int inodeNumber, inode_t *inode) {
  super_t super;
  readSuperBlock(&super);

  if (inodeNumber < 0 || inodeNumber > super.num_inodes) {
    return -EINVALIDINODE;
  }

  inode_t *inodes = new inode_t[super.num_inodes];
  readInodeRegion(&super, inodes);

  *inode = inodes[inodeNumber];
  delete[] inodes;
  return 0;
}

int LocalFileSystem::read(int inodeNumber, void *buffer, int size) {
  super_t super;
  readSuperBlock(&super);

  if (inodeNumber < 0 || inodeNumber > super.num_inodes) {
    return -EINVALIDINODE;
  }

  inode_t *inodes = new inode_t[super.num_inodes];
  readInodeRegion(&super, inodes);
  
  inode_t &inode = inodes[inodeNumber];

  if (size < 0 || size > MAX_FILE_SIZE) {
    delete[] inodes;
    return -EINVALIDSIZE;
  }

  int bytesRead = 0;
  unsigned char *outputBuffer = static_cast<unsigned char *>(buffer);

  // Directory
  if (inode.type == UFS_DIRECTORY) {

    // Iterate over blocks
      for (unsigned int i = 0; i < DIRECT_PTRS && inode.direct[i] != 0; i++) {
        unsigned char block[UFS_BLOCK_SIZE];
        disk->readBlock(inode.direct[i], block);

        int bytesToCopy = min(size - bytesRead, UFS_BLOCK_SIZE);
        memcpy(outputBuffer + bytesRead, block, bytesToCopy);
        bytesRead += bytesToCopy;

        if (bytesRead >= size) {
          break;
        }
      }
  }
  // Regular file
  else if (inode.type == UFS_REGULAR_FILE) {
    while (bytesRead < size) {
      int curBlockIndex = bytesRead / UFS_BLOCK_SIZE;
      int blockOffset = bytesRead % UFS_BLOCK_SIZE;

      if (curBlockIndex >= DIRECT_PTRS || inode.direct[curBlockIndex] == 0) {
        delete[] inodes;
        return bytesRead;
      }

      unsigned char block[UFS_BLOCK_SIZE];
      disk->readBlock(inode.direct[curBlockIndex], block);

      int bytesToCopy = min(UFS_BLOCK_SIZE - blockOffset, size - bytesRead);
      memcpy(outputBuffer + bytesRead, block + blockOffset, bytesToCopy);

      bytesRead += bytesToCopy;

      if (bytesRead >= size) {
        break;
      }
    }
  }
  // Not a file or directory
  else {
    delete[] inodes;
    return -EINVALIDINODE;
  }

  delete[] inodes;
  return bytesRead;
}

int LocalFileSystem::create(int parentInodeNumber, int type, string name) {
  if (name.length() > DIR_ENT_NAME_SIZE) {
    return -EINVALIDNAME;
  }

  // Get super block
  super_t super;
  readSuperBlock(&super);

  // Get parent inode
  inode_t parentInode;
  if (LocalFileSystem::stat(parentInodeNum, &parentInode) < 0 || parentInode.type != UFS_DIRECTORY || parentInode.size <= 0) {
    return -EINVALIDINODE;
  }

  vector<dir_ent_t> entries;
  if (LocalFileSystem::read(parentInodeNum, entries.data(), parentInode.size()) < 0) {
    return -ENOTENOUGHSPACE;
  }

  // Check if name already exists
  if (dir_ent_t entry : entries) {
    if (name == entry.name) {
      inode_t existingInode;
      if (LocalFileSystem::stat(entry.inum, &existingInode)) {
        return -EINVALIDINODE;
      }
      if (entry.type == type) {
        return existingInode.inum; // Success (already exists with correct type)
      }
      else {
        return -EINVALIDTYPE;
      }
    }
  }

  unsigned char *inodeBitmap[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  readInodeBitmap(&super, inodeBitmap);

  int newInodeNum = -1;
  

  if (type == UFS_DIRECTORY) {

  }
  else if (type == UFS_REGULAR_FILE) {

  }
  // Not a file or directory
  else {
    return -EINVALIDTYPE;
  }  

  return 0;
}

int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {
  super_t super;
  readSuperBlock(&super);

  if (inodeNumber < 0 || inodeNumber > super.num_inodes) {
    return -EINVALIDINODE;
  }

  inode_t *inodes = new inode_t[super.num_inodes];
  readInodeRegion(&super, inodes);
  
  inode_t &inode = inodes[inodeNumber];

  if (size < 0 || size > MAX_FILE_SIZE) {
    delete[] inodes;
    return -EINVALIDSIZE;
  }

  if (node.type != UFS_REGULAR_FILE) {
    delete[] inodes;
    return -EINVALIDTYPE;
  }

  int bytesWritten = 0;
  unsigned char *inputBuffer = static_cast<unsigned char *>(buffer);

  while (bytesWritten < size) {
    int curBlockIndex = bytesWritten / UFS_BLOCK_SIZE;
    int blockOffset = bytesWritten % UFS_BLOCK_SIZE;

    if (curBlockIndex >= DIRECT_PTRS) {
      delete[] inodes;
      return -ENOTENOUGHSPACE;
    }

    unsigned char block[UFS_BLOCK_SIZE];
    disk->readBlock(inode.direct[curBlockIndex], block);

    int bytesToCopy = min(UFS_BLOCK_SIZE - blockOffset, size - bytesWritten);
    memcpy(block + blockOffset, inputBuffer + bytesWritten, bytesToCopy);

    disk->writeBlock(inode.direct[curBlockIndex], block);

    bytesWritten += bytesToCopy;
  }

  // Update inode size
  if (bytesWritten > inode.size) {
    inode.size = bytesWriten;
    LocalFileSystem::writeInodeRegion(&super, inodes);
  }

  delete[] inodes;
  return bytesWritten;
}

int LocalFileSystem::unlink(int parentInodeNumber, string name) {


  return 0;
}
