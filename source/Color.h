
#ifndef COLOR_H_
#define COLOR_H_

#if 0
#include <cstdint>
typedef unsigned long Pixel;

class QColor;

struct Color {
	uint16_t r;
	uint16_t g;
	uint16_t b;
};

QColor toQColor(const Color &c);
QColor toQColor(Pixel pixel);
Pixel toPixel(const QColor &color);
#endif

#endif
