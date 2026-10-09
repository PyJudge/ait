#ifndef UTF8_H
#define UTF8_H

/* git v2.53.0 utf8.h:6-6 */
typedef unsigned int ucs_char_t;  /* assuming 32bit int */

int is_utf8(const char *text);
#endif