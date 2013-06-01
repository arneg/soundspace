#ifndef JSON_BUILDER_H
#define JSON_BUILDER_H
#include <string>

class JSONBuilder {
public:
    JSONBuilder() {}
    std::string buf;

    void put(std::string & s);
    void put(const char * s);
    void put(const char * s, size_t n);
    void add(const char * s, size_t size);

    void add(const char * s);
    void add(const std::string & s);
    void add(const unsigned int n);
    void reserve(size_t n);
};

#endif
