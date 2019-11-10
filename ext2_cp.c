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

char *dest;
char *source;
char *parent_path;
char *new_file;

int main(int argc, char **argv) {

  if(argc != 4) {
      fprintf(stderr, "Usage: <image file name> <path in os> <absolute path in disk> \n");
      exit(1);
  }
  int fd = open(argv[1], O_RDWR);

  disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(disk == MAP_FAILED) {
      perror("mmap");
      exit(1);
  }

  source = argv[2];
  dest = argv[3];

  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  struct ext2_inode *inode_table = (struct ext2_inode *)(disk +
    EXT2_BLOCK_SIZE * gd->bg_inode_table);
  
  char *dest_file = get_last_item_name(dest);
  char *source_file = get_last_item_name(source);


  //find parent entry
  if(check_is_file(dest_file)){ //if it's a file
    parent_path = get_parent_directory_path(dest);
    new_file = dest_file;
  } else{ //if it's a directory
    parent_path = dest;
    new_file = source_file;
  }
  struct ext2_dir_entry *parent_entry = path_walker(disk, parent_path);
  //check if destination path is valid
  //struct ext2_dir_entry *dest_entry = path_walker(disk, dest);
  if (parent_entry == NULL) {
    fprintf(stderr, "At least one directory in your absolute path does not exist. Error code: %d\n", ENOENT);
    return ENOENT;
  }
  struct ext2_inode *parent_directory_inode = &inode_table[parent_entry->inode-1];

  //check if the destination file already existed
  for (int i=0; i<12; i++){
    if (parent_directory_inode->i_block[i] == 0) {
        // block pointer number is 0 which means it has no block pointer.
        break;
      }
    struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(disk + parent_directory_inode->i_block[i] * EXT2_BLOCK_SIZE);
    int rec_len_sum = 0;
    while(rec_len_sum < 1024) {
      rec_len_sum = rec_len_sum + entry->rec_len;
      entry = (struct ext2_dir_entry *)(disk + 1024 * parent_directory_inode->i_block[i] + rec_len_sum);
      if (strncmp(new_file, entry->name, strlen(new_file)+1) == 0){
        fprintf(stderr, "The file you're trying to copy already exists. Error code: %d\n", EEXIST);
        return EEXIST;
      }
    }
      
  }

  //open source file
  FILE * file;
  file = fopen(source, "r");
  if (!file){
    //file doesn't exists or cannot be opened (es. you don't have access permission )
    //source file does not exist
    fprintf(stderr, "The source file does not exist or cannot be accessed/opened. Error code: %d\n", ENOENT);
    fclose(file);
    return ENOENT;
  }

  //find file size
  fseek(file, 0 , SEEK_END);
  int file_size = ftell(file);
  fseek(file, 0 , SEEK_SET);
  int num_of_block = 0;
  int size_count = 0;
  //find block size
  if(file_size % EXT2_BLOCK_SIZE == 0){
    num_of_block = file_size/EXT2_BLOCK_SIZE;
  } else{
    num_of_block = file_size/EXT2_BLOCK_SIZE + 1;
  }
  //check if there's enough free block
  if (sb->s_free_blocks_count < num_of_block) {
    perror(strerror(ENOSPC));
    fclose(file);
    return ENOSPC;
  }

  //allocate inode
  int free_inode_number = get_free_inode_number(disk);

  struct ext2_inode *free_inode = &inode_table[free_inode_number - 1];
  // Allocate the free inode.
  free_inode->i_mode = EXT2_S_IFREG;
  free_inode->i_size = file_size;
  free_inode->i_ctime = time(NULL);
  free_inode->i_blocks = 2*num_of_block;  // Since 1 block = 2 disk sectors
  free_inode->i_links_count = 1;
  free_inode->osd1 = 0;
  free_inode->i_gid = 0;
  free_inode->i_generation = 0;
  free_inode->i_block[0] = 0;

  
  //allocate block and copy
  int indirect = 0;
  for (int i = 0; i<num_of_block; i++){
    if(i<12){
      //direct blocks
      //allocate iblock
      int block_number = get_free_block_number(disk, free_inode, 0);
      free_inode->i_block[i] = block_number;
      //copy to iblock
      //fread (disk + (block_number)*1024, 1, EXT2_BLOCK_SIZE, file);
      memcpy(disk + (block_number) * 1024 , file + size_count, file_size - size_count);
      size_count += EXT2_BLOCK_SIZE;
    } else if(i==12){
      /* TODO: indirect data block */
      //only initialize indirect block for 13 as indicated by instructor 
      if (indirect == 0){
        //initialize indirect block
        int new_block = get_free_block_number(disk, free_inode, i);
        //add block to inode
        free_inode->i_block[i] = new_block;
        indirect = 1;
      } else{

      }
    }
    
  }

  //add directory entry to parent entry
  for (int i = 0; i < 12; i++) {
    if (parent_directory_inode->i_block[i] == 0) {
      int new_block_number = get_free_block_number(disk, parent_directory_inode, i);
      add_entry_in_new_block(disk, parent_directory_inode, EXT2_FT_REG_FILE, free_inode_number, i, new_file, new_block_number);
      break;
    } else {
      if (check_space_available(disk, parent_directory_inode, i, new_file) == 1) {
        add_entry_in_used_block(disk, parent_directory_inode, EXT2_FT_REG_FILE, free_inode_number, i, new_file);
        break;
      }
    }
  }


  // close file when you're done
  fclose(file);

  return 0;
}
