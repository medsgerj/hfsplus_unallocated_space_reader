#include <stdio.h>
#include <hfs/hfs_format.h>
#include <libkern/OSByteOrder.h>
#include <libc.h>
 
#define SECTOR_SIZE 512
 
/* DETERMINE IF THAT ALLOCATION BLOCK IS ALLOCATED */
int is_block_allocated(uint64_t block, unsigned char *alloc_file_buf)
{
  uint8_t thisByte;
 
  thisByte = alloc_file_buf[block / 8];
 
  return ( thisByte & (128 >> ((block % 8))) ) != 0;
}
 
int allocation_file_bmp_init(HFSPlusVolumeHeader *vh)
{
  /* RESET ALL THE VOLUME HEADER VARIABLES (CORRECT BYTE ORDER) */    
  vh->blockSize = OSSwapBigToHostInt32(vh->blockSize);
  vh->freeBlocks = OSSwapBigToHostInt32(vh->freeBlocks);
  vh->totalBlocks = OSSwapBigToHostInt32(vh->totalBlocks);                                
 
  /* CONVERT THE BYTE ORDER OF NECESSARY ALLOCATION FILE STRUCT VARIABLES */
  (&(vh->allocationFile))->logicalSize = 
  OSSwapBigToHostInt64( (&(vh->allocationFile))->logicalSize );
  (&(vh->allocationFile))->clumpSize = 
  OSSwapBigToHostInt32( (&(vh->allocationFile))->clumpSize );
  (&(vh->allocationFile))->totalBlocks = 
  OSSwapBigToHostInt32( (&(vh->allocationFile))->totalBlocks );
 
  /* CONVERT THE BYTE ORDER OF NECESSARY ALLOCATION FILE STRUCT VARIABLES */
  uint64_t i;
  for(i = 0; i < kHFSPlusExtentDensity; i++) 
  {
    (&(vh->allocationFile))->extents[i].startBlock = 
    OSSwapBigToHostInt32( (&(vh->allocationFile))->extents[i].startBlock);
    (&(vh->allocationFile))->extents[i].blockCount = 
    OSSwapBigToHostInt32( (&(vh->allocationFile))->extents[i].blockCount);
  }
 
  return 0;
}
 
int read_alloc_bitmap_into_mem(int fd,
                               HFSPlusVolumeHeader *vh, 
                               unsigned char *alloc_file_buf)
{
  uint64_t byte_count;
  uint64_t offset;
  uint64_t extent_length;
  unsigned char *buf;
 
  byte_count = 0;
 
  /* FOR EACH EXTENT OF THE ALLOCATION BITMAP FILE */
  uint64_t i;
  for(i = 0; i < kHFSPlusExtentDensity; i++) 
  {    
    /* CALCULATE THE BYTE OFFSET AND LENGTH OF THAT EXTENT OF THE 
     * ALLOCATION FILE */
    offset = (&(vh->allocationFile))->extents[i].startBlock * vh->blockSize;
    extent_length = (&(vh->allocationFile))->extents[i].blockCount * vh->blockSize;
 
    /* ALLOCATE MEMORY FOR THAT ALLOCATION FILE EXTENT */
    buf = (unsigned char *)malloc( extent_length * sizeof(unsigned char) );
    if (buf == NULL)
    {
      printf("Error allocating memory for allocation file extent.\n");
      return -1;
    }    
 
    /* READ THAT ALLOCATION FILE EXTENT INTO A BUFFER */
    if (pread(fd, buf, extent_length * sizeof(unsigned char), offset) != 
        extent_length * sizeof(unsigned char) )
    {
      printf("Error reading allocation file extent.\n");
      return -1;
    }
 
    /* SEQUENTIALLY COPY ALLOCATION FILE EXTENT INTO THE ALLOCATION FILE BUFFER */
    uint64_t x;
    for (x = 0; x < extent_length * sizeof(unsigned char); x++)
    {
      alloc_file_buf[byte_count] = buf[x];
      byte_count++;  
    }
 
    free(buf);
 
  } 
 
  return 0;
}
 
int main(int argc, char **argv)
{
  int                 fd;
  HFSPlusVolumeHeader *vh;
  unsigned char       buffer[SECTOR_SIZE];
  unsigned char       *alloc_file_buf;
  uint64_t            num_unallocated_blocks;
  uint64_t            num_allocated_blocks;
  unsigned char       *alloc_block_buf;
 
  num_unallocated_blocks = 0;
  num_allocated_blocks = 0;
 
  /* OPEN THE DRIVE */
  fd = open(argv[1], O_RDONLY);
  if (fd == -1)
  {
    printf("Drive could not be opened.\n");
    exit(-1);
  }
 
  /* READ THE SECTOR THAT CONTAINS THE VOLUME HEADER */
  if (pread(fd, buffer, sizeof(buffer), 2 * SECTOR_SIZE) != sizeof(buffer))
  {
    printf("Error reading the Volume Header");
    return -1;
  }
 
  /* GET POINTER TO VOLUME HEADER */
  vh = (HFSPlusVolumeHeader *)buffer;
 
  /* INITIALIZE THE ALLOCATION FILE STRUCTS */
  if (allocation_file_bmp_init(vh) == -1)
  {
    printf("Error initializing the allocation file structures.\n");
    exit(-1);
  }
 
  printf("LOGICAL SIZE %llu\n", (unsigned long long) 
         (&(vh->allocationFile))->logicalSize );
  printf("CLUMP SIZE %lu\n",    (unsigned long) 
         (&(vh->allocationFile))->clumpSize );
  printf("TOTAL BLOCKS %lu\n",  (unsigned long) 
         (&(vh->allocationFile))->totalBlocks );
  printf("ALLOCATION BLOCK SIZE \t%lu\n", (unsigned long) vh->blockSize );
  printf("FREE BLOCKS \t\t%lu\n", (unsigned long) vh->freeBlocks );
  printf("TOTAL BLOCKS \t\t%lu\n", (unsigned long)vh->totalBlocks );
 
  /* ALLOCATE MEMORY FOR ALLOCATION BITMAP FILE */
  alloc_file_buf = (unsigned char *)malloc( (&(vh->allocationFile))->logicalSize * 
                                           sizeof(unsigned char) );
  if (alloc_file_buf == NULL)
  {
    printf("Error allocating array for allocation bitmap file.\n");
    exit(-1);
  }
 
  /* READ WHOLE ALLOCATION FILE INTO MEMORY AS ONE LARGE BUFFER */
  if (read_alloc_bitmap_into_mem(fd, vh, alloc_file_buf) == -1)
  {
    printf("Error reading extents into memory.\n");
    exit(-1);
  }
  
  /* ALLOCATE MEMORY FOR THE ALLOCATION BLOCK BUFFER */
  alloc_block_buf = (unsigned char *)malloc(vh->blockSize * sizeof(unsigned char));
  if (alloc_block_buf == NULL)
  {
    printf("Error allocating memory for the allocation block buf.\n");
    exit(-1);
  }
 
  /* NOW, LET'S LOOP THROUGH THE WHOLE ALLOCATION BITMAP FILE BUFFER, 
   * DETERMINE THE ALLOCATION STATUS OF EACH BLOCK, AND READ
   * ALL THE UNALLOCATED BLOCKS */ 
  uint64_t j;
  for (j = 0; j < vh->totalBlocks; j++)
  {
    /* SEE IF THIS BLOCK IS UNALLOCATED */
    int alloc_status = is_block_allocated(j, alloc_file_buf);
    if (alloc_status == 0)
    {
      /* READ THAT UNALLOCATED BLOCK */
      if (pread(fd, alloc_block_buf, vh->blockSize, j * vh->blockSize) != vh->blockSize)
      {
        printf("Error reading allocation block %llu.\n", j);
        exit(-1);
      }
    
      num_unallocated_blocks++;
    }

  }
 
  printf("CALCULATED NUM FREE BLOCKS: %llu\n", num_unallocated_blocks);
  printf("CALCULATED NUM ALLOCATED BLOCKS: %llu\n", num_allocated_blocks);
  printf("CALCULATED TOTAL NUMBER OF BLOCKS %llu\n", num_unallocated_blocks +
         num_allocated_blocks);  
 
  close(fd);
  free(alloc_block_buf);
  free(alloc_file_buf);
 
  return 0;
}
