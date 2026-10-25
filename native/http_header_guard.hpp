// Shared conservative HTTP/1.x header syntax guard for local protocols.
// Header names are ASCII case-insensitive even when their values are not.
// Keep canonical spellings for accepted framing headers; reject disguised
// duplicates and any Transfer-Encoding regardless of casing.
// GPL-3.0-or-later.
#pragma once
#include <cstddef>
namespace cmp5_http {
enum class Field { other, content_length, content_type, transfer_encoding, invalid };
inline bool token_char(unsigned char c) noexcept {
 return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
    c=='!'||c=='#'||c=='$'||c=='%'||c=='&'||c=='\''||
    c=='*'||c=='+'||c=='-'||c=='.'||c=='^'||c=='_'||
    c=='`'||c=='|'||c=='~';
}
/* Capture string-literal extent in its TYPE before comparing untrusted
 * field names. A longer input must NEVER index beyond a short literal.
 * Template rejects unknown-size, dynamically supplied field names. */
template<std::size_t N>
inline bool ascii_ci_equal(const char*start,std::size_t n,
                           const char (&literal)[N]) noexcept {
 if(!start||n!=N-1)return false;
 for(std::size_t i=0;i<N-1;i++){
  unsigned char x=static_cast<unsigned char>(start[i]);
  unsigned char y=static_cast<unsigned char>(literal[i]);
  if(x>='A'&&x<='Z')x=static_cast<unsigned char>(x+32);
  if(y>='A'&&y<='Z')y=static_cast<unsigned char>(y+32);
  if(x!=y)return false;
 }
 return true;
}
inline Field classify(const char*line,std::size_t n) noexcept {
 if(!line||n==0)return Field::invalid;
 std::size_t sep=0;
 while(sep<n&&line[sep]!=':'){
  if(!token_char(static_cast<unsigned char>(line[sep])))
   return Field::invalid;
  ++sep;
 }
 if(sep==0||sep==n)return Field::invalid;
 // No control characters or obs-fold values on loopback. Conservative
 // rejection is safe for the pinned helper and compression engine.
 for(std::size_t i=sep+1;i<n;i++){
  const auto c=static_cast<unsigned char>(line[i]);
  if(c<0x20u||c>0x7eu)return Field::invalid;
 }
 if(ascii_ci_equal(line,sep,"Content-Length"))return Field::content_length;
 if(ascii_ci_equal(line,sep,"Content-Type"))return Field::content_type;
 if(ascii_ci_equal(line,sep,"Transfer-Encoding"))return Field::transfer_encoding;
 return Field::other;
}
} // namespace cmp5_http
