#include "interpol.h"
#include "json_builder.h"
#include <stdlib.h>
#include <string>
#include <string.h>
#include <fstream>

// lets be a little conservative here
const size_t INBUF_BLOCKSIZE = 1024;

void Interpol::Err(int code, const char * s) {
    if (err) {
	err(code, s);
    }
}

void Interpol::eval(std::string & file) {
    eval(file.c_str());
}

void Interpol::eval(const char * file) {
    std::ifstream script;
    script.open(file);

    if (script.fail()) {
	std::cerr << "could not open script file " << file << std::endl;
    } else {
	eval(script);
	std::cerr << "running script " << file << std::endl;
    }
    script.close();
}

void Interpol::eval(std::istream & script) {
    Interpol tcomm = Interpol(name, cb, script, out);
    tcomm.seperator = '\n';
    tcomm.read();
}

void Interpol::handle_message(size_t pos, size_t end) {
    Json::Reader r;
    Json::Value root;

/*
    std::cerr << "handle_message(" << pos << ", " << end << ")" << std::endl;
    std::cerr << "parsing " << end-pos << " bytes from "
	      << (void*)inbuf << std::endl;
    std::cerr << "parsing '" << std::string(inbuf+pos, end-pos) << std::endl;
*/

    if (!r.parse(inbuf+pos, inbuf+end, root, false)) {
	std::cerr << "Parsing error:\n" << r.getFormatedErrorMessages() << std::endl;
	send_error("bad json");
	return;
    }

    if (!root.isObject()) {
	send_error("bad input");
	std::cerr << "Expecting object format" << std::endl;
	return;
    }

    try {
	if (root.isMember("cmd")) {
	    EXPECT_MAP::iterator item;
	    const char * cmd = root["cmd"].asCString();
	    item = expected.find(cmd);
	    if (item != expected.end()) {
		try {
		    item->second(root);
		} catch (...) {
		    std::cerr << "exception in call to expected callback"
			      << std::endl;
		}
		expected.erase(item);
		return;
	    }
	}
	cb(root);
    } catch (...) {
	send_error("generic");
    }
}

void Interpol::read() {
    std::string s;

    /*
     * This is a bit not quite enough. getline might still block
     * in case we have an incomplete json packet waiting
     */
    do {
	if (inbuf_capacity - inbuf_size < INBUF_BLOCKSIZE) {
	    inbuf = (char*)realloc(inbuf, inbuf_capacity + INBUF_BLOCKSIZE);
	    if (!inbuf) {
		Err(1, "OUT OF MEMORY");
	    }
	    inbuf_capacity += INBUF_BLOCKSIZE;
	}

	in.getline(inbuf+inbuf_size, inbuf_capacity - inbuf_size, seperator);
	inbuf_size += in.gcount();

	if (!in.gcount()) Err(0, "FOO");

	if (in.bad()) goto bad;
	if (in.fail()) {
	    in.clear();
	    continue;
	}

	// subtract the seperator
	if (!in.eof()) inbuf_size--;

	if (inbuf_size) {
	    try {
		handle_message(0, inbuf_size);
	    } catch (...) {
		std::cerr << "some unknown error in handle_message" << std::endl;
		send_error("generic");
	    }
	}

	if (in.eof()) {
	    // end of file. shutdown
bad:

	    Err(0, "end of file");
	} else if (!inbuf_size) break;
	inbuf_size = 0;
    } while (in.rdbuf()->in_avail());
}

void Interpol::expect_command(const char * cmd, InterpolCallback cb) {
    expected.insert(std::pair<const char *, InterpolCallback>(cmd, cb));
}

/* 
 * message handling
 */
void Interpol::send_error(const char * s, size_t n) {
    JSONBuilder buf;
    buf.add(s, n);
    (out << "{  \"src\" : \"" << name << "\", \"cmd\" : \"error\", \"error\" : " << buf.buf << " }").put(seperator);
    out.flush();
}

void Interpol::send_error(const char * s) {
    send_error(s, strlen(s));
}

void Interpol::send_error(std::string & s) {
    send_error(s.c_str(), s.size());
}

void Interpol::send_error(int c) {
    (out << "{  \"src\" : \"" << name << "\", \"cmd\" : \"error\", \"error\" : " << c << " }").put(seperator);
    out.flush();
}

void Interpol::send_command(const char * s) {
    (out << "{  \"src\" : \"" << name << "\", \"cmd\" : \"" << s << "\" }").put(seperator);
    out.flush();
}

void Interpol::send_data(std::string & s) {
    (out << "{ \"src\" : \"" << name << "\", \"cmd\" : \"data\", \"data\" : " << s << " }").put(seperator);
    out.flush();
}
