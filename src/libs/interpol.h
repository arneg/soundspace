#ifndef INTERPOL_H
#define INTERPOL_H

#include "json_builder.h"
#include <iostream>
#include <json/value.h>
#include <json/reader.h>
#include <json/writer.h>

typedef void (*InterpolCallback)(Json::Value&);
typedef void (*InterpolErrorCallback)(int, const char*);
typedef std::map<const char *, InterpolCallback> EXPECT_MAP;

class Interpol {
    std::istream & in;
    std::ostream & out;
    size_t inbuf_size, inbuf_capacity;
    char * inbuf;
    InterpolCallback cb;
    EXPECT_MAP expected;

    void Err(int, const char *);
    void handle_message(size_t, size_t);

public:
    void read();
    InterpolErrorCallback err;
    const char * name;
    char seperator;

    Interpol(const char * _name, InterpolCallback _cb)
    : in(std::cin), out(std::cout), inbuf_size(0), inbuf_capacity(0),
      inbuf(NULL), cb(_cb), err(NULL), name(_name), seperator('\0')
    { }

    Interpol(const char * _name, InterpolCallback _cb, std::istream & _in,
	     std::ostream & _out) 
    : in(_in), out(_out), inbuf_size(0), inbuf_capacity(0),
      inbuf(NULL), cb(_cb), err(NULL), name(_name), seperator('\0')
    { }

    void send_error(const char *, size_t);
    void send_error(const char *);
    void send_error(std::string &);
    void send_error(int);
    void send_command(const char *);
    void send_data(std::string &);
    void expect_command(const char * cmd, InterpolCallback cb);
    void eval(std::istream &);
    void eval(std::string &);
    void eval(const char *);

    static void read_cb(int fd, short flags, void *obj) {
	((Interpol*)obj)->read();
    }
};
#endif
