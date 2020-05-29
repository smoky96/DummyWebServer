#include <assert.h>

#include <cstring>
#include <string>

inline unsigned char ToHex(unsigned char x) { return x > 9 ? x + 55 : x + 48; }

inline unsigned char FromHex(unsigned char x) {
  unsigned char y;
  if (x >= 'A' && x <= 'Z')
    y = x - 'A' + 10;
  else if (x >= 'a' && x <= 'z')
    y = x - 'a' + 10;
  else if (x >= '0' && x <= '9')
    y = x - '0';
  else
    assert(0);
  return y;
}

inline int UrlEncode(const char* str, char* buf, size_t bufsize) {
  size_t length = strlen(str);
  size_t cur = 0;
  for (size_t i = 0; i < length; i++, cur++) {
    if (cur == bufsize - 1) return -1;
    if (isalnum((unsigned char)str[i]) || (str[i] == '-') || (str[i] == '_') ||
        (str[i] == '.') || (str[i] == '*'))
      buf[cur] = str[i];
    else if (str[i] == ' ')
      buf[cur] = '+';
    else {
      if (cur == bufsize - 3) return -1;
      buf[cur] = '%';
      cur += 1;
      buf[cur] = ToHex((unsigned char)str[i] >> 4);
      cur += 1;
      buf[cur] = ToHex((unsigned char)str[i] % 16);
    }
  }
  buf[cur] = '\0';
  return 0;
}

inline int UrlDecode(const char* str, char* buf, size_t bufsize) {
  size_t length = strlen(str);
  size_t cur = 0;
  for (size_t i = 0; i < length; i++, cur++) {
    if (cur == bufsize - 1) return -1;
    if (str[i] == '+')
      buf[cur] = ' ';
    else if (str[i] == '%') {
      assert(i + 2 < length);
      unsigned char high = FromHex((unsigned char)str[++i]);
      unsigned char low = FromHex((unsigned char)str[++i]);
      buf[cur] = high * 16 + low;
    } else
      buf[cur] = str[i];
  }
  buf[cur] = '\0';
  return 0;
}
