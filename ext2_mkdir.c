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
  // /level1/level2/new. The parent path is /level1/level2.
  char *parent_path = get_parent_directory_path(path);
  struct ext2_dir_entry *parent_entry = path_walker(disk, parent_path);
  char *new_directory = get_last_item_name(path);
  if (parent_entry == NULL) {
    fprintf(stderr, "At least one directory in your absolute path does not exist. Error code: %d\n", ENOENT);
    return ENOENT;
  }
  struct ext2_inode *parent_directory_inode = &inode_table[parent_entry->inode-1];

  // Check if the directory you are about to create already exists.
  for (int i = 0; i < 12; i++) {
    if(parent_directory_inode->i_block[i] != 0) {
      struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * parent_directory_inode->i_block[i]);
      int rec_len_sum = 0;
      while (rec_len_sum < 1024) {
        if (strncmp(new_directory, entry->name, strlen(new_directory)) == 0) {
          fprintf(stderr, "The directory you are trying to create already exists. Error code: %d\n", EEXIST);
          return EEXIST;
        }
        rec_len_sum += entry->rec_len;
        entry = (struct ext2_dir_entry *)(disk + 1024* parent_directory_inode->i_block[i] + rec_len_sum);
      }
    }
  }

  int free_inode_number = get_free_inode_number(disk);

  struct ext2_inode *free_inode = &inode_table[free_inode_number - 1];
  // Allocate the free inode.
  free_inode->i_mode = EXT2_S_IFDIR;
  free_inode->i_size = EXT2_BLOCK_SIZE;
  free_inode->i_ctime = time(NULL);
  free_inode->i_blocks = 2;  // Since 1 block = 2 disk sectors
  // Parent and itself; Directory link = 2. File link = 1
  free_inode->i_links_count = 2;
  free_inode->osd1 = 0;
  free_inode->i_gid = 0;
  free_inode->i_generation = 0;
  free_inode->i_block[0] = 0;

  // Add the '.' and '..' directories to the new directory block for the new inode free_inode.
  int block_number = get_free_block_number(disk, free_inode, 0);
  add_entry_in_new_block(disk, free_inode, EXT2_FT_DIR, free_inode_number, 0, ".", block_number);
  // .. is the parent directory. So use parent inode's inode number.
  add_entry_in_used_block(disk, free_inode, EXT2_FT_DIR, parent_entry->inode, 0, "..");


  // Add a new directory entry for the new directory to the data block pointer array in the inode of the new directory's parent directory.
  for (int i = 0; i < 12; i++) {
    if (parent_directory_inode->i_block[i] == 0) {
      int new_block_number = get_free_block_number(disk, parent_directory_inode, i);
      add_entry_in_new_block(disk, parent_directory_inode, EXT2_FT_DIR, free_inode_number, i, new_directory, new_block_number);
    } else {
      if (check_space_available(disk, parent_directory_inode, i, new_directory) == 1) {
        add_entry_in_used_block(disk, parent_directory_inode, EXT2_FT_DIR, free_inode_number, i, new_directory);
      }
    }
  }
  free(parent_path);
  return 0;
}
