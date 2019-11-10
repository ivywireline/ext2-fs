#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include "helper.h"

/* return the directory entry of the last directory/file in the path. */
struct ext2_dir_entry * path_walker(unsigned char *disk, char *path) {
  char *path_token = NULL;
  char *path_holder = strdup(path);
  struct ext2_dir_entry *entry = NULL;
  int found = 0;
  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  // Get the root inode
  struct ext2_inode *root = (struct ext2_inode *)(disk
    + gd->bg_inode_table * EXT2_BLOCK_SIZE + sizeof(struct ext2_inode));
  struct ext2_inode *cur_inode = root;
  struct ext2_dir_entry *result = NULL;

  // Check for special characters

  // If the path_holder is just /, then just return the root inode
  if (strcmp("/", path_holder) == 0) {
    free(path_holder);
    result = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * cur_inode->i_block[0]);
    return result;
  }
  // Remove trailing slash.
  if (path_holder[strlen(path_holder) - 1] == '/') {
    path_holder[strlen(path_holder) - 1] = '\0';
  }

  // Return path_holder error if any special characters are present in path_holder.

  // Tokenize the path_holder
  path_token = strtok(path_holder, "/");
  while (path_token != NULL) {
    found = 0;
    // Loop through the first 12 i_block direct pointers
      // Find the directory entry
    for (int i = 0; i < 12; i++) {
      if (cur_inode->i_block[i] == 0) {
        // block pointer number is 0 which means it has no block pointer.
        break;
      }
      entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * cur_inode->i_block[i]);
      short rec_len_sum = 0;
      // Loop through the directory entries linked list to find the name corresponding to token.
      while (rec_len_sum < 1024 && found == 0) {
        if (strncmp(path_token, entry->name, strlen(path_token)) == 0) {
          result = entry;
          if (entry->file_type == EXT2_FT_DIR) {
            // Update cur_inode as the inode's corresponding directory entry's name
            // matches the path_token name.
            cur_inode = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE
              + (entry->inode - 1) * sizeof(struct ext2_inode) );
            found = 1;
            break;
          } else {
            // We found a file. That means it is at the end of the path_holder, i.e /../../found.txt
            char *token = strtok(NULL, "/");
            if (token != NULL) {
              // Should return at least one directory in your absolute path does not exist, ENONENT
              free(path_holder);
              return NULL;
            }
            free(path_holder);
            return result;
          }
        }
        rec_len_sum = rec_len_sum + entry->rec_len;
        entry = (struct ext2_dir_entry *)(disk + 1024* cur_inode->i_block[i] + rec_len_sum);
      }
      if (found == 1) {
        break;
      }
    }
    // If the directory token is not found in the directory entries in the inode.
    if (found == 0) {
      free(path_holder);
      return NULL;
    }
    path_token = strtok(NULL, "/");
  }
  free(path_holder);
  return result;
}

// Return the inode number of the free inode. inode_table[Inode number - 1] to get
// the inode you want.
int get_free_inode_number(unsigned char *disk) {
  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);

  if (sb->s_free_inodes_count == 0) {
    perror(strerror(ENOSPC));
    return ENOSPC;
  }
  struct ext2_group_desc *group_descriptor = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  unsigned char *inode_bitmap = (unsigned char *)(disk + group_descriptor->bg_inode_bitmap * EXT2_BLOCK_SIZE);
  int inode_number = 1;
  for (int byte = 0; byte < sb->s_inodes_count / 8; byte++) {
    for(int bit = 0; bit < 8; bit++) {
      if (!(inode_bitmap[byte] & (1 << bit))) {
        // Found the free inode. Return the offset and set the free bit to 1 (used).
        inode_bitmap[byte] = inode_bitmap[byte] | (1 << bit);
        sb->s_free_inodes_count--;
        return inode_number;
      }
      inode_number++;
    }
  }
  perror(strerror(ENOSPC));
  return ENOSPC;
}

// 1111 00000 => first 0 is block 5. disk + (block number) * 1024 to get to the corresponding block.
int get_free_block_number(unsigned char *disk, struct ext2_inode *inode, int i_block_idx) {
  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
  if (sb->s_free_blocks_count == 0) {
    perror(strerror(ENOSPC));
    return ENOSPC;
  }
  struct ext2_group_desc *group_descriptor = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  unsigned char *block_bitmap = (unsigned char *)(disk + group_descriptor->bg_block_bitmap * EXT2_BLOCK_SIZE);
  int block_number = 1;
  for (int byte = 0; byte < sb->s_blocks_count / 8; byte++) {
    for(int bit = 0; bit < 8; bit++) {
      if (!(block_bitmap[byte] & (1 << bit))) {
        // Found the free block. Return the offset and the free bit to 1 (used)
        block_bitmap[byte] = block_bitmap[byte] | (1 << bit);
        sb->s_free_blocks_count--;
        group_descriptor->bg_free_blocks_count--;
        inode->i_block[i_block_idx] = block_number;
        return block_number;
      }
      block_number++;
    }
  }
  perror(strerror(ENOSPC));
  return ENOSPC;
}


// 1111 00000 => first 0 is block 5. disk + (block number) * 1024 to get to the corresponding block.
int get_free_block_number_simple(unsigned char *disk) {
  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
  if (sb->s_free_blocks_count == 0) {
    perror(strerror(ENOSPC));
    return ENOSPC;
  }
  struct ext2_group_desc *group_descriptor = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  unsigned char *block_bitmap = (unsigned char *)(disk + group_descriptor->bg_block_bitmap * EXT2_BLOCK_SIZE);
  int block_number = 1;
  for (int byte = 0; byte < sb->s_blocks_count / 8; byte++) {
    for(int bit = 0; bit < 8; bit++) {
      if (!(block_bitmap[byte] & (1 << bit))) {
        // Found the free block. Return the offset and the free bit to 1 (used)
        block_bitmap[byte] = block_bitmap[byte] | (1 << bit);
        sb->s_free_blocks_count--;
        group_descriptor->bg_free_blocks_count--;
        return block_number;
      }
      block_number++;
    }
  }
  perror(strerror(ENOSPC));
  return ENOSPC;
}

// Get the last directory/file in path
char * get_last_item_name(char *path) {
  char *path_holder = strdup(path);
  char *token = strtok(path_holder, "/");
  char *result_temp = NULL;
  if (strcmp(path, "/")==0){
    return path;
  }
  while (token != NULL) {
    result_temp = token;
    token = strtok(NULL, "/");
  }
  char *result = malloc(strlen(result_temp) + 1);
  strncpy(result, result_temp, strlen(result_temp));
  result[strlen(result)] = '\0';
  free(path_holder);
  return result;
}

// Check if the block i_block pointer points to has extra space to add the directory entry. Return 1 if it does -1 if it doesn't.
int check_space_available(unsigned char *disk, struct ext2_inode *parent_inode, int i_block_idx, char *new_entry_name) {
  int new_entry_length = 0;
  int real_entry_length = 0;
  // new_entry_length must be a multiple of 4
  if (strlen(new_entry_name) % 4 == 0) {
    // Since one needs to allocate 8 bytes for other fields before name
    // a multiple of 4 plus 8 is also a multiple of 4. So the new entry length
    // is also 4
    new_entry_length = 8 + strlen(new_entry_name);
  } else {
    new_entry_length = 8 + strlen(new_entry_name) + (4 - (strlen(new_entry_name) % 4));
  }

  struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *)(disk + 1024 * parent_inode->i_block[i_block_idx]);

  int rec_len_sum = 0;
  // Iterate over the current block.
  while (rec_len_sum < 1024) {
    // Find the real length of the current entry to see if there is space available
    // after this entry
    if (cur_entry->name_len % 4 == 0){
      real_entry_length = 8 + cur_entry->name_len;
    } else {
      real_entry_length = 8 + cur_entry->name_len + (4 - (cur_entry->name_len % 4));
    }

    int extra_space = cur_entry->rec_len - real_entry_length;
    // If the current entry has enough extra space to fit the new entry, return 1.
    if (extra_space >= new_entry_length) {
      return 1;
    }
    rec_len_sum += cur_entry->rec_len;
    cur_entry = (struct ext2_dir_entry *)(disk + 1024 * parent_inode->i_block[i_block_idx] + rec_len_sum);
  }
  return -1;
}

/* This function adds the new directory entry to the non-empty block in parent_inode located using i_block_idx.
   Should use check_space_available function to check if there is space available
   in that block first.
*/
int add_entry_in_used_block(unsigned char *disk, struct ext2_inode *parent_inode, unsigned char new_entry_file_type, int new_inode_number, int i_block_idx, char *new_entry_name) {
  int new_entry_length = 0;
  int real_entry_length = 0;

  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);

  if (new_entry_file_type == EXT2_FT_DIR) {
    gd->bg_used_dirs_count = gd->bg_used_dirs_count + 1;
    //only increment if new_entry_file_type is a directory
    parent_inode->i_links_count = parent_inode->i_links_count + 1;
  }
  // new_entry_length must be a multiple of 4
  if (strlen(new_entry_name) % 4 == 0) {
    // Since one needs to allocate 8 bytes for other fields before name
    // a multiple of 4 plus 8 is also a multiple of 4. So the new entry length
    // is also 4
    new_entry_length = 8 + strlen(new_entry_name);
  } else {
    new_entry_length = 8 + strlen(new_entry_name) + (4 - (strlen(new_entry_name) % 4));
  }


  struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *)(disk + 1024 * parent_inode->i_block[i_block_idx]);

  int rec_len_sum = 0;
  int next_rec_len_sum = 0;
  // Iterate over the current block.
  while (rec_len_sum < 1024) {
    next_rec_len_sum = rec_len_sum + cur_entry->rec_len;
    // Check if cur_entry is the last directory entry in the directory block.
    if (next_rec_len_sum >= 1024) {
      // Find the real length of the current entry to see if there is space available
      // after this entry
      if (cur_entry->name_len % 4 == 0){
        real_entry_length = 8 + cur_entry->name_len;
      } else {
        real_entry_length = 8 + cur_entry->name_len + (4 - (cur_entry->name_len % 4));
      }

      int extra_space = cur_entry->rec_len - real_entry_length;
      // If the current entry has enough extra space to fit the new entry.
      if (extra_space >= new_entry_length) {
        // update the current entry's rec_len
        cur_entry->rec_len = real_entry_length;
        // char is one byte; We are moving the pointer to the beginning of extra space.
        struct ext2_dir_entry *new_entry = (struct ext2_dir_entry *)((char *)(cur_entry) + real_entry_length);
        new_entry->inode = new_inode_number;
        new_entry->rec_len = extra_space;
        new_entry->name_len = strlen(new_entry_name);
        new_entry->file_type = new_entry_file_type;
        strncpy(new_entry->name, new_entry_name, strlen(new_entry_name) + 1);
        // On Success adding the entry to the extra space, return 1
        return 1;
      }
    }
    rec_len_sum += cur_entry->rec_len;
    cur_entry = (struct ext2_dir_entry *)(disk + 1024 * parent_inode->i_block[i_block_idx] + rec_len_sum);
  }
  return 0;
}

// Add the entry in an empty block, i.e inode->i_block[i_block_idx] == 0
int add_entry_in_new_block(unsigned char *disk, struct ext2_inode *parent_inode, unsigned char new_entry_file_type, int new_inode_number, int i_block_idx, char * new_entry_name, int free_block_number) {
  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  parent_inode->i_block[i_block_idx] = free_block_number;
  struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * parent_inode->i_block[i_block_idx]);
  entry->inode = new_inode_number;
  // whole block
  entry->rec_len = 1024;
  entry->name_len = strlen(new_entry_name);
  entry->file_type = new_entry_file_type;
  strncpy(entry->name, new_entry_name, strlen(new_entry_name) + 1);
  if (new_entry_file_type == EXT2_FT_DIR) {
    gd->bg_used_dirs_count = gd->bg_used_dirs_count + 1;
    //only increment if new_entry_file_type is a directory
    parent_inode->i_links_count = parent_inode->i_links_count + 1;
  }
  parent_inode->i_blocks += 2;
  parent_inode->i_size = parent_inode->i_size + EXT2_BLOCK_SIZE;
  return 1;
}

// Get the absolute path to the parent directory
char * get_parent_directory_path(char *path) {
  char *path_holder = strdup(path);
  //base case
  if (strcmp(path, "/")==0){
    return path;
  }
  // Remove trailing slash.
  if (path_holder[strlen(path_holder) - 1] == '/') {
    path_holder[strlen(path_holder) - 1] = '\0';
  }
  char *last_item_name = get_last_item_name(path_holder);
  int result_len = strlen(path_holder) - strlen(last_item_name);
  char *parent_directory_path = malloc(result_len);
  strncpy(parent_directory_path, path_holder, result_len);
  parent_directory_path[result_len] = '\0';
  free(path_holder);
  return parent_directory_path;
}

//check if the given string is a file name or a directory name
int check_is_file(char *file){
  if(strstr(file, ".") == NULL){
    return 0;
  }
  return 1;
}

int find_type(int type_mode, int struct_mode) {
  int type;
  if (struct_mode == INODE_STRUCT){ // if it's inode struct
    if (type_mode & EXT2_S_IFDIR){
      type = 2;
    } else if(type_mode & EXT2_S_IFREG) {
      type = 1;
    } else if (type_mode & EXT2_S_IFLNK){
      type = 7;
    } else {
      type = 0;
    }
  } else if (struct_mode == DIR_ENTRY_STRUCT){ // if it's dir_entry struct
    if (type_mode == EXT2_FT_DIR){
      type = 2;
    } else if(type_mode == EXT2_FT_REG_FILE) {
      type = 1;
    } else if (type_mode == EXT2_FT_SYMLINK){
      type = 7;
    } else {
      type = 0;
    }
    //type = (char) type_mode;
  }
  return type;
}

int update_inode_bitmap(unsigned char *disk, int inode_number){
  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  unsigned char *inode_bits = (unsigned char *)(disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);
  unsigned short idx = 1;

  if(*inode_bits & (idx << (sb->s_inodes_count - inode_number - 1))){ // is already 1
    return 0;
  }
  //*inode_bits |= idx << (sb->s_inodes_count-inode_number - 1); //update 0 to 1
  if (flip_inode_bit(disk, inode_number, 1) == -1) { //update to 1
    fprintf(stderr, "Flipping inode bitmap errored out\n");
    return ENOENT;
  }
  return 1;
}

int update_block_bitmap(unsigned char *disk, int block_number){
  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  unsigned char *block_bits = (unsigned char *)(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
  unsigned short idx = 1;

  if(*block_bits & (idx << (sb->s_blocks_count - block_number - 1))){ // is already 1
    return 0;
  }
  if (flip_block_bit(disk, block_number, 1) == -1) {
    fprintf(stderr, "Flipping inode bitmap errored out b\n");
    return ENOENT;
  }
  return 1;
}

// Given the inode number, return 1 if the inode is in use or 0 if it's not.
int get_inode_status(unsigned char *disk, int inode_number) {
  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);

  struct ext2_group_desc *group_descriptor = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  unsigned char *inode_bitmap = (unsigned char *)(disk + group_descriptor->bg_inode_bitmap * EXT2_BLOCK_SIZE);
  int inode_number_count = 1;
  for (int byte = 0; byte < sb->s_inodes_count / 8; byte++) {
    for(int bit = 0; bit < 8; bit++) {
      if (inode_number_count == inode_number){
        if (!(inode_bitmap[byte] & (1 << bit))) {
          // Found the free inode. Can be restored, not overwritten yet. Return 0;
          return 0;
        } else {
          return 1;
        }
      }
      inode_number_count++;
    }
  }
  return -1;
}

// Given the block number, return 1 if the block is in use otherwise 0 if it's not.
int get_block_status(unsigned char *disk, int block_number) {
  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);

  struct ext2_group_desc *group_descriptor = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  unsigned char *block_bitmap = (unsigned char *)(disk + group_descriptor->bg_block_bitmap * EXT2_BLOCK_SIZE);
  int block_number_count = 1;
  for (int byte = 0; byte < sb->s_blocks_count / 8; byte++) {
    for(int bit = 0; bit < 8; bit++) {
      if (block_number_count == block_number){
        if (!(block_bitmap[byte] & (1 << bit))) {
          // Found the free block. Can be restored, not overwritten yet. Return 0;
          return 0;
        } else {
          return 1;
        }
      }
      block_number_count++;
    }
  }
  return -1;
}

// Flip inode bit and update variables. Return 0 for success and -1 if error.
int flip_inode_bit(unsigned char *disk, int inode_number, int bit_status) {
  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);

  struct ext2_group_desc *group_descriptor = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  unsigned char *inode_bitmap = (unsigned char *)(disk + group_descriptor->bg_inode_bitmap * EXT2_BLOCK_SIZE);
  int inode_number_count = 1;
  for (int byte = 0; byte < sb->s_inodes_count / 8; byte++) {
    for(int bit = 0; bit < 8; bit++) {
      if (inode_number_count == inode_number){
        if (bit_status == 1) {
          // Found the free inode. flip the bit to 1.
          inode_bitmap[byte] = inode_bitmap[byte] | (1 << bit);
          sb->s_free_inodes_count--;
          return 0;
        } else {
          // flip the bit to 0
          inode_bitmap[byte] = inode_bitmap[byte] & ~(1 << bit);
          sb->s_free_inodes_count++;
          return 0;
        }
      }
      inode_number_count++;
    }
  }
  return -1;
}

// Given the block number, flip the block bit according to bit_status.
int flip_block_bit(unsigned char *disk, int block_number, int bit_status) {
  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);

  struct ext2_group_desc *group_descriptor = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  unsigned char *block_bitmap = (unsigned char *)(disk + group_descriptor->bg_block_bitmap * EXT2_BLOCK_SIZE);
  int block_number_count = 1;
  for (int byte = 0; byte < sb->s_blocks_count / 8; byte++) {
    for(int bit = 0; bit < 8; bit++) {
      if (block_number_count == block_number){
        if (bit_status == 1) {
          // Found the free block. flip the bit to 1.
          block_bitmap[byte] = block_bitmap[byte] | (1 << bit);
          sb->s_free_blocks_count--;
          group_descriptor->bg_free_blocks_count--;
          return 0;
        } else {
          // flip the bit to 0
          block_bitmap[byte] = block_bitmap[byte] & ~(1 << bit);
          sb->s_free_blocks_count++;
          group_descriptor->bg_free_blocks_count++;
          return 0;
        }
      }
      block_number_count++;
    }
  }
  return -1;
}
