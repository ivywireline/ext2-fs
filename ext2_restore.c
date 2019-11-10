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
  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  struct ext2_inode *inode_table = (struct ext2_inode *)(disk +
    EXT2_BLOCK_SIZE * gd->bg_inode_table);

  char *parent_path = get_parent_directory_path(path);
  struct ext2_dir_entry *parent_entry = path_walker(disk, parent_path);

  struct ext2_dir_entry *entry_check = path_walker(disk, path);

  if (parent_entry == NULL) {
    fprintf(stderr, "At least one directory in your absolute path does not exist. Error code: %d\n", ENOENT);
    return ENOENT;
  }

  struct ext2_inode *parent_directory_inode = &inode_table[parent_entry->inode-1];

  // Check if the file you are about to restore already exists.
  if (entry_check) {
    fprintf(stderr, "File already exists.\n");
    return EEXIST;
  }

  // Search through extra space to find the deleted file.

  char *entry_to_restore_name = get_last_item_name(path);

  for (int i = 0; i < 12; i++) {
    if (parent_directory_inode->i_block[i] == 0) {
      break;
    }
    struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * parent_directory_inode->i_block[i]);
    int rec_len_sum = 0;
    // Iterate over the current block.
    while (rec_len_sum < 1024) {
      // Find the real length of the current entry to see if there is space available
      // after this entry
      int real_entry_length = 0;
      if (cur_entry->name_len % 4 == 0){
        real_entry_length = 8 + cur_entry->name_len;
      } else {
        real_entry_length = 8 + cur_entry->name_len + (4 - (cur_entry->name_len % 4));
      }

      int extra_space = cur_entry->rec_len - real_entry_length;
      // char is one byte; We are moving the pointer to the beginning of extra space.
      struct ext2_dir_entry *deleted_entry = (struct ext2_dir_entry *)((char *)(cur_entry) + real_entry_length);

      int space_already_checked = 0;
      while (space_already_checked <= extra_space) {
        if (deleted_entry->file_type == EXT2_FT_REG_FILE || deleted_entry->file_type == EXT2_FT_SYMLINK) {
          // Found the deleted_entry. Check inode and blocks conditions, if they pass, restore.
          if (strncmp(deleted_entry->name, entry_to_restore_name, strlen(entry_to_restore_name)) == 0) {
            int inode_status = get_inode_status(disk, deleted_entry->inode);
            // File inode has not been overwritten yet.
            if (inode_status == 1 || inode_status == -1) {
              fprintf(stderr, "Inode has been used by another file.\n");
              return ENOENT;
            }

            struct ext2_inode *deleted_inode = &inode_table[deleted_entry->inode - 1];
            // Check blocks status
            int i;
            for(i = 0; i < 12; i++) {
              if (deleted_inode->i_block[i] == 0) {
                break;
              } else {
                if (get_block_status(disk, deleted_inode->i_block[i]) == 1 || get_block_status(disk, deleted_inode->i_block[i]) == -1) {
                  fprintf(stderr, "One or more blocks have been used by another file.\n");
                  return ENOENT;
                }
              }
            }

            // Can start restoring.
            // update the current entry's rec_len
            // Not entry_to_restore_length since we need to account for the extra space padding
            deleted_entry->rec_len = extra_space - space_already_checked;
            // update the cur_entry rec_len to its real_entry_length
            cur_entry->rec_len = real_entry_length;
            // no longer deleted;
            deleted_inode->i_dtime = 0;
            deleted_inode->i_links_count = deleted_inode->i_links_count + 1;

            // Set inode and block bits back to 1.
            if (flip_inode_bit(disk, deleted_entry->inode, 1) == -1) {
              fprintf(stderr, "Flipping inode bitmap errored out\n");
              return ENOENT;
            }

            for (int i = 0; i < 12; i++) {
              if (deleted_inode->i_block[i] == 0) {
                break;
              }
              if (flip_block_bit(disk, deleted_inode->i_block[i], 1) == -1) {
                fprintf(stderr, "Flipping block bitmap errored out\n");
                return ENOENT;
              }
            }
            // Success!
            return 0;
          }
        }
        space_already_checked = space_already_checked + deleted_entry->rec_len;
        deleted_entry = (struct ext2_dir_entry *)((char *)(cur_entry) + deleted_entry->rec_len);
      }
      rec_len_sum += cur_entry->rec_len;
      cur_entry = (struct ext2_dir_entry *)(disk + 1024 * parent_directory_inode->i_block[i] + rec_len_sum);
    }
  }
  fprintf(stderr, "Reached the end of the file. Restore file failed.\n");
  return ENOENT;
}
