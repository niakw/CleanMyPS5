// Host-only renderer contract for exercising native DualSense navigation.
// This header is never copied into the real PS5 application.
#pragma once
#include <cstdint>
#include <string_view>
#include <vector>
#include <string>
#include <fstream>

namespace ps5::demo {
enum class Color : std::uint32_t {
 background=0xff190d0a, panel=0xff301f17, white=0xffffffff,
 cyan=0xffffff00, magenta=0xffff00ff, yellow=0xff00ffff
};
class Canvas {
 public:
  struct Command {
   char kind;
   unsigned x,y,w,h,scale;
   Color color;
   std::string text;
  };
  std::vector<Command> commands;
  void clear(Color color) noexcept {
   commands.clear();
   commands.push_back({'C',0,0,1920,1080,0,color,{}});
  }
  void rectangle(unsigned x,unsigned y,unsigned w,unsigned h,Color color) noexcept {
   commands.push_back({'R',x,y,w,h,0,color,{}});
  }
  void circle(unsigned x,unsigned y,unsigned r,Color color) noexcept {
   commands.push_back({'O',x,y,r,0,0,color,{}});
  }
  void triangle(unsigned x,unsigned y,unsigned w,unsigned h,Color color) noexcept {
   commands.push_back({'T',x,y,w,h,0,color,{}});
  }
  void text(unsigned x,unsigned y,std::string_view value,unsigned scale,Color color) noexcept {
   commands.push_back({'X',x,y,0,0,scale,color,std::string(value)});
  }
  void blit(unsigned x,unsigned y,unsigned w,unsigned h,
            const std::uint32_t*pixels,unsigned source_w,unsigned source_h) noexcept {
   if(pixels&&source_w&&source_h)
    commands.push_back({'I',x,y,w,h,0,Color::cyan,"PREVIEW PHOTO TEST"});
  }
  bool save_trace(const char*path) const {
   std::ofstream stream(path);
   if(!stream)return false;
   for(const auto&op:commands){
    std::string cleaned=op.text;
    for(char&ch:cleaned)if(ch=='\t'||ch=='\n'||ch=='\r')ch=' ';
    stream<<op.kind<<'\t'<<op.x<<'\t'<<op.y<<'\t'<<op.w<<'\t'
          <<op.h<<'\t'<<op.scale<<'\t'<<static_cast<std::uint32_t>(op.color)
          <<'\t'<<cleaned<<'\n';
   }
   return bool(stream);
  }
};
using DrawScene=void (*)(Canvas&) noexcept;
[[noreturn]] void run(DrawScene,std::string_view) noexcept;
}
