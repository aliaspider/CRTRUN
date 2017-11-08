#include <sys/iosupport.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>

#include "util.h"
#include "common.h"
#include "ctr/ctr_debug.h"

FS_MediaType util_get_title_destination(u64 titleId)
{
   u16 platform = (u16)((titleId >> 48) & 0xFFFF);
   u16 category = (u16)((titleId >> 32) & 0xFFFF);
   u8 variation = (u8)(titleId & 0xFF);

   //     DSiWare                3DS                    DSiWare, System, DLP         Application           System Title
   return platform == 0x0003 || (platform == 0x0004 && ((category & 0x8011) != 0 || (category == 0x0000 && variation == 0x02))) ? MEDIATYPE_NAND : MEDIATYPE_SD;
}

static u32 sigSizes[6] = {0x240, 0x140, 0x80, 0x240, 0x140, 0x80};

u64 util_get_cia_title_id(u8 *cia)
{
   u32 headerSize = ((*(u32 *) &cia[0x00]) + 0x3F) & ~0x3F;
   u32 certSize = ((*(u32 *) &cia[0x08]) + 0x3F) & ~0x3F;
   u32 ticketSize = ((*(u32 *) &cia[0x0C]) + 0x3F) & ~0x3F;

   u8 *tmd = &cia[headerSize + certSize + ticketSize];

   return util_get_tmd_title_id(tmd);
}

u64 util_get_tmd_title_id(u8 *tmd)
{
   return __builtin_bswap64(*(u64 *) &tmd[sigSizes[tmd[0x03]] + 0x4C]);
}

#define HTTP_TIMEOUT 15000000000

#define MAKE_HTTP_USER_AGENT_(major, minor, micro) ("Mozilla/5.0 (Nintendo 3DS; Mobile; rv:10.0) Gecko/20100101 FBI/" #major "." #minor "." #micro)
#define MAKE_HTTP_USER_AGENT(major, minor, micro) MAKE_HTTP_USER_AGENT_(major, minor, micro)
#define HTTP_USER_AGENT MAKE_HTTP_USER_AGENT(VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO)

Result util_http_open(httpcContext *context, u32 *responseCode, const char *url, bool userAgent)
{
   return util_http_open_ranged(context, responseCode, url, userAgent, 0, 0);
}

Result util_http_open_ranged(httpcContext *context, u32 *responseCode, const char *url, bool userAgent, u32 rangeStart,
                             u32 rangeEnd)
{

   if (context == NULL || url == NULL)
      return R_FBI_INVALID_ARGUMENT;

   char currUrl[1024];
   strncpy(currUrl, url, sizeof(currUrl));

   char range[64];

   if (rangeEnd > rangeStart)
      snprintf(range, sizeof(range), "%lu-%lu", rangeStart, rangeEnd);

   else
      snprintf(range, sizeof(range), "%lu-", rangeStart);

   Result res = 0;

   bool resolved = false;
   u32 redirectCount = 0;

   while (R_SUCCEEDED(res) && !resolved && redirectCount < 32)
   {
      if (R_SUCCEEDED(res = httpcOpenContext(context, HTTPC_METHOD_GET, currUrl, 1)))
      {
         u32 response = 0;

         if (R_SUCCEEDED(res = httpcSetSSLOpt(context, SSLCOPT_DisableVerify))
               && (!userAgent || R_SUCCEEDED(res = httpcAddRequestHeaderField(context, "User-Agent", HTTP_USER_AGENT)))
               && (rangeStart == 0 || R_SUCCEEDED(res = httpcAddRequestHeaderField(context, "Range", range)))
               && R_SUCCEEDED(res = httpcSetKeepAlive(context, HTTPC_KEEPALIVE_ENABLED))
               && R_SUCCEEDED(res = httpcBeginRequest(context))
               && R_SUCCEEDED(res = httpcGetResponseStatusCodeTimeout(context, &response, HTTP_TIMEOUT)))
         {
            if (response == 301 || response == 302 || response == 303)
            {
               redirectCount++;

               memset(currUrl, '\0', sizeof(currUrl));

               if (R_SUCCEEDED(res = httpcGetResponseHeader(context, "Location", currUrl, sizeof(currUrl))))
                  httpcCloseContext(context);
            }
            else
            {
               resolved = true;

               if (responseCode != NULL)
                  *responseCode = response;

               if (response != 200)
                  res = R_FBI_HTTP_RESPONSE_CODE;
            }
         }

         if (R_FAILED(res))
            httpcCloseContext(context);
      }
   }

   if (R_SUCCEEDED(res) && redirectCount >= 32)
      res = R_FBI_TOO_MANY_REDIRECTS;

   return res;
}

Result util_http_get_size(httpcContext *context, u32 *size)
{
   if (context == NULL || size == NULL)
      return R_FBI_INVALID_ARGUMENT;

   return httpcGetDownloadSizeState(context, NULL, size);
}

Result util_http_get_file_name(httpcContext *context, char *out, u32 size)
{
   if (context == NULL || out == NULL)
      return R_FBI_INVALID_ARGUMENT;

   Result res = 0;

   char *header = (char *) calloc(1, size + 64);

   if (header != NULL)
   {
      if (R_SUCCEEDED(res = httpcGetResponseHeader(context, "Content-Disposition", header, size + 64)))
      {
         char *start = strstr(header, "filename=");

         if (start != NULL)
         {
            char format[32];
            snprintf(format, sizeof(format), "filename=\"%%%lu[^\"]\"", size);

            if (sscanf(start, format, out) != 1)
               res = R_FBI_BAD_DATA;
         }
         else
            res = R_FBI_BAD_DATA;
      }

      free(header);
   }
   else
      res = R_FBI_OUT_OF_MEMORY;

   return res;
}

Result util_http_read(httpcContext *context, u32 *bytesRead, void *buffer, u32 size)
{
   if (context == NULL || buffer == NULL)
      return R_FBI_INVALID_ARGUMENT;

   Result res = 0;

   u32 startPos = 0;

   if (R_SUCCEEDED(res = httpcGetDownloadSizeState(context, &startPos, NULL)))
   {
      res = HTTPC_RESULTCODE_DOWNLOADPENDING;

      u32 outPos = 0;

      while (res == HTTPC_RESULTCODE_DOWNLOADPENDING && outPos < size)
      {
         if (R_SUCCEEDED(res = httpcReceiveDataTimeout(context, &((u8 *) buffer)[outPos], size - outPos, HTTP_TIMEOUT))
               || res == HTTPC_RESULTCODE_DOWNLOADPENDING)
         {
            Result posRes = 0;
            u32 currPos = 0;

            if (R_SUCCEEDED(posRes = httpcGetDownloadSizeState(context, &currPos, NULL)))
               outPos = currPos - startPos;

            else
               res = posRes;
         }
      }

      if (res == HTTPC_RESULTCODE_DOWNLOADPENDING)
         res = 0;

      if (R_SUCCEEDED(res) && bytesRead != NULL)
         *bytesRead = outPos;
   }

   return res;
}

Result util_http_close(httpcContext *context)
{
   if (context == NULL)
      return R_FBI_INVALID_ARGUMENT;

   return httpcCloseContext(context);
}
