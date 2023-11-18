#include "userfs.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


enum
{
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
	MAX_NUMBER_OF_BLOCKS = 204800,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block
{
	/** Block memory. */
	char *memory;
	/** How many bytes of memory are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;
	/*position in linked list of file*/
	int id;
};

struct file
{
	/** Double-linked list of file blocks. */
	struct block *block_list;

	/** How many file descriptors are opened on the file. */
	int refs_n;
	/** File name. */
	char *file_name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;
	int number_of_blocks;
	int deleted;
	int size;
	/* PUT HERE OTHER MEMBERS */
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc
{
	struct file *file;
	int fd;
	// For permissions flags
	int permissions;
	int read_write_pointer;
	/**
	 * Last block in the list of blocks for fast access to the end
	 * of file.
	 */
	struct block *last_block;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;
enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

// Search file list to check if a file with this name exists.
struct file *search_files_for(const char *filename)
{
	for (struct file *i = file_list; i != NULL; i = i->next)
	{
		// This loop may find partially deleted files along the way but it will
		// ignore them and return the latest added file.
		// Partially deleted files are only kept until all FDs pointing to them are closed.
		if (!i->deleted && !strcmp(i->file_name, filename))
		{
			return i;
		}
	}
	return NULL;
}

int ufs_open(const char *filename, int flags)
{
	if (file_descriptor_capacity == file_descriptor_count)
	{
		if (file_descriptor_capacity)
		{
			file_descriptor_capacity *= 2;
		}
		else
		{
			file_descriptor_capacity = 1;
		}
		file_descriptors =
			realloc(file_descriptors, file_descriptor_capacity * sizeof(struct filedesc*));
		for(int i = file_descriptor_count; i < file_descriptor_capacity; i++)
		{
			file_descriptors[i] = NULL;
		}
	}

	struct file *my_file = malloc(sizeof(struct file));
	if (file_list == NULL || search_files_for(filename) == NULL)
	{
		if(!(flags & 1))
		{
			ufs_error_code = UFS_ERR_NO_FILE;
			return -1;
		}

		my_file->file_name = strdup(filename);
		my_file->prev = NULL;
		my_file->next = NULL;
		my_file->refs_n = 1;
		my_file->block_list = NULL;
		my_file->number_of_blocks = 0;
		my_file->deleted = 0;
		my_file->size = 0;
		if (file_list == NULL)
		{
			file_list = my_file;
		}
		else
		{
			for (struct file *i = file_list; i != NULL; i = i->next)
			{
				if (i->next == NULL)
				{
					my_file->prev = i;
					i->next = my_file;
					break;
				}
			}
		}
	}
	else
	{
		my_file = search_files_for(filename);
		my_file->refs_n += 1;
	}

	int my_fd = 0;
	while (1)
	{
		if (*(file_descriptors + my_fd) == NULL)
		{
			break;
		}
		my_fd++;
	}

	struct filedesc *new_fd = (struct filedesc*) malloc(sizeof(struct filedesc));
	new_fd->file = malloc(sizeof(struct file));
	new_fd->file = my_file;
	new_fd->fd = my_fd;
	new_fd->permissions = flags;
	new_fd->read_write_pointer = 0;
	new_fd->last_block = my_file->block_list;
	*(file_descriptors + my_fd) = new_fd;
	file_descriptor_count += 1;
	ufs_error_code = UFS_ERR_NO_ERR;
	return my_fd;
}

int writeAtI(char c, struct block* my_block, int fd){
	struct block* current_block = my_block;
	int i = file_descriptors[fd]->read_write_pointer;
	i -= (my_block->id * BLOCK_SIZE);
	if( i < 0)
	{
		printf("indexing error occured while writing, error at line 179\n");
	}
	struct file* my_file = file_descriptors[fd]->file;
	if(i >= BLOCK_SIZE && current_block->next != NULL)
	{
		current_block = current_block->next;
		file_descriptors[fd]->last_block = current_block;
		i -= BLOCK_SIZE;
	}
	else
	if(i >= BLOCK_SIZE && current_block->id < MAX_NUMBER_OF_BLOCKS-1)
	{
		struct block *new_block = (struct block*) malloc(sizeof(struct block));
		new_block->next = NULL;
		new_block->prev = current_block;
		new_block->occupied = 0;
		new_block->id = current_block->id + 1;
		new_block->memory = malloc(BLOCK_SIZE * sizeof(char));
		current_block->next = new_block;
		current_block = current_block->next;
		i -= BLOCK_SIZE;
		file_descriptors[fd]->last_block = current_block;
	}
	
	if(i < BLOCK_SIZE)
	{
		current_block->memory[i] = c;

		file_descriptors[fd]->read_write_pointer++;
		if(my_file->size < file_descriptors[fd]->read_write_pointer)
		{
			my_file->size++;
			current_block->occupied++;
		}
	}
	else
	{
		ufs_error_code = UFS_ERR_NO_MEM;
		return -1;
	}
	
	return 0;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
	if (fd >= file_descriptor_capacity || fd < 0 || file_descriptors[fd] == NULL)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	if(file_descriptors[fd]->permissions&2)
	{
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}	

	// If file has no blocks, create first block.
	struct file *my_file = file_descriptors[fd]->file;
	if (my_file->block_list == NULL)
	{
		struct block *new_block = (struct block*) malloc(sizeof(struct block));
		new_block->next = NULL;
		new_block->prev = NULL;
		new_block->occupied = 0;
		new_block->id = 0;
		new_block->memory = malloc(BLOCK_SIZE * sizeof(char));
		file_descriptors[fd]->last_block = new_block;
		my_file->block_list = new_block;
		my_file->number_of_blocks += 1;
	}

	if(file_descriptors[fd]->last_block == NULL)
	{
		file_descriptors[fd]->last_block = my_file->block_list;
	}

	int cur_size = 0;
	while (cur_size != (int)size)
	{
		if((cur_size) == (int)size){
			break;
		}
		if(writeAtI(buf[cur_size], file_descriptors[fd]->last_block, fd) == -1)
		{
			break;
		}
		cur_size ++;
	}
	// printf("I was supposed to write %s\n", buf);
	// printf("\n write is now %d, and cur_size is %d\n", file_descriptors[fd]->read_write_pointer, cur_size);
	if(!cur_size)
	{
		return -1;
	}
	return cur_size;
}

int readAtI(char* c, struct block* current_block, int fd)
{
	int i = file_descriptors[fd]->read_write_pointer;
	int limit = file_descriptors[fd]->file->size;
	if(i >= limit)
	{
		return -1;
	}
	i -= (current_block->id * BLOCK_SIZE);

	if( i < 0)
	{
		printf("indexing error occured while reading, error at line 278\n");
	}
	
	if(i >= BLOCK_SIZE && current_block->next != NULL){
		current_block = current_block->next;
		file_descriptors[fd]->last_block = current_block;
		i -= BLOCK_SIZE;
	}

	if(i < BLOCK_SIZE)
	{
		*c = current_block->memory[i];
		file_descriptors[fd]->read_write_pointer++;
		return 0;
	}
	else
	{
		ufs_error_code = UFS_ERR_NO_MEM;
		return -1;
	}

	return -1;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
	if (fd > file_descriptor_capacity || fd < 0 || file_descriptors[fd] == NULL)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	if(file_descriptors[fd]->permissions&4)
	{
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}	

	if(file_descriptors[fd]->last_block == NULL)
	{
		file_descriptors[fd]->last_block = file_descriptors[fd]->file->block_list;
	}

	int cur_size = 0;
	while (1)
	{
		if(cur_size == (int)size)
		{
			break;
		}
		if(readAtI((buf + cur_size), file_descriptors[fd]->last_block, fd) == -1)
		{
			break;
		}
		cur_size ++;
	}
	// printf("I think I read %d characters\n", cur_size);
	return cur_size;
}

void fully_delete_file(struct file *deleted_file)
{
	// printf("%s is getting fully deleted\n", deleted_file->file_name);
	struct file *current = file_list;
	while (current != NULL)
	{
		if (current->file_name == deleted_file->file_name && current->deleted && !current->refs_n)
		{
			if (current->next)
			{
				current->next->prev = current->prev;
			}

			if (current->prev)
			{
				current->prev->next = current->next;
				// printf("setting the next of %p with %p\n", current->prev, current->next);

			}
			if(!current->next && !current->prev){
				file_list = NULL;
			}
			break;
		}
		current = current->next;
	}
	struct block* current_block = deleted_file->block_list;
	struct block* tmp;
	while(current_block != NULL) {
		tmp = current_block;
		current_block = current_block->next;
		if(tmp->memory != NULL)
			free(tmp->memory);
		free(tmp);
	}
	free(deleted_file->file_name);
	// todo: free this, why wouldn't you?
	// free(deleted_file);
}

int ufs_close(int fd)
{
	if (fd > file_descriptor_capacity || fd < 0 || file_descriptors[fd] == NULL)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	file_descriptors[fd]->file->refs_n -= 1;
	if (!file_descriptors[fd]->file->refs_n && file_descriptors[fd]->file->deleted)
	{
		fully_delete_file(file_descriptors[fd]->file);
	}

	file_descriptors[fd] = NULL;
	file_descriptor_count--;
	return 0;
}

int ufs_delete(const char *filename)
{
	// printf("%s is getting partially deleted\n", filename);
	struct file *deleted_file = search_files_for(filename);
	if(deleted_file == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	deleted_file->deleted = 1;
	if (deleted_file->refs_n == 0)
	{
		fully_delete_file(deleted_file);
	}
	return 0;
}

int ufs_resize(int fd, size_t new_size)
{
	if (fd >= file_descriptor_capacity 
		|| fd < 0 || file_descriptors[fd] == NULL)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	if(file_descriptors[fd]->permissions&2)
	{
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}	

	if((long long int) new_size > MAX_FILE_SIZE)
	{
		ufs_error_code = UFS_ERR_NO_MEM;
	}

	struct file* my_file = file_descriptors[fd]->file;

	if((int)new_size < my_file->size)
	{
		// Change all fd pointers to new_size -1;
		// This doesn't handle new files that have the same name as partially deleted files.
		my_file->size = (int)new_size;
		int new_last_block_id = (int)new_size / BLOCK_SIZE;
		struct block* new_last_block;
		for(struct block* i = my_file->block_list; i != NULL; i++)
		{
			if(i->id == new_last_block_id)
			{
				new_last_block = i;
				break;
			}
		}
		for(int i = 0; i < file_descriptor_capacity; i++)
		{
			if(file_descriptors[i] != NULL)
			{
				if(file_descriptors[i]->file->file_name == my_file->file_name)
				{
					if((int)new_size < file_descriptors[i]->read_write_pointer)
					{
						file_descriptors[i]->read_write_pointer = (int) new_size;
					}
					file_descriptors[i]->last_block = new_last_block;
				}
			}
		}

	}
	else
	{
		// It's not necessary to add blocks that will be ignored anyway.
		// Blocks will be added when necessary.
	}
	return 0;
}

void ufs_destroy(void)
{
	struct file* current_file = file_list;
	while(current_file != NULL)
	{
		struct block* current_block = current_file->block_list;
		struct block* tmp;
		while(current_block != NULL) {
			free(current_block->memory);
			tmp = current_block->next;
			free(current_block);
			current_block = tmp;
		}
		struct file* tmp_file = current_file->next;
		free(current_file);
		current_file = tmp_file;
	}
	
	for(int i = 0; i < file_descriptor_capacity; i++)
	{
		free(file_descriptors[i]);
	}
}
