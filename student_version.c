//TUAN: I write this helper function. Not part of the skeleton code.
static inline uint32_t
find_free_inode(void)
{
	uint32_t inode_no;
	ospfs_inode_t *new_inode_loc;

	// Determine what inode we can use
	// Start at 2 since the first two inodes are special
	// Inode number 1 is the inode for the root directory of the file system.
	// Inode number 0 is reserved and must never be used. 
	for (inode_no = 2; inode_no < ospfs_super->os_ninodes; inode_no++) {
		new_inode_loc = ospfs_inode(inode_no); //load inode structure corresponding to inode number from disk

		if (new_inode_loc->oi_nlink == 0) //inode is free if link count reaches 0 => no hard links
			return inode_no;
	}

	// If we've gotten this far, there are no free inodes to use
	return 0;
}

// ospfs_read
//	Linux calls this function to read data from a file.
//	It is the file_operations.read callback.
//
//   Inputs:  filp	-- a file pointer
//            buffer    -- a user space ptr where data should be copied
//            count     -- the amount of data requested
//            f_pos     -- points to the file position
//   Returns: Number of chars read on success, -(error code) on error.
//
//   This function copies the corresponding bytes from the file into the user
//   space ptr (buffer).  Use copy_to_user() to accomplish this.
//   The current file position is passed into the function
//   as 'f_pos'; read data starting at that position, and update the position
//   when you're done.
//
//   COMPLETED EXERCISE: Complete this function.

static ssize_t
ospfs_read(struct file *filp, char __user *buffer, size_t count, loff_t *f_pos)
{
	ospfs_inode_t *oi = ospfs_inode(filp->f_dentry->d_inode->i_ino);
	int retval = 0;
	size_t amount = 0;

	if (*f_pos + count < *f_pos) { //overflow detection
		return -EIO;
	}
	
	if (*f_pos >= oi->oi_size) {
		count = 0;
	} else if (*f_pos + count > oi->oi_size) //cannot read pass the file
		count = oi->oi_size - *f_pos;
	}

	// Copy the data to user block by block
	while (amount < count && retval >= 0) {
		uint32_t blockno = ospfs_inode_blockno(oi, *f_pos);
		uint32_t n;
		char *data;

		uint32_t data_offset; // Data offset from the start of the block
		uint32_t bytes_left_to_copy = count - amount;

		// ospfs_inode_blockno returns 0 on error
		if (blockno == 0) {
			retval = -EIO;
			goto done;
		}

		data = ospfs_block(blockno);

		// Figure out how much data is left in this block to read.
		// Copy data into user space. Return -EFAULT if unable to write
		// into user space.
		// Use variable 'n' to track number of bytes moved.
		/* COMPLETED EXERCISE: Your code here */

		data_offset = *f_pos % OSPFS_BLKSIZE; //block size is 1024 bytes

		n = OSPFS_BLKSIZE - data_offset;

		// Copy bytes either until we hit the end
		// of the block or satisfy the user
		if (n > bytes_left_to_copy) {
			n = bytes_left_to_copy;
		}

		// Copy_to_user return the number of bytes that could not be copied. On success, this will be 0
		if (copy_to_user(buffer, data + data_offset, n) > 0) {//copy to buffer
			return -EFAULT;
		}

		buffer += n;
		amount += n;
		*f_pos += n;
	}

    done:
	return (retval >= 0 ? amount : retval);
}




// create_blank_direntry(dir_oi)
//	'dir_oi' is an OSP inode for a directory.
//	Return a blank directory entry in that directory.  This might require
//	adding a new block to the directory.  Returns an error pointer (see
//	below) on failure.
//
// ERROR POINTERS: The Linux kernel uses a special convention for returning
// error values in the form of pointers.  Here's how it works.
//	- ERR_PTR(errno): Creates a pointer value corresponding to an error.
//	- IS_ERR(ptr): Returns true iff 'ptr' is an error value.
//	- PTR_ERR(ptr): Returns the error value for an error pointer.
//	For example:
//
//	static ospfs_direntry_t *create_blank_direntry(...) {
//		return ERR_PTR(-ENOSPC);
//	}
//	static int ospfs_create(...) {
//		...
//		ospfs_direntry_t *od = create_blank_direntry(...);
//		if (IS_ERR(od))
//			return PTR_ERR(od);
//		...
//	}
//
//	The create_blank_direntry function should use this convention.
//
// COMPLETED EXERCISE: Write this function.

static ospfs_direntry_t *
create_blank_direntry(ospfs_inode_t *dir_oi)
{
	// Outline:
	// 1. Check the existing directory data for an empty entry.  Return one
	//    if you find it.
	// 2. If there's no empty entries, add a block to the directory.
	//    Use ERR_PTR if this fails; otherwise, clear out all the directory
	//    entries and return one of them.

	uint32_t new_size;
	ospfs_direntry_t *od;
	int retval = 0, offset;

	if (dir_oi->oi_ftype != OSPFS_FTYPE_DIR) {
		return ERR_PTR(-EIO);
	}

	for (offset = 0; offset < dir_oi->oi_size; offset += OSPFS_DIRENTRY_SIZE) {
		od = ospfs_inode_data(dir_oi, offset);
		//See the header ospfs_direntry. It says that:
		//If the inode number is 0, then the directory entry is EMPTY
		if (od->od_ino == 0) {
			return od;
		}
	}

	// If no free entries were found, add a block
	// ospfs_size2nblocks(size) returns the number of blocks required to hold 'size' bytes of data.
	new_size = (ospfs_size2nblocks(dir_oi->oi_size) + 1) * OSPFS_BLKSIZE;
	retval = change_size(dir_oi, new_size);

	if(retval != 0) { //change_size returns 0 on success
		return ERR_PTR(retval);
	}

	dir_oi->oi_size = new_size;

	//Note that in the above loop, offset stops when it is greater than or equal to dir_oi->oi_size.
	//Now, we change the size, we don't need to add anything to offset.
	return ospfs_inode_data(dir_oi, offset);
}

// ospfs_link(src_dentry, dir, dst_dentry
//   Linux calls this function to create hard links.
//   It is the ospfs_dir_inode_ops.link callback.
//
//   Inputs: src_dentry   -- a pointer to the dentry for the source file.  This
//                           file's inode contains the real data for the hard
//                           linked filae.  The important elements are:
//                             src_dentry->d_name.name
//                             src_dentry->d_name.len
//                             src_dentry->d_inode->i_ino
//           dir          -- a pointer to the containing directory for the new
//                           hard link.
//           dst_dentry   -- a pointer to the dentry for the new hard link file.
//                           The important elements are:
//                             dst_dentry->d_name.name
//                             dst_dentry->d_name.len
//                             dst_dentry->d_inode->i_ino
//                           Two of these values are already set.  One must be
//                           set by you, which one?
//   Returns: 0 on success, -(error code) on error.  In particular:
//               -ENAMETOOLONG if dst_dentry->d_name.len is too large, or
//			       'symname' is too long;
//               -EEXIST       if a file named the same as 'dst_dentry' already
//                             exists in the given 'dir';
//               -ENOSPC       if the disk is full & the file can't be created;
//               -EIO          on I/O error.
//
//   COMPLETED EXERCISE: Complete this function.

static int
ospfs_link(struct dentry *src_dentry, struct inode *dir, struct dentry *dst_dentry) {
	// Note: source entry and destination entry have the same inode number since this is hard link.

	ospfs_inode_t *dir_oi = ospfs_inode(dir->i_ino);
	ospfs_inode_t *src_oi = ospfs_inode(src_dentry->d_inode->i_ino);
	ospfs_direntry_t *new_entry;

	// Some error handling code. See the function header requirement above.

	if (dir_oi == NULL || dir_oi->oi_ftype != OSPFS_FTYPE_DIR || src_oi->oi_nlink + 1 == 0) {
		return -EIO;
	}

	if (dst_dentry->d_name.len > OSPFS_MAXNAMELEN) {
		return -ENAMETOOLONG;
	}

	if (find_direntry(dir_oi, dst_dentry->d_name.name, dst_dentry->d_name.len) != NULL) {
		return -EEXIST;
	}

	// Since this is hard link. inode structure are the same for both original file and the hard link
	// We only need to allocate a new directory entry for the hard link.

	new_entry = create_blank_direntry(dir_oi);

	if (IS_ERR(new_entry)) {
		return PTR_ERR(new_entry);
	}

	if (new_entry == NULL) {
		return -EIO;
	}

	// Initialize the new directory entry
	new_entry->od_ino = src_dentry->d_inode->i_ino;
	memcpy(new_entry->od_name, dst_dentry->d_name.name, dst_dentry->d_name.len);
	new_entry->od_name[dst_dentry->d_name.len] = '\0';

	// Increase the link count on the source file.
	// Note that we can only have hard link on regular file.
	src_oi->oi_nlink++;

	return 0;
}

// ospfs_create
//   Linux calls this function to create a regular file.
//   It is the ospfs_dir_inode_ops.create callback.
//
//   Inputs:  dir	-- a pointer to the containing directory's inode
//            dentry    -- the name of the file that should be created
//                         The only important elements are:
//                         dentry->d_name.name: filename (char array, not null
//                            terminated)
//                         dentry->d_name.len: length of filename
//            mode	-- the permissions mode for the file (set the new
//			   inode's oi_mode field to this value)
//	      nd	-- ignore this
//   Returns: 0 on success, -(error code) on error.  In particular:
//               -ENAMETOOLONG if dentry->d_name.len is too large;
//               -EEXIST       if a file named the same as 'dentry' already
//                             exists in the given 'dir';
//               -ENOSPC       if the disk is full & the file can't be created;
//               -EIO          on I/O error.
//
//   We have provided strictly less skeleton code for this function than for
//   the others.  Here's a brief outline of what you need to do:
//   1. Check for the -EEXIST error and find an empty directory entry using the
//	helper functions above.
//   2. Find an empty inode.  Set the 'entry_ino' variable to its inode number.
//   3. Initialize the directory entry and inode.
//
//   COMPLETED EXERCISE: Complete this function.

static int
ospfs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
{
	ospfs_inode_t *dir_oi = ospfs_inode(dir->i_ino); // load ospfs_inode structure from disk
	uint32_t entry_ino = 0;

	ospfs_inode_t *file_oi = NULL;
	ospfs_direntry_t *new_entry = NULL;
	uint32_t block_no = 0;
	struct inode *i;

	if (dir_oi->oi_ftype != OSPFS_FTYPE_DIR) { //check if file type is directory
		return -EIO;
	}

	if (dentry->d_name.len > OSPFS_MAXNAMELEN) { //check if the name is too large
		return -ENAMETOOLONG;
	}

	if (find_direntry(dir_oi, dentry->d_name.name, dentry->d_name.len) != NULL) { //check if file name already exists
		return -EEXIST;
	}

	// Step 1: Create a new inode for new file

	// Get an inode 
	entry_ino = find_free_inode(); // Tuan: I have this function impelemented above.
		// return inode number if it finds a free inode. return 0 otherwise.	
		// How do we know if an inode is free? when its link count equals 0.	

	if(entry_ino == 0) {
		return -ENOSPC;
	}

	file_oi = ospfs_inode(entry_ino); // load ospfs_inode structure from disk

	if (file_oi == NULL) {
		return -EIO;
	}

	// Initialize the new inode structure with correct values
	file_oi->oi_size = 0; //File size
	file_oi->oi_ftype = OSPFS_FTYPE_REG;
	file_oi->oi_nlink = 1; //Number of hard links
	file_oi->oi_mode = mode; //File permission mode

	// Step 2: Create a new directory entry for new file

	// As part of this lab, we need to implement create_blank_direntry
	new_entry = create_blank_direntry(dir_oi); //create a blank directory entry in that directory
		//This function returns a pointer to a directory entry which we can modify.
	
	if(IS_ERR(new_entry)) {
		return PTR_ERR(new_entry);
	}

	// Initialize the new directory entry with correct values
	new_entry->od_ino = entry_ino;
	memcpy(new_entry->od_name, dentry->d_name.name, dentry->d_name.len);
	new_entry->od_name[dentry->d_name.len] = '\0';

	/* Execute this code after your function has successfully created the
	   file.  Set entry_ino to the created file's inode number before
	   getting here. */
	i = ospfs_mk_linux_inode(dir->i_sb, entry_ino);
	if (!i)
		return -ENOMEM;
	d_instantiate(dentry, i);
	return 0;
}
