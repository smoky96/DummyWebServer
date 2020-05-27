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
  char tmp[bufsize];
  size_t length = strlen(str);
  size_t i;
  for (i = 0; i < length; i++) {
    if (i == bufsize - 1) return -1;
    if (isalnum((unsigned char)str[i]) || (str[i] == '-') || (str[i] == '_') ||
        (str[i] == '.') || (str[i] == '*'))
      tmp[i] = str[i];
    else if (str[i] == ' ')
      tmp[i] = '+';
    else {
      if (i == bufsize - 3) return -1;
      tmp[i] = '%';
      i += 1;
      tmp[i] = ToHex((unsigned char)str[i] >> 4);
      i += 1;
      tmp[i] = ToHex((unsigned char)str[i] % 16);
    }
  }
  tmp[i] = '\0';
  for (i = 0; i <= strlen(tmp); ++i) {
    buf[i] = tmp[i];
  }
  return 0;
}

inline int UrlDecode(const char* str, char* buf, size_t bufsize) {
  char tmp[bufsize];
  size_t length = strlen(str);
  size_t i;
  for (i = 0; i < length; i++) {
    if (i == bufsize - 1) return -1;
    if (str[i] == '+')
      tmp[i] = ' ';
    else if (str[i] == '%') {
      assert(i + 2 < length);
      unsigned char high = FromHex((unsigned char)str[++i]);
      unsigned char low = FromHex((unsigned char)str[++i]);
      tmp[i] = high * 16 + low;
    } else
      tmp[i] = str[i];
  }
  tmp[i] = '\0';
  for (i = 0; i <= strlen(tmp); ++i) {
    buf[i] = tmp[i];
  }
  return 0;
}
