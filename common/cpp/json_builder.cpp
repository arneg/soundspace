#include "json_builder.h"
#include <cstdio>
#include "string.h"
#include <boost/lexical_cast.hpp>

void JSONBuilder::put(std::string & s) { buf.append(s); }
void JSONBuilder::put(const char * s) { buf.append(s); }
void JSONBuilder::put(const char * s, size_t n) { buf.append(s, n); }

void JSONBuilder::add(const char * s, size_t size) {
    size_t start = 0;
    char b[20];
    buf.append("\"");
    for (size_t i = 0; i < size; i++) {
	unsigned char c = s[i];
	if (c <= 0x1f || c == '\\' || c == '"') {
	    if (start < i)
		buf.append(s + start, i-start);

	    switch (s[i]) {
	    case '\b': buf.append("\\b"); break;
	    case '\f': buf.append("\\f"); break;
	    case '\n': buf.append("\\n"); break;
	    case '\r': buf.append("\\r"); break;
	    case '\t': buf.append("\\t"); break;
	    case '\\': buf.append("\\\\"); break;
	    case '"': buf.append("\\\""); break;
	    default:
		std::sprintf(b, "\\u%04u", (unsigned int)c);
		buf.append(b);
	    }

	    start = i+1;
	}
    }

    if (start < size) {
	buf.append(s+start, size-start);
    }
    buf.append("\"");
}

void JSONBuilder::add(const char * s) {
    add(s, strlen(s));
}

void JSONBuilder::add(const std::string & s) {
    add(s.c_str(), s.size());
}

void JSONBuilder::add(const unsigned int n) {
    buf += boost::lexical_cast<std::string>(n);
}

void JSONBuilder::reserve(size_t n) {
}
