
#include <stdio.h>
#include <stdlib.h>

#include <3ds.h>

#include "common.h"
#include "util.h"
#include "ctr/ctr_debug.h"


void wait_for_input(void)
{
   printf("\n\nPress Start.\n\n");
   fflush(stdout);

   while (aptMainLoop())
   {
      u32 kDown;

      hidScanInput();

      kDown = hidKeysDown();

      if (kDown & KEY_START)
         break;

      if (kDown & KEY_SELECT)
         exit(0);

      svcSleepThread(1000000);
   }
}

__attribute((aligned(0x1000)))
static u32 soc_buffer[0x100000 >> 2];

u64 currTitleId;

int main(int argc, char **argv)
{

   gfxInit(GSP_BGR8_OES, GSP_RGB565_OES, false);

   gfxSet3D(false);
   consoleInit(GFX_BOTTOM, NULL);

   socInit(soc_buffer, sizeof(soc_buffer));
   httpcInit(0);
   amInit();

#if 1
//   DEBUG_HOLD();
   currTitleId = 0x000400000BC00000ULL;
   netloaderTask();
#else
   remoteinstall_receive_urls_network();
#endif

   amExit();
   httpcExit();
   socExit();

   DEBUG_VAR64(currTitleId);

//   0x000400000BC00000ULL;
   if (currTitleId)
   {
      DEBUG_RESULT(APT_PrepareToDoApplicationJump(0, currTitleId, util_get_title_destination(currTitleId)));
      u8 param[0x300];
      u8 hmac[0x20];

      DEBUG_RESULT(APT_DoApplicationJump(param, sizeof(param), hmac));
   }

   wait_for_input();
   gfxExit();
   return 0;
}
