#pragma once
// Compile-check stub. Not libcurl. See ../README.md.
#include <cstddef>
#define CURL_ERROR_SIZE 256
typedef void CURL;
typedef enum { CURLE_OK = 0 } CURLcode;
typedef enum {
  CURLOPT_URL=10002, CURLOPT_WRITEFUNCTION=20011, CURLOPT_WRITEDATA=10001,
  CURLOPT_ERRORBUFFER=10010, CURLOPT_TIMEOUT=13, CURLOPT_CONNECTTIMEOUT=78,
  CURLOPT_SSL_VERIFYPEER=64, CURLOPT_SSL_VERIFYHOST=81, CURLOPT_FOLLOWLOCATION=52,
  CURLOPT_MAXREDIRS=68, CURLOPT_USERAGENT=10018, CURLOPT_NOSIGNAL=99
} CURLoption;
typedef enum { CURLINFO_RESPONSE_CODE = 2097154 } CURLINFO;
CURL* curl_easy_init();
CURLcode curl_easy_setopt(CURL*, CURLoption, ...);
CURLcode curl_easy_perform(CURL*);
CURLcode curl_easy_getinfo(CURL*, CURLINFO, ...);
void curl_easy_cleanup(CURL*);
const char* curl_easy_strerror(CURLcode);
