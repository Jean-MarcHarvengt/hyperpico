#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <signal.h>

#define MAX_FILESIZE 0x10000

static unsigned char filebuffer[MAX_FILESIZE];

int main(int argc, char * argv[]) {
    if (argc != 2) return 0;

    FILE *fp_rd;
    if ((fp_rd = fopen (argv[1], "rb")) == NULL)
    {
        printf("can not open tap file\n");
        return 0;      
    }

    fseek(fp_rd, 0L, SEEK_END);
    int size = ftell(fp_rd);
    fseek(fp_rd, 0L, SEEK_SET);

    while (size > 0) {            
	    int byte;
	    int leader_count = 0;

	    // 1. Zoek naar de synchronisatiebytes (0x16) en de sync marker (0x24)
	    while ((byte = fgetc(fp_rd)) != EOF) {
	        if (byte == 0x16) {
	            leader_count++;
	        } else if (byte == 0x24) {
	        	size--;
	            break; // Sync marker gevonden, de header begint hierna
	        } else {
	            leader_count = 0; // Reset als er valse bytes tussen zitten
	        }
	        size--;
	    }

	    if (byte == EOF) {
	        printf("error tap program\n");
	        fclose(fp_rd);
	        return 0;
	    }

        unsigned char header[9];
        if (fread(header, 1, 9, fp_rd) != 9) {
            printf("error tap bad header \n");
            fclose(fp_rd);
            return 0;
        }
        unsigned char  type = header[2];
        unsigned char autorun = header[3];
        unsigned short end_addr = (header[4] << 8) | header[5];
        unsigned short start_addr = (header[6] << 8) | header[7];
        long data_size = end_addr - start_addr + 1;
        size -= 9;

        char name[18];
        int i = 0;
        while (i < 17) {
            byte = fgetc(fp_rd);
            if (byte == EOF) {
                printf("error tap bad name\n");
                fclose(fp_rd);
                return 0;
            }
            name[i] = (char)byte;
            i++;
            if (byte == 0x00) {
                break;
            }            
        }
        size -= i;
        
        printf("--- Oric TAP Header Info ---\n");
        printf("Name    : %s\n", (i == 0) ? "[none]" : name);
        printf("Type    : 0x%02X (%s)\n", type, (type == 0x80) ? "BASIC" : "Machine / Data");
        printf("Autorun : %s\n", (autorun == 0x01) ? "yes" : "no");
        printf("Start   : %04X\n", start_addr);
        printf("End     : %04X\n", end_addr);
        printf("datasize: %ld bytes\n", data_size);
        printf("----------------------------\n");
            

        size -= data_size;

		if (fread(&filebuffer[0], 1, data_size, fp_rd) != data_size) {
                printf("data block error\n");
                fclose(fp_rd);
                return 0;			
		}

        printf("remaining size is %d\n", size);
    }
    fclose(fp_rd);

    printf("exiting vkey...\n");    
    return 1;
}