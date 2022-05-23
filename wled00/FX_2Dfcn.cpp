/*
  FX_2Dfcn.cpp contains all 2D utility functions
  
  LICENSE
  The MIT License (MIT)
  Copyright (c) 2022  Blaz Kristan (https://blaz.at/home)
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

  Parts of the code adapted from WLED Sound Reactive
*/
#include "wled.h"
#include "FX.h"
#include "palettes.h"

// setUpMatrix() - constructs ledmap array from matrix of panels with WxH pixels
// this converts physical (possibly irregular) LED arrangement into well defined
// array of logical pixels: fist entry corresponds to left-topmost logical pixel
// followed by horizontal pixels, when matrixWidth logical pixels are added they
// are followed by next row (down) of matrixWidth pixels (and so forth)
// note: matrix may be comprised of multiple panels each with different orientation
// but ledmap takes care of that. ledmap is constructed upon initialization
// so matrix should disable regular ledmap processing
void WS2812FX::setUpMatrix() {
  // erase old ledmap, just in case.
  if (customMappingTable != nullptr) delete[] customMappingTable;
  customMappingTable = nullptr;
  customMappingSize = 0;

  if (isMatrix) {
    matrixWidth  = hPanels * panelW;
    matrixHeight = vPanels * panelH;

    // safety check
    if (matrixWidth * matrixHeight > MAX_LEDS) {
      matrixWidth = getLengthTotal();
      matrixHeight = 1;
      isMatrix = false;
      return;
    }

    customMappingSize  = matrixWidth * matrixHeight;
    customMappingTable = new uint16_t[customMappingSize];

    if (customMappingTable != nullptr) {
      uint16_t startL; // index in custom mapping array (logical strip)
      uint16_t startP; // position of 1st pixel of panel on (virtual) strip
      uint16_t x, y, offset;
      uint8_t h = matrix.vertical ? vPanels : hPanels;
      uint8_t v = matrix.vertical ? hPanels : vPanels;

      for (uint8_t j=0, p=0; j<v; j++) {
        for (uint8_t i=0; i<h; i++, p++) {
          y = (matrix.vertical ? matrix.rightStart : matrix.bottomStart) ? v - j - 1 : j;
          x = (matrix.vertical ? matrix.bottomStart : matrix.rightStart) ? h - i - 1 : i;
          x = matrix.serpentine && j%2 ? h - x - 1 : x;

          startL = (matrix.vertical ? y : x) * panelW + (matrix.vertical ? x : y) * matrixWidth * panelH; // logical index (top-left corner)
          startP = p * panelW * panelH; // physical index (top-left corner)

          uint8_t H = panel[h*j + i].vertical ? panelW : panelH;
          uint8_t W = panel[h*j + i].vertical ? panelH : panelW;
          for (uint16_t l=0, q=0; l<H; l++) {
            for (uint16_t k=0; k<W; k++, q++) {
              y = (panel[h*j + i].vertical ? panel[h*j + i].rightStart : panel[h*j + i].bottomStart) ? H - l - 1 : l;
              x = (panel[h*j + i].vertical ? panel[h*j + i].bottomStart : panel[h*j + i].rightStart) ? W - k - 1 : k;
              x = (panel[h*j + i].serpentine && l%2) ? (W - x - 1) : x;
              offset = (panel[h*j + i].vertical ? y : x) + (panel[h*j + i].vertical ? x : y) * matrixWidth;
              customMappingTable[startL + offset] = startP + q;
            }
          }
        }
      }
      #ifdef WLED_DEBUG
      DEBUG_PRINT(F("Matrix ledmap:"));
      for (uint16_t i=0; i<customMappingSize; i++) {
        if (!(i%matrixWidth)) DEBUG_PRINTLN();
        DEBUG_PRINTF("%4d,", customMappingTable[i]);
      }
      DEBUG_PRINTLN();
      #endif
    } else {
      // memory allocation error
      matrixWidth = getLengthTotal();
      matrixHeight = 1;
      isMatrix = false;
      return;
    }
  } else { 
    // not a matrix set up
    matrixWidth = getLengthTotal();
    matrixHeight = 1;
  }
}

// XY(x,y) - gets pixel index within current segment
uint16_t IRAM_ATTR WS2812FX::XY(uint16_t x, uint16_t y) {
  uint16_t width  = SEGMENT.virtualWidth();   // segment width in logical pixels
  uint16_t height = SEGMENT.virtualHeight();  // segment height in logical pixels
/*
  // it may be unnecessary to perform transpose since pixels should be always addressed using XY() function
  if (SEGMENT.getOption(SEG_OPTION_TRANSPOSED)) {
    uint16_t t;
    // swap X & Y if segment transposed
    t = x; x = y; y = t;
    // swap width & height if segment transposed
    t = width; width = height; height = t;
  }
*/
  return (x%width) + (y%height) * width;
}

// get2DPixelIndex(x,y,seg) - returns an index of segment pixel in a matrix layout
// index still needs to undergo ledmap processing to represent actual physical pixel
// matrix is always organized by matrixHeight number of matrixWidth pixels from top to bottom, left to right
// so: pixel at get2DPixelIndex(5,6) in a 2D segment with [start=10, stop=19, startY=20, stopY=29 : 10x10 pixels]
// corresponds to pixel with logical index of 847 (0 based) if a 2D segment belongs to a 32x32 matrix.
// math: (matrixWidth * (startY + y)) + start + x => (32 * (20+6)) + 10 + 5 = 847
uint16_t IRAM_ATTR WS2812FX::get2DPixelIndex(uint16_t x, uint16_t y, uint8_t seg) {
  if (seg == 255) seg = _segment_index;
  x %= _segments[seg].width();  // just in case constrain x (wrap around)
  y %= _segments[seg].height(); // just in case constrain y (wrap around)
  return ((_segments[seg].startY + y) * matrixWidth) + _segments[seg].start + x;
}

void IRAM_ATTR WS2812FX::setPixelColorXY(uint16_t x, uint16_t y, byte r, byte g, byte b, byte w)
{
  if (!isMatrix) return; // not a matrix set-up

  if (_bri_t < 255) {  
    r = scale8(r, _bri_t);
    g = scale8(g, _bri_t);
    b = scale8(b, _bri_t);
    w = scale8(w, _bri_t);
  }
  uint32_t col = RGBW32(r, g, b, w);

  if (SEGMENT.getOption(SEG_OPTION_TRANSPOSED)) { uint16_t t = x; x = y; y = t; } // swap X & Y if segment transposed

  x *= SEGMENT.groupLength(); // expand to physical pixels
  y *= SEGMENT.groupLength(); // expand to physical pixels
  if (x >= SEGMENT.width() || y >= SEGMENT.height()) return;  // if pixel would fall out of segment just exit

  for (uint8_t j = 0; j < SEGMENT.grouping; j++) {   // groupping vertically
    for (uint8_t g = 0; g < SEGMENT.grouping; g++) { // groupping horizontally
      uint16_t index, xX = (x+g), yY = (y+j);
      if (xX >= SEGMENT.width() || yY >= SEGMENT.height()) continue; // we have reached one dimension's end

      if (SEGMENT.getOption(SEG_OPTION_REVERSED)  ) xX = SEGMENT.width()  - xX - 1;
      if (SEGMENT.getOption(SEG_OPTION_REVERSED_Y)) yY = SEGMENT.height() - yY - 1;

      index = get2DPixelIndex(xX, yY);
      if (index < customMappingSize) index = customMappingTable[index];
      busses.setPixelColor(index, col);

      if (SEGMENT.getOption(SEG_OPTION_MIRROR)) { //set the corresponding horizontally mirrored pixel
        index = get2DPixelIndex(SEGMENT.width() - xX - 1, yY);
        if (index < customMappingSize) index = customMappingTable[index];
        busses.setPixelColor(index, col);
      }
      if (SEGMENT.getOption(SEG_OPTION_MIRROR_Y)) { //set the corresponding vertically mirrored pixel
        index = get2DPixelIndex(xX, SEGMENT.height() - yY - 1);
        if (index < customMappingSize) index = customMappingTable[index];
        busses.setPixelColor(index, col);
      }
    }
  }
}

// not working correctly ATM
uint32_t WS2812FX::getPixelColorXY(uint16_t x, uint16_t y) {
  if (SEGMENT.getOption(SEG_OPTION_TRANSPOSED)) { uint16_t t = x; x = y; y = t; } // swap X & Y if segment transposed

  x *= SEGMENT.groupLength(); // expand to physical pixels
  y *= SEGMENT.groupLength(); // expand to physical pixels
  if (x >= SEGMENT.width() || y >= SEGMENT.height()) return 0;

  if (SEGMENT.getOption(SEG_OPTION_REVERSED)  ) x = SEGMENT.width()  - x - 1;
  if (SEGMENT.getOption(SEG_OPTION_REVERSED_Y)) y = SEGMENT.height() - y - 1;

  uint16_t index = get2DPixelIndex(x, y);
  if (index < customMappingSize) index = customMappingTable[index];

  return busses.getPixelColor(index);
}

/*
 * Blends the specified color with the existing pixel color.
 */
void WS2812FX::blendPixelColorXY(uint16_t x, uint16_t y, uint32_t color, uint8_t blend) {
  setPixelColorXY(x, y, color_blend(getPixelColorXY(x,y), color, blend));
}

// blurRow: perform a blur on a row of a rectangular matrix
void WS2812FX::blurRow(uint16_t row, fract8 blur_amount, CRGB* leds) {
  uint16_t width  = SEGMENT.virtualWidth();
  uint16_t height = SEGMENT.virtualHeight();
  if (row >= height) return;
  // blur one row
  uint8_t keep = 255 - blur_amount;
  uint8_t seep = blur_amount >> 1;
  CRGB carryover = CRGB::Black;
  for (uint16_t x = 0; x < width; x++) {
    CRGB cur = leds[XY(x,row)];
    CRGB part = cur;
    part.nscale8(seep);
    cur.nscale8(keep);
    cur += carryover;
    if (x) leds[XY(x-1,row)] += part;
    leds[XY(x,row)] = cur;
    carryover = part;
  }
}

// blurCol: perform a blur on a column of a rectangular matrix
void WS2812FX::blurCol(uint16_t col, fract8 blur_amount, CRGB* leds) {
  uint16_t width  = SEGMENT.virtualWidth();
  uint16_t height = SEGMENT.virtualHeight();
  if (col >= width) return;
  // blur one column
  uint8_t keep = 255 - blur_amount;
  uint8_t seep = blur_amount >> 1;
  CRGB carryover = CRGB::Black;
  for (uint16_t i = 0; i < height; i++) {
    CRGB cur = leds[XY(col,i)];
    CRGB part = cur;
    part.nscale8(seep);
    cur.nscale8(keep);
    cur += carryover;
    if (i) leds[XY(col,i-1)] += part;
    leds[XY(col,i)] = cur;
    carryover = part;
  }
}

// blur1d: one-dimensional blur filter. Spreads light to 2 line neighbors.
// blur2d: two-dimensional blur filter. Spreads light to 8 XY neighbors.
//
//           0 = no spread at all
//          64 = moderate spreading
//         172 = maximum smooth, even spreading
//
//         173..255 = wider spreading, but increasing flicker
//
//         Total light is NOT entirely conserved, so many repeated
//         calls to 'blur' will also result in the light fading,
//         eventually all the way to black; this is by design so that
//         it can be used to (slowly) clear the LEDs to black.

void WS2812FX::blur1d(CRGB* leds, fract8 blur_amount) {
  uint16_t height = SEGMENT.virtualHeight();
  for (uint16_t y = 0; y < height; y++) blurRow(y, blur_amount, leds);
}

void WS2812FX::blur2d(CRGB* leds, fract8 blur_amount) {
  uint16_t width  = SEGMENT.virtualWidth();  // same as SEGLEN
  uint16_t height = SEGMENT.virtualHeight();
  for (uint16_t i = 0; i < height; i++) blurRow(i, blur_amount, leds); // blur all rows
  for (uint16_t k = 0; k < width; k++)  blurCol(k, blur_amount, leds); // blur all columns
}

void WS2812FX::moveX(CRGB *leds, int8_t delta) {
  uint16_t width  = SEGMENT.virtualWidth();  // same as SEGLEN
  uint16_t height = SEGMENT.virtualHeight();
  if (delta) {
    if (delta > 0) {
      for (uint8_t y = 0; y < height; y++) {
        for (uint8_t x = 0; x < width; x++) {
          leds[XY(x, y)] = leds[XY(x + delta, y)];
        }
      }
    } else {
      for (uint8_t y = 0; y < height; y++) {
        for (uint8_t x = width - 1; x > 0; x--) {
          leds[XY(x, y)] = leds[XY(x + delta, y)];
        }
      }
    }
  }
}

void WS2812FX::moveY(CRGB *leds, int8_t delta) {
  uint16_t width  = SEGMENT.virtualWidth();  // same as SEGLEN
  uint16_t height = SEGMENT.virtualHeight();
  if (delta) {
    if (delta > 0) {
      for (uint8_t x = 0; x < height; x++) {
        for (uint8_t y = 0; y < width; y++) {
          leds[XY(x, y)] = leds[XY(x, y + delta)];
        }
      }
    } else {
      for (uint8_t x = 0; x < height; x++) {
        for (uint8_t y = width - 1; y > 0; y--) {
          leds[XY(x, y)] = leds[XY(x, y + delta)];
        }
      }
    }
  }
}


//ewowi20210628: new functions moved from colorutils: add segment awareness

void WS2812FX::fill_solid(CRGB* leds, CRGB color) {
  uint16_t w  = SEGMENT.virtualWidth();
  uint16_t h = SEGMENT.virtualHeight();
  for(uint16_t y = 0; y < h; y++) for (uint16_t x = 0; x < w; x++) {
    if (leds) leds[XY(x,y)] = color;
    else setPixelColorXY(x, y, color);
  }
}

// by stepko, taken from https://editor.soulmatelights.com/gallery/573-blobs
void WS2812FX::fill_circle(CRGB* leds, uint16_t cx, uint16_t cy, uint8_t radius, CRGB col) {
  for (int16_t y = -radius; y <= radius; y++) {
    for (int16_t x = -radius; x <= radius; x++) {
      if (x * x + y * y <= radius * radius &&
          int16_t(cx)+x>=0 && int16_t(cy)+y>=0 &&
          int16_t(cx)+x<SEGMENT.virtualWidth() && int16_t(cy)+y<SEGMENT.virtualHeight())
        leds[XY(cx + x, cy + y)] += col;
    }
  }
}

void WS2812FX::fadeToBlackBy(CRGB* leds, uint8_t fadeBy) {
  nscale8(leds, 255 - fadeBy);
}

void WS2812FX::nscale8(CRGB* leds, uint8_t scale) {
  uint16_t w  = SEGMENT.virtualWidth();
  uint16_t h = SEGMENT.virtualHeight();
  for(uint16_t y = 0; y < h; y++) for (uint16_t x = 0; x < w; x++) {
    if (leds) leds[XY(x,y)].nscale8(scale);
    else setPixelColorXY(x, y, col_to_crgb(getPixelColorXY(x, y)).nscale8(scale));
  }
}

void WS2812FX::setPixels(CRGB* leds) {
  uint16_t w = SEGMENT.virtualWidth();
  uint16_t h = SEGMENT.virtualHeight();
  for (uint16_t y = 0; y < h; y++) for (uint16_t x = 0; x < w; x++) setPixelColorXY(x, y, leds[XY(x,y)]);
}

//line function
void WS2812FX::drawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, CRGB c, CRGB *leds) {
  int16_t dx = abs(x1-x0), sx = x0<x1 ? 1 : -1;
  int16_t dy = abs(y1-y0), sy = y0<y1 ? 1 : -1; 
  int16_t err = (dx>dy ? dx : -dy)/2, e2;
  for (;;) {
    if (leds == nullptr) setPixelColorXY(x0,y0,c);
    else                 leds[XY(x0,y0)] = c;
    if (x0==x1 && y0==y1) break;
    e2 = err;
    if (e2 >-dx) { err -= dy; x0 += sx; }
    if (e2 < dy) { err += dx; y0 += sy; }
  }
}

#define WU_WEIGHT(a,b) ((uint8_t) (((a)*(b)+(a)+(b))>>8))
void WS2812FX::wu_pixel(CRGB *leds, uint32_t x, uint32_t y, CRGB c) {      //awesome wu_pixel procedure by reddit u/sutaburosu
  // extract the fractional parts and derive their inverses
  uint8_t xx = x & 0xff, yy = y & 0xff, ix = 255 - xx, iy = 255 - yy;
  // calculate the intensities for each affected pixel
  uint8_t wu[4] = {WU_WEIGHT(ix, iy), WU_WEIGHT(xx, iy),
                   WU_WEIGHT(ix, yy), WU_WEIGHT(xx, yy)};
  // multiply the intensities by the colour, and saturating-add them to the pixels
  for (uint8_t i = 0; i < 4; i++) {
    uint16_t xy = XY((x >> 8) + (i & 1), (y >> 8) + ((i >> 1) & 1));
    leds[xy].r = qadd8(leds[xy].r, c.r * wu[i] >> 8);
    leds[xy].g = qadd8(leds[xy].g, c.g * wu[i] >> 8);
    leds[xy].b = qadd8(leds[xy].b, c.b * wu[i] >> 8);
  }
}
#undef WU_WEIGHT
