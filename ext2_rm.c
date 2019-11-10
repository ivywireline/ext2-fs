#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include "ext2.h"
#include "helper.h"
#include <string.h>

unsigned char *disk;

char *path;

int main(int argc, char **argv) {

  if(argc != 3) {
      fprintf(stderr, "Usage: <image file name> <absolute path in disk> \n");
      exit(1);
  }
  int fd = open(argv[1], O_RDWR);

  disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(disk == MAP_FAILED) {
      perror("mmap");
      exit(1);
  }
  path = argv[2];
  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  struct ext2_inode *inode_table = (struct ext2_inode *)(disk +
    EXT2_BLOCK_SIZE * gd->bg_inode_table);

  char *parent_directory_path = get_parent_directory_path(path);
  struct ext2_dir_entry *parent_entry = path_walker(disk, parent_directory_path);
  struct ext2_dir_entry *entry_to_remove = path_walker(disk, path);

  // File does not exist.
  if (!entry_to_remove || !parent_entry) {
    fprintf(stderr, "File does not exist\n");
    return ENOENT;
  }

  // File is a directory
  if (entry_to_remove->file_type == EXT2_FT_DIR) {
    fprintf(stderr, "File is a directory error\n");
    return EISDIR;
  }

  // Store the entry_to_remove information to variables
  int to_remove_inode_number = entry_to_remove->inode;
  struct ext2_inode *inode_to_remove = &inode_table[to_remove_inode_number - 1];
  inode_to_remove->i_dtime = time(NULL);
  if (inode_to_remove->i_links_count > 0) {
    inode_to_remove->i_links_count = inode_to_remove->i_links_count - 1;
  }
  // Find the parent inode
  int parent_inode_number = parent_entry->inode;
  struct ext2_inode *parent_inode = &inode_table[parent_inode_number-1];
  char *last_item_name = get_last_item_name(path);
  int done = 0;

  int block_to_remove = 0;
  // Update the directory entries.
  for (int i = 0; i < 12; i++) {
    struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(disk + parent_inode->i_block[i] * EXT2_BLOCK_SIZE);
    struct ext2_dir_entry *previous_entry = parent_entry;
    int rec_len_sum = 0;
    while(rec_len_sum < 1024) {
      // If file is found.
      if (strncmp(entry->name, last_item_name, strlen(last_item_name)) == 0) {
        if (entry->rec_len == 1024) {
          entry->rec_len = 0;
          block_to_remove = parent_inode->i_block[i];
        } else if (previous_entry != NULL) {
          // This entry is first entry.
          previous_entry->rec_len = previous_entry->rec_len + entry->rec_len;
        } else {
          // This entry is not the first.
          // Let the next entry overwrite the current entry.
          struct ext2_dir_entry *next_entry = (struct ext2_dir_entry *)((char *)(entry) + entry->rec_len);
          entry->inode = next_entry->inode;
          entry->rec_len = entry->rec_len + next_entry->rec_len;
          entry->name_len = next_entry->name_len;
          entry->file_type = EXT2_FT_REG_FILE;
          strncpy(entry->name, next_entry->name, strlen(next_entry->name));
        }
        done = 1;
        break;
      }
      rec_len_sum = rec_len_sum + entry->rec_len;
      previous_entry = entry;
      entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * parent_inode->i_block[i] + rec_len_sum);
    }
    if (done == 1) {
      break;
    }
  }

  if (done == 1) {
    // Update the inode bitmap.
    int inode_done = 0;
    unsigned char *inode_bitmap = (unsigned char *)(disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);
    int inode_number = 1;
    for (int byte = 0; byte < sb->s_inodes_count / 8; byte++) {
      for(int bit = 0; bit < 8; bit++) {
        if ((inode_bitmap[byte] & (1 << bit)) && inode_number == to_remove_inode_number) {
          // Found the free inode. Return the offset and set the used bit to 0).
          inode_bitmap[byte] = inode_bitmap[byte] & ~(1 << bit);
          sb->s_free_inodes_count++;
          inode_done = 1;
          break;
        }
        inode_number++;
      }
      if (inode_done == 1){
        break;
      }
    }
    int block_done = 0;
    // A parent block is to be removed.
    if (block_to_remove != 0) {
      // Update block bitmap.
      unsigned char *block_bitmap = (unsigned char *)(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
      int block_number = 1;
      for (int byte = 0; byte < sb->s_blocks_count / 8; byte++) {
        for(int bit = 0; bit < 8; bit++) {
          if (block_number == block_to_remove) {
            // Found the block. Set the bit to 0
            block_bitmap[byte] = block_bitmap[byte] & ~(1 << bit);
            sb->s_free_blocks_count++;
            gd->bg_free_blocks_count++;
            block_done = 1;
            break;
          }
          block_number++;
        }
        if (block_done == 1){
          break;
        }
      }
    }

    for(int i = 0; i < 12; i++) {
      // Means there is something to remove
      if (inode_to_remove->i_block[i] != 0) {
        // Update block bitmap.
        unsigned char *block_bitmap = (unsigned char *)(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
        int block_number = 1;
        for (int byte = 0; byte < sb->s_blocks_count / 8; byte++) {
          for(int bit = 0; bit < 8; bit++) {
            if (block_number == inode_to_remove->i_block[i]) {
              // Found the block.  Set the bit to 0
              block_bitmap[byte] = block_bitmap[byte] & ~(1 << bit);
              sb->s_free_blocks_count++;
              gd->bg_free_blocks_count++;
            }
            block_number++;
          }
        }
      }
    }
  }

  return 0;
}
