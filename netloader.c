
#include <stdbool.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <zlib.h>
#include <3ds.h>

#include "ctr/ctr_debug.h"

#define NETLOADER_PORT 17491
#define ZLIB_CHUNK (16 * 1024)

#define ENTRY_ARGBUFSIZE   0x400
typedef struct
{
   char *dst;
   u32 buf[ENTRY_ARGBUFSIZE / sizeof(u32)];
} argData_s;


static void recvall(int sock, void *buffer, int size, int flags)
{
   int ret;
   size_t read = 0;
   while (read < size)
   {
      ret = recv(sock, buffer + read, size - read, flags);

      if(ret > 0)
         read += ret;
      else if (errno != EAGAIN && errno != EWOULDBLOCK)
      {
         DEBUG_ERROR(errno);
         return;
      }
   }
}

static u32 sigSizes[6] = {0x240, 0x140, 0x80, 0x240, 0x140, 0x80};

static u64 get_tmd_title_id(u8 *tmd)
{
   return __builtin_bswap64(*(u64 *) &tmd[sigSizes[tmd[0x03]] + 0x4C]);
}

static u64 get_cia_title_id(u8 *cia)
{
   u32 headerSize = ((*(u32 *) &cia[0x00]) + 0x3F) & ~0x3F;
   u32 certSize = ((*(u32 *) &cia[0x08]) + 0x3F) & ~0x3F;
   u32 ticketSize = ((*(u32 *) &cia[0x0C]) + 0x3F) & ~0x3F;

   u8 *tmd = &cia[headerSize + certSize + ticketSize];

   return get_tmd_title_id(tmd);
}


static int decompress(int sock, FILE *fh, size_t filesize)
{
   static unsigned char in[ZLIB_CHUNK];
   static unsigned char out[ZLIB_CHUNK];

   int ret;
   z_stream stream = {};

   inflateInit(&stream);


   Handle cia_handle;
   DEBUG_RESULT(AM_StartCiaInstall(MEDIATYPE_SD, &cia_handle));
   do
   {
      stream.avail_out = sizeof(out);
      stream.next_out = out;
      while(stream.avail_out > 0 && ret != Z_STREAM_END)
      {
         if(!stream.avail_in)
         {
            stream.next_in = in;
            recvall(sock, &stream.avail_in, 4, 0);
            if(stream.avail_in > sizeof(in))
               return -1;

            recvall(sock, stream.next_in, stream.avail_in, 0);
         }
         DEBUG_ERROR(ret = inflate(&stream, Z_NO_FLUSH));
      }

      extern u64 currTitleId;

      if(!currTitleId)
         currTitleId = get_cia_title_id(out);

      if(cia_handle)
      {
         u32 tmp;
         Result res;
         if(R_FAILED(res = FSFILE_Write(cia_handle, &tmp, stream.total_out - (sizeof(out) - stream.avail_out),
                                        out, sizeof(out) - stream.avail_out, 0)))
         {
            DEBUG_RESULT(res);
            AM_CancelCIAInstall(cia_handle);
            cia_handle = 0;
         }
      }

      printf("\r%lu (%ld%%)",stream.total_out, (100 * stream.total_out) / filesize);
   }while (ret != Z_STREAM_END);
   printf("\n");

   if(cia_handle)
      AM_FinishCiaInstall(cia_handle);

   inflateEnd(&stream);

   return ret;
}

void netloaderTask(void)
{
   static char fbuf[64 * 1024];
   int listenfd = -1;
   int datafd   = -1;
   size_t filelen = 0;

   struct sockaddr_in serv_addr;
   memset(&serv_addr.sin_zero, '0', sizeof(serv_addr.sin_zero));
   serv_addr.sin_family = AF_INET;
   serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
   serv_addr.sin_port = htons(NETLOADER_PORT);

   DEBUG_ERROR(listenfd = socket(AF_INET, SOCK_STREAM, 0));

   if(listenfd < 0)
      return;

   DEBUG_ERROR(bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)));
   DEBUG_ERROR(fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL) | O_NONBLOCK));
   DEBUG_ERROR(listen(listenfd, 10));

   struct in_addr addr = {(in_addr_t) gethostid()};
   printf("Waiting for 3dslink connection...\n\nPress B to cancel\n\nIP: %s\nPort: 5000\n", inet_ntoa(addr));

   while (datafd < 0)
   {
      hidScanInput();
      if (hidKeysDown() & KEY_B)
         return;

      datafd = accept(listenfd, NULL, NULL);

      if (datafd < 0)
      {
         if (errno != -EWOULDBLOCK && errno != EWOULDBLOCK)
         {
            DEBUG_ERROR(errno);
            return;
         }
      }
      svcSleepThread(16666666ULL);
   }
   close(listenfd);
   listenfd = -1;

   int namelen;
   recvall(datafd, &namelen, 4, 0);
   recvall(datafd, fbuf, namelen, 0);
   fbuf[namelen] = 0;
   recvall(datafd, (int *)&filelen, 4, 0);

   int response = 0;
   send(datafd, &response, sizeof(response), 0);

   decompress(datafd, NULL, filelen);

   send(datafd, &response, sizeof(response), 0);

   int cmdlen;
   recvall(datafd, &cmdlen, 4, 0);
   recvall(datafd, fbuf, cmdlen, 0);
   char *arg = fbuf;
   while(arg < fbuf + cmdlen)
   {
      DEBUG_STR(arg);
      arg += strlen(arg) + 1;
   }

   close(datafd);
   datafd = -1;
}
