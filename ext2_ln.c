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
  char *source;
  char *dest;

  int symlink_flag = 0;

  if(argc == 4) {
    source = argv[2];
    dest = argv[3];
  } else if (argc == 5) {
    source = argv[3];
    dest = argv[4];
    symlink_flag = 1;
  } else {
      fprintf(stderr, "Usage: <image file name> (-s) <absolute source path in disk> <absolute dest path in disk>\n");
      exit(1);
  }
  int fd = open(argv[1], O_RDWR);

  disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  if(disk == MAP_FAILED) {
      perror("mmap");
      exit(1);
  }

  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  struct ext2_inode *inode_table = (struct ext2_inode *)(disk +
    EXT2_BLOCK_SIZE * gd->bg_inode_table);

  // char *source_parent_path = get_parent_directory_path(source);
  // struct ext2_dir_entry *source_parent_entry = path_walker(disk, source_parent_path);

  struct ext2_dir_entry *source_file_entry = path_walker(disk, source);
  struct ext2_inode *source_inode = &inode_table[source_file_entry->inode - 1];

  char *dest_parent_path = get_parent_directory_path(dest);
  struct ext2_dir_entry *dest_parent_entry = path_walker(disk, dest_parent_path);

  struct ext2_dir_entry *dest_file_entry = path_walker(disk, dest);
  char *dest_file_name = get_last_item_name(dest);

  if (!source_file_entry) {
    fprintf(stderr, "Source file does not exist\n");
    return ENOENT;
  }

  // Check if hard Links
  if (symlink_flag == 0 && source_file_entry->file_type == EXT2_FT_DIR) {
    fprintf(stderr, "source is a directory\n");
    return EISDIR;
  }

  if (dest_file_entry) {
    fprintf(stderr, "Link name already exists\n");
    return EEXIST;
  }

  struct ext2_inode *dest_parent_inode = &inode_table[dest_parent_entry->inode - 1];

  // Create hard links
  if (symlink_flag == 0) {
    // Add a new file or link to the dest parent inode.
    for (int i = 0; i < 12; i++) {
      if (dest_parent_inode->i_block[i] == 0) {
        int new_block_number = get_free_block_number(disk, dest_parent_inode, i);
        add_entry_in_new_block(disk, dest_parent_inode, source_file_entry->file_type, source_file_entry->inode, i, dest_file_name, new_block_number);
      } else {
        if (check_space_available(disk, dest_parent_inode, i, dest_file_name) == 1) {
          add_entry_in_used_block(disk, dest_parent_inode, source_file_entry->file_type, source_file_entry->inode, i, dest_file_name);
        }
      }
    }
    source_inode->i_links_count = source_inode->i_links_count + 1;
  } else {
    // Create soft links.

    int free_inode_number = get_free_inode_number(disk);
    struct ext2_inode *free_inode = &inode_table[free_inode_number];
    free_inode->i_mode = EXT2_S_IFLNK;
    free_inode->i_ctime = time(NULL);
    free_inode->i_links_count = 1;
    free_inode->i_blocks = 2;
    free_inode->i_size = strlen(dest_file_name);

    int free_block_number = get_free_block_number_simple(disk);

    char *free_block = (char *)(disk + free_block_number * EXT2_BLOCK_SIZE);

    // Write the source path to the free block.
    strncpy(free_block, source, strlen(source));

    // Add a new link to the dest parent inode.
    for (int i = 0; i < 12; i++) {
      if (dest_parent_inode->i_block[i] == 0) {
        int new_block_number = get_free_block_number(disk, dest_parent_inode, i);
        add_entry_in_new_block(disk, dest_parent_inode, EXT2_FT_SYMLINK, free_inode_number, i, dest_file_name, new_block_number);
      } else {
        if (check_space_available(disk, dest_parent_inode, i, dest_file_name) == 1) {
          add_entry_in_used_block(disk, dest_parent_inode, EXT2_FT_SYMLINK, free_inode_number, i, dest_file_name);
        }
      }
    }

  }


  return 0;
}
