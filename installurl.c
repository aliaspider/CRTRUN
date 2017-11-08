#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <3ds.h>

#include "common.h"
#include "util.h"
#include "ctr/ctr_debug.h"

static Result action_install_url_open_dst(void *data, void *initialReadBlock, u64 size, u32 *handle)
{
   install_url_data *installData = (install_url_data *) data;

   Result res = 0;

   installData->responseCode = 0;
   installData->currTitleId = 0;

   if (*(u16 *) initialReadBlock == 0x2020)
   {
      u64 titleId = util_get_cia_title_id((u8 *) initialReadBlock);
      if(!titleId)
         return R_FBI_BAD_DATA;

      if (util_get_title_destination(titleId) == MEDIATYPE_NAND)
         return R_FBI_BAD_DATA;

      if (R_SUCCEEDED(res = AM_StartCiaInstall(MEDIATYPE_SD, handle)))
         installData->currTitleId = titleId;
   }
   else
      return R_FBI_BAD_DATA;

   return res;
}

static Result action_install_url_close_dst(void *data, bool succeeded, u32 handle)
{
   if (!succeeded)
      return AM_CancelCIAInstall(handle);

   return AM_FinishCiaInstall(handle);
}

static Result task_data_op_copy(data_op_data *data)
{
   data->currProcessed = 0;
   data->currTotal = 0;

   Result res = 0;

   httpcContext* srcHandle = (httpcContext *) calloc(1, sizeof(httpcContext));

    if (R_SUCCEEDED(res = util_http_open(srcHandle, &data->url_data->responseCode, data->url_data->url, true)))
   {
      data->currTotal = 0;
      if (R_SUCCEEDED(res = httpcGetDownloadSizeState(srcHandle, NULL, (u32*)&data->currTotal)))
      {
         if (data->currTotal == 0)
            res = R_FBI_BAD_DATA;
         else
         {
            static u8 buffer[0x4000];

            u32 dstHandle = 0;

            while (data->currProcessed < data->currTotal)
            {
               u32 bytesRead = 0;

               if (R_FAILED(res = util_http_read(srcHandle, &bytesRead, buffer, sizeof(buffer))))
                  break;

               if (!dstHandle && R_FAILED(res = action_install_url_open_dst(data->url_data, buffer, data->currTotal, &dstHandle)))
                  break;

               u32 bytesWritten = 0;

               if (R_FAILED(res = FSFILE_Write(dstHandle, &bytesWritten, data->currProcessed, buffer, bytesRead, 0)))
                  break;

               data->currProcessed += bytesWritten;

               printf("%llu/%llu\r", data->currProcessed, data->currTotal);

            }

            printf("\n");

            if (dstHandle != 0)
            {
               Result closeDstRes = action_install_url_close_dst(data->url_data, res == 0, dstHandle);

               if (R_SUCCEEDED(res))
                  res = closeDstRes;
            }
         }
      }

      Result closeSrcRes = httpcCloseContext(srcHandle);

      if (R_SUCCEEDED(res))
         res = closeSrcRes;
   }

   return res;
}

void action_install_url(const char *urls)
{
   install_url_data *data = (install_url_data *) calloc(1, sizeof(install_url_data));

   strncpy(data->url, "http://", 7);
   strncpy(&data->url[7], urls, sizeof(data->url) - 7);

   data->responseCode = 0;
   data->currTitleId = 0;

   data->installInfo.url_data = data;


   DEBUG_ERROR(task_data_op_copy(&data->installInfo));

   extern u64 currTitleId;

   currTitleId = data->currTitleId;

   free(data);
}
