/*
   Main program for the virtual memory project.
   Make all of your modifications to this file.
   You may add or rearrange any code or data as you need.
   The header files page_table.h and disk.h explain
   how to use the page table and disk interfaces.
   */
#include <time.h>
#include "page_table.h"
#include "disk.h"
#include "program.h"
#include <sys/time.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct table {
    char *method;
    int size;
    int currFree;
    int oldest;
    int lastUsed;
    int elements[];
};

struct timeTable {
    int dummy;
    struct timeval elements[];
};

int tenMult = 10 * 10 * 10 * 10 * 10 * 10;
int faults = 0;
int writes = 0;
int reads = 0;
struct table *frameTable;
struct timeTable *accessTable;
struct disk *gDisk;

void page_fault_handler( struct page_table *pt, int page )
{
    faults++;
    //printf("curr %d size %d", frameTable->currFree, frameTable->size);
    /* printf("-------\n"); */
    int frame;
    int bits;
    page_table_get_entry(pt, page, &frame, &bits);

    if (bits == 0) //we need to set PROT_READ -> the page is not in the frame table
    {
        char *phys = page_table_get_physmem(pt);
        int nframes = page_table_get_nframes(pt);

        if (frameTable->currFree <= frameTable->size - 1) //this runs as long as we have empty frames we can fill
        {                                                 //it's linear which makes the beginning slightly more efficient
                                                          //e.g. we don't have to search for free frames, we know exactly where it is
            //load in
            page_table_set_entry(pt, page, frameTable->currFree, PROT_READ);
            struct timeval tv;
            gettimeofday(&tv,NULL);
            accessTable->elements[page] = tv;
            frameTable->lastUsed = page;

            disk_read(gDisk, page, &phys[frameTable->currFree * PAGE_SIZE]);
            reads++;

            frameTable->elements[frameTable->currFree] = page;

            frameTable->currFree++; //this keeps track of where our first available empty frame is
        }
        else //physical memory is full and we need to free something
        {
            int randFrame, removePage, removeFrame, removeBits;
            if (!strcmp(frameTable->method, "rand"))
            {
                /* printf("rand\n"); */
                randFrame = rand() % nframes; //randomly get and index of the frames
                removePage = frameTable->elements[randFrame]; //get the page at that index for removal
            }
            else if (!strcmp(frameTable->method, "fifo"))
            {
                /* printf("fifo!\n"); */
                randFrame = frameTable->oldest;
                frameTable->oldest = (frameTable->oldest + 1) % nframes; //because we insert and replace pages linearly this works for fifo
                removePage = frameTable->elements[randFrame];
            }
            else if (!strcmp(frameTable->method, "custom"))
            {
                struct timeval tv;
                gettimeofday(&tv,NULL);

                int i = 0;
                struct timeval min = tv;
                min.tv_sec = INT_MAX;
                for (; i < page_table_get_npages(pt); i++) //this for loop finds the timestamp that was longest ago
                {
                    if (accessTable->elements[i].tv_sec < min.tv_sec && accessTable->elements[i].tv_sec != 0)
                    {
                        min = accessTable->elements[i];
                        removePage = i;
                    }
                    else if (accessTable->elements[i].tv_sec == min.tv_sec) //if seconds are equal compare microseconds
                    {
                        if (accessTable->elements[i].tv_usec < min.tv_usec && accessTable->elements[i].tv_sec > 0)
                        {
                            min = accessTable->elements[i];
                            removePage = i;
                        }
                    }
                }
            }
            else
            {
                printf("Method not recognized, please choose rand|fifo|custom. Exiting...\n");
                exit(1);
            }

            page_table_get_entry(pt, removePage, &removeFrame, &removeBits);

            int i = 0; //this for loop updates the page/frame tracker with the new page
            for (; i < nframes; i++)
                if (frameTable->elements[i] == removePage)
                    frameTable->elements[i] = page;

            disk_write(gDisk, removePage, &phys[removeFrame * PAGE_SIZE]);
            writes++;
            disk_read(gDisk, page, &phys[removeFrame * PAGE_SIZE]);
            reads++;
            page_table_set_entry(pt, page, removeFrame, PROT_READ);

            struct timeval tv;
            gettimeofday(&tv,NULL);
            accessTable->elements[page] = tv; //set the new pages timestamp
            page_table_set_entry(pt, removePage, 0, 0);
            accessTable->elements[removePage].tv_sec = 0; //reset this pages timestamp to a value so we can ignore it
        }
    }
    else //PROT_READ is set now set PROT_WRITE
    {
        page_table_set_entry(pt, page, frame, PROT_READ|PROT_WRITE); //set the new perms
        struct timeval tv;
        gettimeofday(&tv,NULL);
        accessTable->elements[page] = tv; //get the time for our custom alg
        if (accessTable->elements[page].tv_usec + 10000 > tenMult) //this if handles the wacky stuff we had to do for the timestamps
        {
            accessTable->elements[page].tv_sec += 1; //give a priority boost to files that have been recently written to
            accessTable->elements[page].tv_usec = (accessTable->elements[page].tv_usec + 10000) - tenMult;
        }
        else
            accessTable->elements[page].tv_usec += 10000;
    }
}

int main( int argc, char *argv[] )
{
    if(argc!=5) {
        printf("use: virtmem <npages> <nframes> <rand|fifo|custom> <sort|scan|focus>\n");
        return 1;
    }

    int npages = atoi(argv[1]);
    if (npages <= 0 || npages > 10000) 
    {
        printf("Bad npages input\n");
        exit(1);
    }
    int nframes = atoi(argv[2]);
    
    if (nframes <= 0 || nframes > 10000) 
    {
        printf("Bad nframes input\n");
        exit(1);
    }

    if (nframes > npages) 
    {
        printf("nframes cannot be greater than npages\n");
        exit(1);
    }
    const char *program = argv[4];

    frameTable = malloc(sizeof(struct table) + nframes * sizeof(int));
    accessTable = malloc(sizeof(struct table) + npages * sizeof(struct timeval));

    frameTable->currFree = 0;
    frameTable->oldest = 0;
    frameTable->size = nframes;
    frameTable->method = argv[3];

    printf("%s\n", frameTable->method);

    struct disk *disk = disk_open("myvirtualdisk",npages);
    gDisk = disk;
    if(!disk) {
        fprintf(stderr,"couldn't create virtual disk: %s\n",strerror(errno));
        return 1;
    }


    struct page_table *pt = page_table_create( npages, nframes, page_fault_handler );
    if(!pt) {
        fprintf(stderr,"couldn't create page table: %s\n",strerror(errno));
        return 1;
    }

    char *virtmem = page_table_get_virtmem(pt);

    //char *physmem = page_table_get_physmem(pt);

    if(!strcmp(program,"sort")) {
        sort_program(virtmem,npages*PAGE_SIZE);

    } else if(!strcmp(program,"scan")) {
        scan_program(virtmem,npages*PAGE_SIZE);

    } else if(!strcmp(program,"focus")) {
        focus_program(virtmem,npages*PAGE_SIZE);

    } else {
        fprintf(stderr,"unknown program: %s\n",argv[3]);
        return 1;
    }

    page_table_delete(pt);
    disk_close(disk);
    free(frameTable);
    printf("faults: %d, reads: %d, writes: %d\n", faults, reads, writes);
  
    return 0;
}













