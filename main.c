/*
   Main program for the virtual memory project.
   Make all of your modifications to this file.
   You may add or rearrange any code or data as you need.
   The header files page_table.h and disk.h explain
   how to use the page table and disk interfaces.
   */

#include "page_table.h"
//#include "page_table.c"
#include "disk.h"
#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct table {
    char method[25];
    int size;
    int currFree;
    int elements[];
};

struct table *frameTable;
struct disk *gDisk;

void page_fault_handler( struct page_table *pt, int page )
{
    printf("curr %d size %d", frameTable->currFree, frameTable->size);
    int frame;
    int bits;
    page_table_get_entry(pt, page, &frame, &bits);    

    if (bits == 0)
    {
        if (frameTable->currFree <= frameTable->size - 1)
        {
            printf("in if\n");
            printf("page %d, currFree %d\n", page, frameTable->currFree);
            //load in
            page_table_set_entry(pt, page, frameTable->currFree, PROT_READ);

            char *phys = page_table_get_physmem(pt);
            int nframes = page_table_get_nframes(pt);
            //        disk_read(gDisk, page, &phys[frameTable->currFree * nframes]);
            //disk_read(pt->fd, page, pt->physmem[frameTable->currFree * (pt->nframes)]);
            frameTable->currFree++;
        }
    }
    else
    {
        //free then load in

        //writing
        page_table_set_entry(pt, page, frame, PROT_READ|PROT_WRITE);

        char *phys = page_table_get_physmem(pt);
        int nframes = page_table_get_nframes(pt);
        disk_read(gDisk, page, &phys[frame * nframes]);

        //freeing
        //x = randNum to free
        //   disk_write(gDisk, page, &phys[0 * PAGE_SIZE]);
        //disk_write(pt->fd, page, pt->physmem[0 * PAGE_SIZE]);

    }

    //page_table_set_entry(pt,page,page,PROT_READ|PROT_WRITE);
    printf("page fault on page #%d\n",page);
    page_table_print(pt);
    //exit(1);
}

int main( int argc, char *argv[] )
{
    if(argc!=5) {
        printf("use: virtmem <npages> <nframes> <rand|fifo|lru|custom> <sort|scan|focus>\n");
        return 1;
    }

    int npages = atoi(argv[1]);
    int nframes = atoi(argv[2]);
    const char *program = argv[4];

    frameTable = malloc(sizeof(struct table) + nframes * sizeof(int));
    frameTable->currFree = 0;
    frameTable->size = nframes;

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

    char *physmem = page_table_get_physmem(pt);

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

    return 0;


}
