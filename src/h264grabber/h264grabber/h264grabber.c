/*
 * Copyright (c) 2020 roleo.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Scans the buffer and sends h264 frames to stdout.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <getopt.h>
#include <sys/time.h>

#define RESOLUTION_LOW 360
#define RESOLUTION_HIGH 1080

//#define REPEAT 1

unsigned char SPS[] = { 0x00, 0x00, 0x00, 0x01, 0x67 };

unsigned char SEI_F0[] = { 0x00, 0x00, 0x00, 0x01, 0x06, 0xF0 };

// Returns the 1st process id corresponding to pname
int pidof(const char *pname)
{
  DIR *dirp;
  FILE *fp;
  struct dirent *entry;
  char path[1024], read_buf[1024];
  int ret = 0;

  dirp = opendir ("/proc/");
  if (dirp == NULL) {
    fprintf(stderr, "error opening /proc");
    return 0;
  }

  while ((entry = readdir (dirp)) != NULL) {
    if (atoi(entry->d_name) > 0) {
      sprintf(path, "/proc/%s/comm", entry->d_name);

      /* A file may not exist, Ait may have been removed.
       * dut to termination of the process. Actually we need to
       * make sure the error is actually file does not exist to
       * be accurate.
       */
      fp = fopen (path, "r");
      if (fp != NULL) {
        fscanf (fp, "%s", read_buf);
        if (strcmp (read_buf, pname) == 0) {
            ret = atoi(entry->d_name);
            fclose (fp);
            break;
        }
        fclose (fp);
      }
    }
  }

  closedir (dirp);
  return ret;
}

// Converts virtual address to physical address
unsigned int rmm_virt2phys(unsigned int inAddr) {
    int pid;
    unsigned int outAddr;
    char sInAddr[16];
    char sMaps[1024];
    FILE *fMaps;
    char *p;
    char *line;
    size_t lineSize;

    line = (char  *) malloc(1024);

    pid = pidof("rmm");
    sprintf(sMaps, "/proc/%d/maps", pid);
    fMaps = fopen(sMaps, "r");
    sprintf(sInAddr, "%08x", inAddr);
    while (getline(&line, &lineSize, fMaps) != -1) {
        if (strncmp(line, sInAddr, 8) == 0)
            break;
    }

    p = line;
    p = strchr(p, ' ');
    p++;
    p = strchr(p, ' ');
    p++;
    p[8] = '\0';
    sscanf(p, "%x", &outAddr);
    free(line);
    fclose(fMaps);

    return outAddr;
}

long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds

    return milliseconds;
}

void fillCheck(unsigned char *check, int checkSize, unsigned char *buf, int bufSize)
{
    int i;

    for (i=0; i<checkSize; i++) {
        check[i] = buf[bufSize/checkSize*i];
    }
}

void print_usage(char *progname)
{
    fprintf(stderr, "\nUsage: %s [-r RES] [-d]\n\n", progname);
    fprintf(stderr, "\t-r RES, --resolution RES\n");
    fprintf(stderr, "\t\tset resolution: LOW or HIGH (default HIGH)\n");
    fprintf(stderr, "\t-s, --ssf0\n");
    fprintf(stderr, "\t\tskip SEI F0\n");
    fprintf(stderr, "\t-d, --debug\n");
    fprintf(stderr, "\t\tenable debug\n");
    fprintf(stderr, "\t-h, --help\n");
    fprintf(stderr, "\t\tprint this help\n");
}

int main(int argc, char **argv)
{
    int res = RESOLUTION_HIGH;
    int ssf0 = 0;
    int debug = 0;

    int c;
    int repeat;
    const char memDevice[] = "/dev/mem";
    FILE *fPtr, *fLen, *fTime;
    int fMem;
    unsigned int ivAddr, ipAddr;
    unsigned int size;
    unsigned char *addr;
    unsigned char check1[64], check2[64];
    char filLenFile[1024];
    char timeStampFile[1024];
    unsigned char buffer[262144];
    int len;
    unsigned int time, oldTime = 0;
    int stream_started = 0;

    while (1) {
        static struct option long_options[] =
        {
            {"resolution",  required_argument, 0, 'r'},
            {"ssf0",  required_argument, 0, 's'},
            {"debug",  no_argument, 0, 'd'},
            {"help",  no_argument, 0, 'h'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "r:sdh",
                         long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
        case 'r':
            if (strcasecmp("low", optarg) == 0) {
                res = RESOLUTION_LOW;
            } else if (strcasecmp("high", optarg) == 0) {
                res = RESOLUTION_HIGH;
            }
            break;

        case 's':
            fprintf (stderr, "skip SEI F0\n");
            ssf0 = 1;
            break;

        case 'd':
            fprintf (stderr, "debug on\n");
            debug = 1;
            break;

        case 'h':
            print_usage(argv[0]);
            return -1;
            break;

        case '?':
            /* getopt_long already printed an error message. */
            break;

        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if (debug) fprintf(stderr, "Resolution: %d\n", res);

    if (res == RESOLUTION_LOW) {
        fPtr = fopen("/proc/mstar/OMX/VMFE1/ENCODER_INFO/OBUF_pBuffer", "r");
        fLen = fopen("/proc/mstar/OMX/VMFE1/ENCODER_INFO/OBUF_nAllocLen", "r");
    } else {
        fPtr = fopen("/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_pBuffer", "r");
        fLen = fopen("/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_nAllocLen", "r");
    }
    fscanf(fPtr, "%x", &ivAddr);
    fclose(fPtr);
    fscanf(fLen, "%d", &size);
    fclose(fLen);

    ipAddr = rmm_virt2phys(ivAddr);

    if (debug) fprintf(stderr, "vaddr: 0x%08x - paddr: 0x%08x - size: %u\n", ivAddr, ipAddr, size);

    // open /dev/mem and error checking
    fMem = open(memDevice, O_RDONLY); // | O_SYNC);
    if (fMem < 0) {
        fprintf(stderr, "Failed to open the /dev/mem\n");
        return -1;
    }

    // mmap() the opened /dev/mem
    addr = (unsigned char *) (mmap(NULL, size, PROT_READ, MAP_SHARED, fMem, ipAddr));
    if (addr == MAP_FAILED) {
        fprintf(stderr, "Failed to map memory\n");
        return -1;
    }

    // close the character device
    close(fMem);

    if (res == RESOLUTION_LOW) {
        sprintf(filLenFile, "/proc/mstar/OMX/VMFE1/ENCODER_INFO/OBUF_nFilledLen");
        sprintf(timeStampFile, "/proc/mstar/OMX/VMFE1/ENCODER_INFO/OBUF_nTimeStamp");
    } else {
        sprintf(filLenFile, "/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_nFilledLen");
        sprintf(timeStampFile, "/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_nTimeStamp");
    }

    char stdoutbuf[262144];

    if (setvbuf(stdout, stdoutbuf, _IOFBF, sizeof(stdoutbuf)) != 0) {
        fprintf(stderr, "Error setting stdout buffer\n");
    }

    while (1) {

        fTime = fopen(timeStampFile, "r");
        fscanf(fTime, "%u", &time);
        fclose(fTime);
        oldTime = time;

        stream_started = 0;

        while(!stream_started) {
            fTime = fopen(timeStampFile, "r");
            fscanf(fTime, "%u", &time);
            fclose(fTime);

            if (time == oldTime) {
                usleep(8000);
                continue;
            }

            usleep(1000);

            fLen = fopen(filLenFile, "r");
            fscanf(fLen, "%d", &len);
            fclose(fLen);

            memcpy(buffer, addr, len);
            oldTime = time;
            if (memcmp(SPS, buffer, sizeof(SPS)) == 0) {
                if (debug) fprintf(stderr, "time: %u - len: %d\n", time, len);
                if (ssf0) {
                    fwrite(buffer, 1, 24, stdout);
                    fwrite(buffer + 76, 1, len - 76, stdout);
                } else {
                    fwrite(buffer, 1, len, stdout);
                }
                fflush(stdout);
                stream_started = 1;
            }
        }

        while(1) {
            fTime = fopen(timeStampFile, "r");
            fscanf(fTime, "%u", &time);
            fclose(fTime);
            if (debug) fprintf(stderr, "time: %u\n", time);

            if (time == oldTime) {
                usleep(8000);
                continue;
            } else if (time - oldTime <= 75000) {
                repeat = 1;
            } else if ((time - oldTime > 75000) && (time - oldTime <= 125000)) {
                fprintf(stderr, "frame lost: %u\n", time - oldTime);
#ifdef REPEAT
                repeat = 2;
#else
                repeat = 1;
#endif
            } else if (time - oldTime > 125000) {
                // If time - oldTime > 125000 (125 ms) assume sync lost
                fprintf(stderr, "sync lost: %u - %u\n", time, oldTime);
                repeat = 1;
                break;
            }

            usleep(1000);

            fLen = fopen(filLenFile, "r");
            fscanf(fLen, "%d", &len);
            fclose(fLen);
            if (debug) fprintf(stderr, "time: %u - len: %d\n", time, len);
            if (debug) fprintf(stderr, "milliseconds: %lld\n", current_timestamp());

            if (debug) fprintf(stderr, "copy buffer: len %d\n", len);
            memcpy(buffer, addr, len);
            memset(check1, '\0', sizeof(check1));
            fillCheck(check2, sizeof(check2), buffer, len);

            while (memcmp(check1, check2, sizeof(check1)) != 0) {
                usleep(1000);
                if (debug) fprintf(stderr, "copy again buffer: len %d\n", len);
                memcpy(buffer, addr, len);
                memcpy(check1, check2, sizeof(check1));
                fillCheck(check2, sizeof(check2), buffer, len);
            }
            oldTime = time;

            while (repeat > 0) {
                if (ssf0) {
                    if (memcmp(SEI_F0, buffer, sizeof(SEI_F0)) == 0) {
                        fwrite(buffer + 52, 1, len - 52, stdout);
                    } else if (memcmp(SPS, buffer, sizeof(SPS)) == 0) {
                        fwrite(buffer, 1, 24, stdout);
                        fwrite(buffer + 76, 1, len - 76, stdout);
                    } else {
                        fwrite(buffer, 1, len, stdout);
                    }
                } else {
                    fwrite(buffer, 1, len, stdout);
                }
                fflush(stdout);
                repeat--;
            }
        }
    }

    munmap(addr, size);
}
