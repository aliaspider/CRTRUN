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
   char url[INSTALL_URL_MAX];

   char path3dsx[FILE_PATH_MAX];

   void *userData;

   u32 responseCode;
   u64 currTitleId;

   data_op_data installInfo;
} install_url_data;

static Result action_install_url_open_src(void *data, u32 *handle)
{
   install_url_data *installData = (install_url_data *) data;

   Result res = 0;

   httpcContext *context = (httpcContext *) calloc(1, sizeof(httpcContext));

   if (R_SUCCEEDED(res = util_http_open(context, &installData->responseCode, installData->url, true)))
      *handle = (u32) context;
   else
      free(context);

   return res;
}

static Result action_install_url_open_dst(void *data, void *initialReadBlock, u64 size, u32 *handle)
{
   install_url_data *installData = (install_url_data *) data;

   Result res = 0;

   installData->responseCode = 0;
   installData->currTitleId = 0;

   DEBUG_VAR(initialReadBlock);

   if (*(u16 *) initialReadBlock == 0x2020)
   {
      u64 titleId = util_get_cia_title_id((u8 *) initialReadBlock);

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

   u32 srcHandle = 0;

   if (R_SUCCEEDED(res = action_install_url_open_src(data->data, &srcHandle)))
   {
      data->currTotal = 0;
      if (R_SUCCEEDED(res = httpcGetDownloadSizeState((httpcContext *) srcHandle, NULL, (u32*)&data->currTotal)))
      {
         if (data->currTotal == 0)
            res = R_FBI_BAD_DATA;
         else
         {
            static u8 buffer[128 * 1024];

            u32 dstHandle = 0;

            while (data->currProcessed < data->currTotal)
            {
               u32 bytesRead = 0;

               if (R_FAILED(res = util_http_read((httpcContext *) srcHandle, &bytesRead, buffer, sizeof(buffer))))
                  break;

               if (!dstHandle && R_FAILED(res = action_install_url_open_dst(data->data, buffer, data->currTotal, &dstHandle)))
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
               Result closeDstRes = action_install_url_close_dst(data->data, res == 0, dstHandle);

               if (R_SUCCEEDED(res))
                  res = closeDstRes;
            }
         }
      }

      Result closeSrcRes = util_http_close((httpcContext *) srcHandle);

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

   data->installInfo.data = data;


   DEBUG_ERROR(task_data_op_copy(&data->installInfo));

   extern u64 currTitleId;

   currTitleId = data->currTitleId;

   free(data);
}
