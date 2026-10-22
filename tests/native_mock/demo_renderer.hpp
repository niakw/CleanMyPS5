// Host-only renderer contract for exercising native DualSense navigation.
// This header is never copied into the real PS5 application.
#pragma once
#include <cstdint>
#include <string_view>

namespace ps5::demo {
enum class Color : std::uint32_t {
 background=0xff190d0a, panel=0xff301f17, white=0xffffffff,
 cyan=0xffffff00, magenta=0xffff00ff, yellow=0xff00ffff
};
class Canvas {
public:
 void clear(Color) noexcept {}
 void rectangle(unsigned,unsigned,unsigned,unsigned,Color) noexcept {}
 void circle(unsigned,unsigned,unsigned,Color) noexcept {}
 void triangle(unsigned,unsigned,unsigned,unsigned,Color) noexcept {}
 void text(unsigned,unsigned,std::string_view,unsigned,Color) noexcept {}
};
using DrawScene=void (*)(Canvas&) noexcept;
[[noreturn]] void run(DrawScene,std::string_view) noexcept;
}
