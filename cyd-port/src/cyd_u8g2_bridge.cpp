/*
    cyd_u8g2_bridge.cpp — nyanBOX CYD port

    The single ODR definition of the shared TFT_eSPI instance used by the
    U8g2->ILI9341 bridge (include/cyd_u8g2_bridge.h). All display config comes
    from platformio.ini build_flags (USER_SETUP_LOADED=1 + ILI9341_DRIVER etc).

    MUST NOT include pindefs.h (keeps real digitalRead / TFT SPI in this TU).
*/
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();
