#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <3ds.h>

#include "common.h"
#include "util.h"
#include "ctr/ctr_debug.h"

typedef struct
{
   char urls[INSTALL_URLS_MAX][INSTALL_URL_MAX];

   char path3dsx[FILE_PATH_MAX];

   void *userData;
   void (*finished)(void *data);

   u32 responseCode;
   u64 currTitleId;
   httpcContext *currContext;

   data_op_data installInfo;
} install_url_data;

static void action_install_url_free_data(install_url_data *data)
{
   if (data->finished != NULL)
      data->finished(data->userData);

   free(data);
}

static Result action_install_url_open_src(void *data, u32 index, u32 *handle)
{
   install_url_data *installData = (install_url_data *) data;

   Result res = 0;

   httpcContext *context = (httpcContext *) calloc(1, sizeof(httpcContext));

   if (R_SUCCEEDED(res = util_http_open(context, &installData->responseCode, installData->urls[index], true)))
   {
      *handle = (u32) context;

      installData->currContext = context;
   }
   else
      free(context);

   return res;
}

static Result action_install_url_close_src(void *data, u32 index, bool succeeded, u32 handle)
{
   ((install_url_data *) data)->currContext = NULL;

   return util_http_close((httpcContext *) handle);
}

static Result action_install_url_get_src_size(void *data, u32 handle, u64 *size)
{
   u32 downloadSize = 0;
   Result res = util_http_get_size((httpcContext *) handle, &downloadSize);

   *size = downloadSize;
   return res;
}

static Result action_install_url_read_src(void *data, u32 handle, u32 *bytesRead, void *buffer, u64 offset, u32 size)
{
   return util_http_read((httpcContext *) handle, bytesRead, buffer, size);
}

static Result action_install_url_open_dst(void *data, u32 index, void *initialReadBlock, u64 size, u32 *handle)
{
   install_url_data *installData = (install_url_data *) data;

   Result res = 0;

   installData->responseCode = 0;
   installData->currTitleId = 0;

   if (*(u16 *) initialReadBlock == 0x2020)
   {
      u64 titleId = util_get_cia_title_id((u8 *) initialReadBlock);

      FS_MediaType dest = util_get_title_destination(titleId);

      bool n3ds = false;

      if (R_SUCCEEDED(APT_CheckNew3DS(&n3ds)) && !n3ds && ((titleId >> 28) & 0xF) == 2)
         printf("Title is intended for New 3DS systems.\n");

      // Deleting FBI before it reinstalls itself causes issues.
      u64 currTitleId = 0;
      FS_MediaType currMediaType = MEDIATYPE_NAND;

      if (envIsHomebrew()
            || R_FAILED(APT_GetAppletInfo((NS_APPID) envGetAptAppId(), &currTitleId, (u8 *) &currMediaType, NULL, NULL, NULL))
            || titleId != currTitleId || dest != currMediaType)
      {
         AM_DeleteTitle(dest, titleId);
         AM_DeleteTicket(titleId);

         if (dest == MEDIATYPE_SD)
            AM_QueryAvailableExternalTitleDatabase(NULL);
      }

      if (R_SUCCEEDED(res = AM_StartCiaInstall(dest, handle)))
         installData->currTitleId = titleId;
   }
   else
      res = R_FBI_BAD_DATA;

   return res;
}

static Result action_install_url_close_dst(void *data, u32 index, bool succeeded, u32 handle)
{
   install_url_data *installData = (install_url_data *) data;

   Result res = 0;

   if (succeeded)
   {
      if (R_SUCCEEDED(res = AM_FinishCiaInstall(handle)))
      {
         util_import_seed(NULL, installData->currTitleId);

         if (installData->currTitleId == 0x0004013800000002 || installData->currTitleId == 0x0004013820000002)
            res = AM_InstallFirm(installData->currTitleId);
      }
   }
   else
      res = AM_CancelCIAInstall(handle);

   return res;
}

static Result action_install_url_write_dst(void *data, u32 handle, u32 *bytesWritten, void *buffer, u64 offset,
      u32 size)
{
   return FSFILE_Write(handle, bytesWritten, offset, buffer, size, 0);
}


static bool action_install_url_error(void *data, u32 index, Result res)
{
   install_url_data *installData = (install_url_data *) data;

   if (res == R_FBI_CANCELLED)
   {
      printf("Failure, Install cancelled.\n");
      return false;
   }
   else if (res != R_FBI_WRONG_SYSTEM)
   {
      char *url = installData->urls[index];

      if (res == R_FBI_HTTP_RESPONSE_CODE)
         printf("Failed to install from URL.\n%s...\nHTTP server returned response code %d", url, installData->responseCode);
      else
         printf("Failed to install from URL.\n%s", url);

   }

   return index < installData->installInfo.total - 1;
}

static void action_install_url_install_update(void *data, float *progress, char *text)
{
   install_url_data *installData = (install_url_data *) data;

   if (installData->installInfo.finished)
   {
      if (R_SUCCEEDED(installData->installInfo.result))
         printf("Success, Install finished.\n");

      action_install_url_free_data(installData);

      return;
   }

   if (hidKeysDown() & KEY_B)
      svcSignalEvent(installData->installInfo.cancelEvent);

   *progress = installData->installInfo.currTotal != 0 ? (float)((double) installData->installInfo.currProcessed /
               (double) installData->installInfo.currTotal) : 0;
   printf("%lu / %lu\n%.2f %s / %.2f %s\n%.2f %s/s, ETA %s\n", installData->installInfo.processed,
          installData->installInfo.total, util_get_display_size(installData->installInfo.currProcessed),
          util_get_display_size_units(installData->installInfo.currProcessed),
          util_get_display_size(installData->installInfo.currTotal),
          util_get_display_size_units(installData->installInfo.currTotal),
          util_get_display_size(installData->installInfo.copyBytesPerSecond),
          util_get_display_size_units(installData->installInfo.copyBytesPerSecond),
          util_get_display_eta(installData->installInfo.estimatedRemainingSeconds));
}

static Result task_data_op_copy(data_op_data *data, u32 index)
{
   data->currProcessed = 0;
   data->currTotal = 0;

   data->copyBytesPerSecond = 0;

   Result res = 0;

   u32 srcHandle = 0;

   if (R_SUCCEEDED(res = data->openSrc(data->data, index, &srcHandle)))
   {
      if (R_SUCCEEDED(res = data->getSrcSize(data->data, srcHandle, &data->currTotal)))
      {
         if (data->currTotal == 0)
         {
            if (data->copyEmpty)
            {
               u32 dstHandle = 0;

               if (R_SUCCEEDED(res = data->openDst(data->data, index, NULL, data->currTotal, &dstHandle)))
                  res = data->closeDst(data->data, index, true, dstHandle);
            }
            else
               res = R_FBI_BAD_DATA;
         }
         else
         {
            u8 *buffer = (u8 *) calloc(1, data->copyBufferSize);

            if (buffer != NULL)
            {
               u32 dstHandle = 0;

               u64 ioStartTime = 0;
               u64 lastBytesPerSecondUpdate = osGetTime();
               u32 bytesSinceUpdate = 0;

               bool firstRun = true;

               while (data->currProcessed < data->currTotal)
               {
                  u32 bytesRead = 0;

                  if (R_FAILED(res = data->readSrc(data->data, srcHandle, &bytesRead, buffer, data->currProcessed,
                                                   data->copyBufferSize)))
                     break;

                  if (firstRun)
                  {
                     firstRun = false;

                     if (R_FAILED(res = data->openDst(data->data, index, buffer, data->currTotal, &dstHandle)))
                        break;
                  }

                  u32 bytesWritten = 0;

                  if (R_FAILED(res = data->writeDst(data->data, dstHandle, &bytesWritten, buffer, data->currProcessed, bytesRead)))
                     break;

                  data->currProcessed += bytesWritten;
                  bytesSinceUpdate += bytesWritten;

                  u64 time = osGetTime();
                  u64 elapsed = time - lastBytesPerSecondUpdate;

                  if (elapsed >= 1000)
                  {
                     data->copyBytesPerSecond = (u32)(bytesSinceUpdate / (elapsed / 1000.0f));

                     if (ioStartTime != 0)
                        data->estimatedRemainingSeconds = (u32)((data->currTotal - data->currProcessed) / (data->currProcessed / ((
                              time - ioStartTime) / 1000.0f)));

                     else
                        data->estimatedRemainingSeconds = 0;

                     if (ioStartTime == 0 && data->currProcessed > 0)
                        ioStartTime = time;

                     bytesSinceUpdate = 0;
                     lastBytesPerSecondUpdate = time;
                  }
                  printf("%llu/%llu %.2fKBytes/s (%s)\r", data->currProcessed, data->currTotal, data->copyBytesPerSecond / 1024.0f,
                         data->estimatedRemainingSeconds);

               }
               printf("\n");

               if (dstHandle != 0)
               {
                  Result closeDstRes = data->closeDst(data->data, index, res == 0, dstHandle);

                  if (R_SUCCEEDED(res))
                     res = closeDstRes;
               }

               free(buffer);
            }
            else
               res = R_FBI_OUT_OF_MEMORY;
         }
      }

      Result closeSrcRes = data->closeSrc(data->data, index, res == 0, srcHandle);

      if (R_SUCCEEDED(res))
         res = closeSrcRes;
   }

   return res;
}

void action_install_url(const char *urls)
{
   install_url_data *data = (install_url_data *) calloc(1, sizeof(install_url_data));
   data->installInfo.total = 0;

   size_t payloadLen = strlen(urls);

   if (payloadLen > 0)
   {
      const char *currStart = urls;

      while (data->installInfo.total < INSTALL_URLS_MAX && currStart - urls < payloadLen)
      {
         const char *currEnd = strchr(currStart, '\n');

         if (currEnd == NULL)
            currEnd = urls + payloadLen;

         u32 len = currEnd - currStart;

         if ((len < 7 || strncmp(currStart, "http://", 7) != 0) && (len < 8 || strncmp(currStart, "https://", 8) != 0))
         {
            if (len > INSTALL_URL_MAX - 7)
               len = INSTALL_URL_MAX - 7;

            strncpy(data->urls[data->installInfo.total], "http://", 7);
            strncpy(&data->urls[data->installInfo.total][7], currStart, len);
         }
         else
         {
            if (len > INSTALL_URL_MAX)
               len = INSTALL_URL_MAX;

            strncpy(data->urls[data->installInfo.total], currStart, len);
         }

         data->installInfo.total++;
         currStart = currEnd + 1;
      }
   }

   data->responseCode = 0;
   data->currTitleId = 0;

   data->installInfo.data = data;

   data->installInfo.op = DATAOP_COPY;

   data->installInfo.copyBufferSize = 128 * 1024;
   data->installInfo.copyEmpty = false;

   data->installInfo.processed = data->installInfo.total;

   data->installInfo.openSrc = action_install_url_open_src;
   data->installInfo.closeSrc = action_install_url_close_src;
   data->installInfo.getSrcSize = action_install_url_get_src_size;
   data->installInfo.readSrc = action_install_url_read_src;

   data->installInfo.openDst = action_install_url_open_dst;
   data->installInfo.closeDst = action_install_url_close_dst;
   data->installInfo.writeDst = action_install_url_write_dst;

   data->installInfo.error = action_install_url_error;

   data->installInfo.finished = true;

   DEBUG_ERROR(task_data_op_copy(&data->installInfo, 0));

   extern u64 currTitleId;

   currTitleId = data->currTitleId;

   action_install_url_free_data(data);
}
