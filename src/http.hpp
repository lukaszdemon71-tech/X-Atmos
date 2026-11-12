#pragma once
#include <string>

namespace af::http {

// Blocking HTTPS GET. Called only from the weather worker thread, never from
// a flight loop or a browser callback. Returns false on any transport error;
// `error` then carries a short reason fit to show in the UI status line.
bool get(const std::string& url, std::string& body, std::string& error);

} // namespace af::http
