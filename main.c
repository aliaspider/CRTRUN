

#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>

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


int main(int argc, char** argv)
{

   gfxInit(GSP_BGR8_OES,GSP_RGB565_OES,false);

   gfxSet3D(false);
   consoleInit(GFX_BOTTOM, NULL);

//   osSetSpeedupEnable(true);

   printf("test\n");

   wait_for_input();

   return 0;
}
