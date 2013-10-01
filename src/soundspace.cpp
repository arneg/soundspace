#include "interpol.h"
#include <time.h>
#include <event.h>
#include <csignal>
#include <stdlib.h>
#include <fstream>
#include <math.h>
#include <unistd.h>
#include <list>
#include <cmath>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>

#include "al.h"
#include "alc.h"

void interpol_callback(Json::Value&);

Interpol comm = Interpol("soundspace", interpol_callback);
Json::Value config;

static inline const char * err_name(ALenum err) {
    switch (err) {
#define CASE(name)  case name : return #name;
    CASE(AL_INVALID_NAME)
    CASE(AL_INVALID_ENUM)
    CASE(AL_INVALID_VALUE)
    CASE(AL_INVALID_OPERATION)
    CASE(AL_OUT_OF_MEMORY)
    CASE(AL_NO_ERROR)
    CASE(AL_PLAYING)
    CASE(AL_PAUSED)
    CASE(AL_STOPPED)
    CASE(AL_INITIAL)
    CASE(AL_FORMAT_MONO8)
    CASE(AL_FORMAT_MONO16)
    CASE(AL_FORMAT_STEREO8)
    CASE(AL_FORMAT_STEREO16)
    }
    return "UNKNOWN";
}

class RiffException : public std::exception {
    std::string msg;
public:
    RiffException(const char * file, const char * s) {
	msg.append(s);
	msg.append(" '");
	msg.append(file);
	msg.append("'");
    }

    virtual const char * what() const throw() { return msg.c_str(); }

    ~RiffException() throw() {}
};

class OpenALException : public std::exception {
    std::string msg;
public:
    OpenALException(const char * file, int line, const char * src, ALenum al_error) : msg("") {
	msg.append("Error ");
	msg.append(err_name(al_error));
	msg.append(" in '");
	msg.append(file);
	msg.append("' at line ");
	msg += boost::lexical_cast<std::string>(line);
	msg.append(" using: ");
	msg.append(src);
    }

    virtual const char * what() const throw() { return msg.c_str(); }

    ~OpenALException() throw() {}
};

#define CHECK(x)    do {						\
    x;									\
    ALenum err = alGetError();						\
    if (err != AL_NO_ERROR) {						\
	throw OpenALException(__FILE__, __LINE__, #x, err);		\
    }									\
} while (0)

#undef CASE

static inline void Json2AL(Json::Value & v, ALfloat a[], unsigned int n = 3) {
    if (!v.isArray()) throw std::logic_error("Bad argument 1 to Json2fv. Expected Array.");
    for (unsigned int i = 0; i < n; i++) {
	if (!v[i].isNumeric()) throw std::logic_error("Bad element in vector. Expected numeric");
	a[i] = (ALfloat)v[i].asDouble();
    }
}

static inline void Json2AL(Json::Value & v, ALfloat & f) {
    if (!v.isNumeric()) throw std::logic_error("Bad argument 1 to Json2f. Expected numeric");
    f = (ALfloat)v.asDouble();
}

static inline void Json2AL(Json::Value & v, ALint & i) {
    if (!v.isNumeric()) throw std::logic_error("Bad argument 1 to Json2f. Expected numeric");
    i = (ALint)v.asInt();
}

static inline void Json2AL(Json::Value & v, bool & b) {
    if (!v.isBool()) throw std::logic_error("Bad argument 1 to Json2f. Expected true or false");
    b = (ALfloat)v.asBool();
}

const int NBUFFERS = 3;
const int BUFFER_INTERVAL = 1000;

class Listener {
public:
#define fvFUN(name, FLAG)    ALfloat name ## value[3];			    \
    ALfloat * name (ALfloat v[]) {					    \
	name ## value[0] = v[0];					    \
	name ## value[1] = v[1];					    \
	name ## value[2] = v[2];					    \
	CHECK(alListenerfv(FLAG, name ## value));			    \
	return name ## value;						    \
    }									    \
    ALfloat * name (ALfloat x, ALfloat y, ALfloat z) {			    \
	name ## value[0] = x;						    \
	name ## value[1] = y;						    \
	name ## value[2] = z;						    \
	CHECK(alListenerfv(FLAG, name ## value));			    \
	return name ## value;						    \
    }									    \
    ALfloat * name (Json::Value & v) {					    \
	Json2AL(v, name ## value);					    \
	CHECK(alListenerfv(FLAG, name ## value));			    \
	return name ## value;						    \
    }									    \
    ALfloat * name () {							    \
	return name ## value;						    \
    }
    fvFUN(position, AL_POSITION);
    fvFUN(velocity, AL_VELOCITY);

    void orientation(Json::Value & v) {
	ALfloat fv[6];
	Json2AL(v, fv, 6);
	orientation(fv);
    }

    void orientation(const ALfloat v[6]) {
	alListenerfv(AL_ORIENTATION, v);
    }

    Listener() {
#if 0
	static const ALfloat default_orientation[] = {
	    1.0, 0.0, 0.0,
	    0.0, 1.0, 0.0
	};
	orientation(default_orientation);
#endif
	position(0.0, 0.0, 0.0);
	velocity(0.0, 0.0, 0.0);
    }
};

// forward definition
class Device;
class Buffer;
class Source;

class Buffer {
    struct chunk {
	char type[4];
	unsigned int length;
    };
    struct riff_header {
	char riff[4];
	unsigned int length;
	char wave[4];
    };
    struct wave_format {
	char fmt[4];
	unsigned int len;
	unsigned short tag;
	unsigned short channels;
	unsigned int sample_rate;
	unsigned int bytes_per_second;
	unsigned short align;
	unsigned short bits_per_sample;
    };
    struct pcm_header {
	char data[4];
	unsigned int length;
    };
#define HEADER_SIZE (long)(sizeof(struct riff_header) + sizeof(struct wave_format) + sizeof(struct pcm_header))
public:
    ALuint id[NBUFFERS];
    void * data;
    int fd;
    struct stat st;
    size_t offset;
    size_t chunk_size;
    ALenum format;
    ALuint frequency;
    unsigned long interval;

    Buffer() {
	data = NULL;
	fd = -1;
	CHECK(alGenBuffers(NBUFFERS, id));
    }

    void fromFile(const char * f) {
	data = NULL;

	fd = open(f, O_RDONLY);

	if (fd == -1)
	    throw RiffException(f, "could not open file");

	if (fstat(fd, &st) == -1)
	    throw RiffException(f, "could not stat file");

	if (!S_ISREG (st.st_mode))
	    throw RiffException(f, "not a regular file");

#ifdef DEBUG
	std::cerr << "open file " << f << " with size " << st.st_size << std::endl;
#endif

	data = mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);

#ifdef DEBUG
	std::cerr << "mapped " << f << " to " << data << std::endl;
#endif

	if (data == MAP_FAILED) {
	    close(fd);
	    throw RiffException(f, "could not mmap ");
	}

	madvise(data, st.st_size, MADV_SEQUENTIAL);

	{
	    const struct riff_header * rhead;
	    const struct wave_format * whead;
	    const struct pcm_header * phead;
	    const struct chunk * chead;
	    const char * buf = (const char *) data;

	    if (st.st_size < HEADER_SIZE) {
		throw RiffException(f, "file smaller than wave header ");
	    }

	    rhead = (const struct riff_header*)buf;
	    buf += sizeof(struct riff_header);
	    whead = (const struct wave_format*)buf;
	    buf += sizeof(struct wave_format);

	    chead = (const struct chunk*)buf;

	    while (strncmp(chead->type, "data", 4)) {
		buf += sizeof(struct chunk) + chead->length;
		chead = (const struct chunk*)buf;
	    }

	    phead = (const struct pcm_header*)buf;
	    buf += sizeof(struct pcm_header);

	    if (strncmp(rhead->riff, "RIFF", 4) || strncmp(rhead->wave, "WAVE", 4)) {
		throw RiffException(f, "bad riff wave header in");
	    }

	    if (rhead->length + 8 != st.st_size)
		throw RiffException(f, "corrupt header in ");

	    if (strncmp(whead->fmt, "fmt ", 4) || whead->len != 16)
		throw RiffException(f, "bad wave format in");

#ifdef DEBUG
	    std::cerr << "bits: " << whead->bits_per_sample
		      << ", channels: " << whead->channels << std::endl;
#endif
	    if (whead->channels == 1) {
		format = (whead->bits_per_sample == 8) ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
	    } else if (whead->channels == 2) {
		std::cerr << "Warning: '" << f << "' contains stereo data and"
			     " will be played without spatialization." << std::endl;
		format = (whead->bits_per_sample == 8) ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
	    } else throw RiffException(f, "bad number of channels in");

	    frequency = (ALuint)whead->sample_rate;

	    if (strncmp(phead->data, "data", 4))
		throw RiffException(f, "bad pcm header in ");

	    offset = HEADER_SIZE;
	    chunk_size = whead->bytes_per_second / 1000 * BUFFER_INTERVAL;
	    interval = BUFFER_INTERVAL/2;

	    if (2*chunk_size > (st.st_size - HEADER_SIZE)) {
		chunk_size = (st.st_size - HEADER_SIZE) / 2;
		interval = chunk_size * 1000 / whead->bytes_per_second;
		interval /= 2;
	    }

	    if (chunk_size % 256) {
		chunk_size -= chunk_size % 256;
		chunk_size += 256;
	    }

#ifdef DEBUG
	    std::cerr << "buffering chunks of " << chunk_size << " bytes" << std::endl;
	    std::cerr << "using interval of " << interval << " ms" << std::endl;
#endif
	}
	CHECK(alGenBuffers(NBUFFERS, id));
#ifdef DEBUG
	std::cerr << "generated " << NBUFFERS << " buffer " << *id << std::endl;
#endif
    }

    void * buf() {
	return (char*)data + offset;
    }

    size_t left() {
	return st.st_size - offset;
    }

    void reset() {
	offset = HEADER_SIZE;
    }

    int feed_one(Source & source, ALuint buffer, size_t len);
    int feed_start(Source & source);
    int feed_more(Source & source);

    Buffer(const char * f) {
	fromFile(f);
    }

    Buffer(std::string & file) {
	fromFile(file.c_str());
    }

    Buffer(Json::Value & s) {
	if (!s.isString())
	    throw std::logic_error("Bad argument one to Buffer(). Expected string.");
	fromFile(s.asCString());
    }

    /*
    void add_data(const char * file) {
	ALenum format;
	ALvoid * data;
	ALsizei size, freq;
	ALboolean succ;
	alutLoadWAVFile((ALbyte*)file,&format,&data,&size,&freq,&succ);
	if (format != AL_FORMAT_MONO8 && format != AL_FORMAT_MONO16) {

	}
	alBufferData(id,format,data,size,freq);
	alutUnloadWAV(format,data,size,freq);
    }
    */

    ~Buffer() {
#ifdef DEBUG
	std::cerr << "deleting buffer " << id << " with data " << data << std::endl;
#endif
	CHECK(alDeleteBuffers(NBUFFERS, id));
	munmap(data, st.st_size);
	close(fd);
    }

};

class SourceSettings {
    void get(ALenum pname, ALint & i) {
	CHECK(alGetSourcei(id, pname, & i));
    }

    void get(ALenum pname, ALfloat & f) {
	CHECK(alGetSourcef(id, pname, & f));
    }

    void get(ALenum pname, ALfloat f[]) {
	CHECK(alGetSourcefv(id, pname, f));
    }

    void set(ALenum pname, ALint i) {
	CHECK(alSourcei(id, pname, i));
    }

    void set(ALenum pname, ALfloat f) {
	CHECK(alSourcef(id, pname, f));
    }

    void set(ALenum pname, ALfloat f[]) {
	CHECK(alSourcefv(id, pname, f));
    }

    void set(ALenum pname, bool b) {
	const ALint i = (ALint)((b && AL_TRUE) || AL_FALSE);
	set(pname, i);
    }

    void get(ALenum pname, bool & b) {
	ALint i;
	get(pname, i);
	b = (i == AL_TRUE);
    }
public:
    ALuint id;
#define FUN(name, FLAG)	typeof(name ## _value) name () {		    \
	get(FLAG, name ## _value);					    \
	return name ## _value;						    \
    }									    \
    typeof(name ## _value) name (typeof(name ## _value) v) {		    \
	set(FLAG, name ## _value = v);					    \
	return name ## _value;						    \
    }									    \
    typeof(name ## _value) name (Json::Value & v) {			    \
	Json2AL(v, name ## _value);					    \
	set(FLAG, name ## _value);					    \
	return name ## _value;						    \
    }

#undef fvFUN
#define fvFUN(name, FLAG)    ALfloat name ## _value[3];			    \
    ALfloat * name () {							    \
	get(FLAG, name ## _value);					    \
	return name ## _value;						    \
    }									    \
    ALfloat * name (Json::Value & v) {					    \
	Json2AL(v, name ## _value);					    \
	set(FLAG, name ## _value);					    \
	return name ## _value;						    \
    }									    \
    ALfloat * name (ALfloat x, ALfloat y, ALfloat z) {			    \
	name ## _value[0] = x;						    \
	name ## _value[1] = y;						    \
	name ## _value[2] = z;						    \
	set(FLAG, name ## _value);					    \
	return name ## _value;						    \
    }									    \
    ALfloat * name (ALfloat v[]) {					    \
	name ## _value[0] = v[0];					    \
	name ## _value[1] = v[1];					    \
	name ## _value[2] = v[2];					    \
	set(FLAG, name ## _value);					    \
	return name ## _value;						    \
    }

#define fFUN(name, FLAG)    ALfloat name ## _value;  \
    FUN(name, FLAG)

#define iFUN(name, FLAG)    ALint name ## _value;    \
    FUN(name, FLAG)

#define bFUN(name, FLAG)    bool name ## _value;    \
    FUN(name, FLAG)
    fvFUN(position, AL_POSITION);
    fvFUN(velocity, AL_VELOCITY);
    fFUN(pitch, AL_PITCH);
    fFUN(gain, AL_GAIN);
    fFUN(min_gain, AL_MIN_GAIN);
    fFUN(max_gain, AL_MAX_GAIN);
    iFUN(state, AL_SOURCE_STATE);
    iFUN(buffers_processed, AL_BUFFERS_PROCESSED);

    SourceSettings() {}
    SourceSettings(ALuint _id) : id(_id) {
	update();
#ifdef DEBUG
	std::cerr << "copied source " << id << std::endl;
#endif
    }

    ~SourceSettings() {
#ifdef DEBUG
	std::cerr << "deleted copied source " << id << std::endl;
#endif
    }

    void update() {
	position();
	velocity();
	pitch();
	gain();
	min_gain();
	max_gain();
    }

    void apply() {
	position(position_value);
	velocity(velocity_value);
	pitch(pitch_value);
	gain(gain_value);
	min_gain(min_gain_value);
	max_gain(max_gain_value);
    }
};

class Source : public SourceSettings {
    struct event timer_ev;
public:
    Buffer * buffer;
    Device * dev;

    bool _loop;
    bool loop() {
	return _loop;
    }

    bool loop(bool v) {
	return _loop = v;
    }

    bool loop(Json::Value & v) {
	Json2AL(v, _loop);
	return _loop;
    }

    void add(Buffer * buf) {
	if (buffer) {
	    std::cerr << "sources can currently only hold one buffer."
			 " replacing old one." << std::endl;
	    delete(buffer);
	}
	buffer = buf;
#if 0
	std::cerr << "adding buffer " << buf->id << std::endl;
#endif
    }

    bool paused;

    void Play() {
	if (!buffer) return;

	if (paused) {
	    paused = false;
	    timer_continue();
	} else {
	    Stop();
	    timer_start();
	}
	CHECK(alSourcePlay(id));
    }


    ALenum State() {
	ALenum state;
	CHECK(alGetSourcei(id, AL_SOURCE_STATE, &state));
	return state;
    }

    void Stop() {
	if (!buffer) return;
	timer_stop();
	buffer->reset();
	CHECK(alSourceStop(id));
	paused = false;

	ALuint num = buffers_processed();

	while (num--) unqueue_buffer();
    }

    void Rewind() {
	// TODO: this is certainly broken
	if (!buffer) return;
	buffer->reset();
	CHECK(alSourceRewind(id));
	paused = false;
    }

    void Pause() {
	paused = true;
	timer_stop();
	CHECK(alSourcePause(id));
    }

    Source(Device * _dev) : dev(_dev) {
	buffer = NULL;
	paused = false;
	timer_set = false;
	loop(false);
	CHECK(alGenSources(1, &id));
	evtimer_set(&timer_ev, timer_callback, this);
#ifdef DEBUG
	std::cerr << "created source " << id << std::endl;
#endif
	update();
    }

    SourceSettings * copy() {
	SourceSettings * t = new SourceSettings(id);
	return t;
    }

    ~Source() {
#ifdef DEBUG
	std::cerr << ">> deletint source " << id << std::endl;
#endif
	Stop();
	if (buffer) delete(buffer);
	alDeleteSources(1, &id);
#ifdef DEBUG
	std::cerr << "<< deleted source " << id << std::endl;
#endif
    }

    void enqueue_buffer(ALuint buf_id) {
	CHECK(alSourceQueueBuffers(id, 1, &buf_id));
    }

    ALuint unqueue_buffer() {
	ALuint buf_id;
	CHECK(alSourceUnqueueBuffers(id, 1, &buf_id));
	return buf_id;
    }

    bool timer_set;

    void timer_continue() {
	const struct timeval sound_interval = { 0, buffer->interval*1000 };
	if (!timer_set) {
	    evtimer_add(&timer_ev, &sound_interval);
	    timer_set = true;
	}
    }

    void timer_start() {
	if (buffer && buffer->feed_start(*this))
	    timer_continue();
    }

    void timer_stop() {
	if (timer_set) {
	    evtimer_del(&timer_ev);
	    timer_set = false;
	}
    }

    void run() {
	timer_set = false;
	if (buffer && buffer->feed_more(*this)) {
	    timer_continue();
	}
    }

    static void timer_callback(int, short int, void * o) {
	try {
	    ((Source*)o)->run();
	} catch (const std::exception & e) {
	    std::cerr << "error: " << e.what() << std::endl;
	}
    }

};

int Buffer::feed_one(Source & source, ALuint buffer, size_t len) {

    if (!left()) return 0;

    if (left() < len) len = left();

    CHECK(alBufferData(buffer, format, buf(), len, frequency));
    offset += len;

    source.enqueue_buffer(buffer);

    return 1;
}

int Buffer::feed_start(Source & source) {
    ALuint i;

    for (i = 0; i < NBUFFERS; i++) {
	if (!left()) { 
	    if (source.loop()) reset();
	    else return 0;
	}
	feed_one(source, id[i], chunk_size);
    }
    return 1;
}

int Buffer::feed_more(Source & source) {
    ALuint num = source.buffers_processed();
#ifdef DEBUG
    std::cerr << "feeding " << num << " chunks" << std::endl;
#endif
    while (num--) {
#ifdef DEBUG
	std::cerr << "source.loop(): " << source.loop() << std::endl;
#endif
	if (!left() && source.loop()) reset();
	if (!feed_one(source, source.unqueue_buffer(), chunk_size)) return 0;
    }

    if (source.State() != AL_PLAYING) {
	std::cerr << "Buffer underrun." << std::endl;
	source.Play();
    }

    return 1;
}

class Animation {
public:
    struct timespec start, end, now;
    double length;
    Source * source;

    Animation(Source * s, double l) {
	source = s;
	length = l;
	clock_gettime(CLOCK_MONOTONIC, &start);
	end.tv_sec = start.tv_sec + (time_t)l;
	end.tv_nsec = ((l-(double)(long)(l)) * 1E9);
#if 0
	std::cerr << "new animation for Source(" << source->id << ")"
		  << std::endl;
#endif
    }

    inline void update() {
	clock_gettime(CLOCK_MONOTONIC, &now);
    }

    double p() {
	return (double)((now.tv_sec - start.tv_sec)
		+ (now.tv_nsec - start.tv_nsec)*1E-9)/length;
    }

    virtual const char * toString() {
	return "Animation";
    }

    virtual void step() { }
    virtual bool done() {
	return p() >= 1.0;
    }
};

class FadeGain : public Animation {
    ALfloat old_gain, new_gain;
public:
    FadeGain(Source * s, double l, ALfloat _gain) : Animation(s, l) {
	old_gain = s->gain();
	new_gain = _gain;
#if DEBUG
	std::cerr << "animating between " << old_gain << " and " << new_gain
		  << std::endl;
#endif
    }

    void step() {
	double t = p();

	if (t >= 1.0) {
	    source->gain(new_gain);
	} else {
	    source->gain(old_gain + (new_gain-old_gain)*t);
	}
    }

    const char * toString() {
	return "FadeGain";
    }
};

// since positions are multi dimensional, we dont specify the target
// distance but rather the speed per second. like this we can interleave
// animations to create other continuos transformations.
//
// this uses linear interpolation, so its important to get the infitisimal
// transformation right to then do a linear approximation. this basically
// numerically integrates the derivative. this can be a problem for fast
// non linear transformations.
class Scale : public Animation {
    ALfloat speed;
    double t0;
public:
    // scale by speed / s  for l seconds
    Scale(Source * s, double l, ALfloat speed) : Animation(s, l), t0(0.0) {
	Scale::speed = speed * l;
    }

    void step() {
	ALfloat * v = source->position();
	double r = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
	double t = (p() - t0) * speed;
	t = (r+t)/r;
	source->position(v[0]*t, v[1]*t, v[2]*t);
	t0 = p();
    }

    const char * toString() {
	return "Scale";
    }
};

static double PI = 2 * acos(0.0);

class Rotate : public Animation {
    ALfloat speed;
    double t0;
public:

    // rotate at speed rotations per minute clockwise for l seconds
    Rotate(Source * s, double l, ALfloat speed) : Animation(s, l), t0(0.0) {
	// speed in radian
	Rotate::speed = 2.0 * PI * speed * l;
    }

    void step() {
	// as
	//  a  0  b
	//  0  0  0
	//  c  0  d
	ALfloat a, b, c, d;
	ALfloat * v = source->position();
	double t = (p() - t0) * speed;
	a = d = cos(t);
	b = sin(t);
	c = -b;

	source->position(v[0]*a + v[2]*b, v[1], v[0]*c + v[2]*d);
	t0 = p();
    }

    const char * toString() {
	return "Rotate";
    }
};

const struct timeval animation_interval = { 0, 20*1000 };
class Animator {
    // animation interval is 20 ms
    struct event timer_ev;
    std::list<Animation*> l;

public:
    void add(Animation * a) {
	if (l.size() == 0) {
	    evtimer_add(&timer_ev, &animation_interval);
	}
	l.push_back(a);
    }

    static bool is_done(Animation * a) {
	return a->done();
    }

    void run() {
	std::list<Animation*>::iterator it;
	for (it = l.begin(); it != l.end();) {
	    Animation * a = * it;

	    a->update();
	    a->step();

	    if (a->done()) {
		it = l.erase(it);
		delete a;
		continue;
	    }
	    it++;
	}

	if (l.size()) {
	    evtimer_add(&timer_ev, &animation_interval);
	}
    }

    void removeSource(Source* s) {
	std::list<Animation*>::iterator it;
	for (it = l.begin(); it != l.end();) {
	    Animation * a = * it;

	    if (a->source == s) {
		std::cerr << "removing animation " << a->toString() << std::endl;
		it = l.erase(it);
		delete a;
		continue;
	    }
	    it++;
	}
    }

    void clear() {
	if (l.size()) {
	    std::list<Animation*>::iterator it;
	    for (it = l.begin(); it != l.end(); it++) {
		delete (*it);
	    }
	    evtimer_del(&timer_ev);
	    l.clear();
	}
    }

    static void animation_callback(int, short int, void * o) {
	try {
	    ((Animator*)o)->run();
	} catch (const std::exception & e) {
	    std::cerr << "error in animation : '"
		      << e.what() << "'" << std::endl;
	}
    }

    Animator() {
	evtimer_set(&timer_ev, animation_callback, this);
    }
};

class Device {
public:
    std::vector<Source*> sources;
    std::vector<SourceSettings*> snapshot;
    std::map<std::string,Source*> name2source;
    Listener * l;
    Animator animator;
    ALCdevice * dev;
    ALCcontext * ctx;

    Device(const char * dev_name = NULL) {
	dev = alcOpenDevice(dev_name);
	if (!dev) {
	    const char * devices = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
	    size_t len;

	    std::cerr << "available devices:" << std::endl;
	    while ((len = strlen(devices)) > 0) {
		std::string name = std::string(devices, len);
		std::cerr << "\t" << name << std::endl;
		devices += len;
	    }

	    throw std::logic_error("Could not open device.");
	}
	ctx = alcCreateContext(dev, NULL);
	if (!ctx) {
	    throw std::logic_error("Could not create context.");
	}
	alcMakeContextCurrent(ctx);
	l = new Listener();
    }

    void addName(std::string name, Source * s) {
	if (name2source.find(name) != name2source.end()) {
	    std::cerr << "adding source with same name '"
		      << name << "'. consider "
		      << "using a name field in your configuration"
		      << std::endl;
	}
	name2source.insert(std::pair<std::string, Source*>(name, s));
    }

    void makeSnapshot() {
	size_t i;
	if (snapshot.size()) {
	    for (i = 0; i < snapshot.size(); i++) {
		delete snapshot[i];
	    }
	    snapshot.clear();
	}

	for (i = 0; i < sources.size(); i++) {
	    snapshot.push_back(sources[i]->copy());
	}
    }

    void applySnapshot() {
	size_t i;
	if (sources.size() != snapshot.size()) {
	    throw std::logic_error("mismatching snapshot");
	}
	for (i = 0; i < snapshot.size(); i++) {
	    snapshot[i]->apply();
	    sources[i]->update();
	}
    }

    Source * getSource() {
	Source * source = new Source(this);
	sources.push_back(source);
	return source;
    }

    Source * getSource(size_t n) {
	return sources[n];
    }

    Source * getSource(const std::string & s) {
	std::map<std::string,Source*>::iterator it = name2source.find(s);
	if (it == name2source.end())
	    throw std::logic_error("Could not find source by name.");
	return it->second;
    }

    Source * getSource(Json::Value & v) {
	if (v.isNumeric()) {
	    return getSource((size_t)v.asUInt());
	} else if (v.isString()) {
	    const std::string s = v.asString();
	    return getSource(s);
	} else
	    throw std::logic_error("Bad argument 1 to getSource(). Expected uint or string.");
    }

    void removeSource(Source * s) {
	ALuint id = s->id;
	std::vector<SourceSettings*>::reverse_iterator it2;
	std::vector<Source*>::reverse_iterator it3;

	for (it2 = snapshot.rbegin(); it2 != snapshot.rend(); it2++) {

	    if ((*it2)->id == id) {
		delete(*it2);
		snapshot.erase(--it2.base());
		break;
	    }
	}

	for (it3 = sources.rbegin(); it3 != sources.rend(); it3++) {
	    if ((*it3)->id == id) {
		delete(*it3);
		sources.erase(--it3.base());
		break;
	    }
	}

	std::map<std::string,Source*>::iterator it;

	for (it = name2source.begin(); it != name2source.end(); it++) {
	    if (it->second == s) {
		name2source.erase(it);
		break;
	    }
	}
    }

    void removeSources(Json::Value & ids) {
	std::vector<Source*> a;
	Ids2Sources(ids, a);
	std::vector<Source*>::iterator it;

	for (it = a.begin(); it != a.end(); it++) {
	    Source * s = *it;
	    animator.removeSource(s);
	    removeSource(s);
	}
    }

    void checkSource(size_t id) throw(const char *) {
	if (id > sources.size())
	    throw std::logic_error("Source ID is out of range.");
    }

    inline void Json2Ids(Json::Value & ids, std::vector<ALuint> & a) {
	size_t n, i;
	if (ids.isArray() && (n = ids.size())) {
	    a.reserve((size_t)n);
	    for (i = 0; i < n; i++) {
		a.push_back(getSource(ids[i])->id);
	    }
	} else if (ids.isBool()) {
	    bool t = ids.asBool();
	    if (t) {
		for (i = 0; i < sources.size(); i++) {
		    a.push_back(sources[i]->id);
		}
	    } else throw std::logic_error("bad argument one to Json2Ids. Expected string|int|array|true");
	} else a.push_back(getSource(ids)->id);
    }

    inline void Ids2Sources(Json::Value & ids, std::vector<Source*> & a) {
	size_t n, i;
	if (ids.isArray() && (n = ids.size())) {
	    a.reserve((size_t)n);
	    for (i = 0; i < n; i++) {
		a.push_back(getSource(ids[i]));
	    }
	} else if (ids.isBool()) {
	    bool t = ids.asBool();
	    if (t) {
		a = sources;
	    } else throw std::logic_error("bad argument one to Json2Ids. Expected string|int|array|true");
	} else a.push_back(getSource(ids));
    }

#define DEVICE_ACTION(name) void name (Json::Value & ids)		\
    {									\
	std::vector<Source*> a;						\
	std::vector<Source*>::iterator it;				\
	Ids2Sources(ids, a);						\
									\
	for (it = a.begin(); it != a.end(); it++) {			\
	    (*it)->name();						\
	}								\
    }
    DEVICE_ACTION(Play)
    DEVICE_ACTION(Pause)
    DEVICE_ACTION(Stop)
    DEVICE_ACTION(Rewind)

#undef FUN
#define FUN(name, type)							\
  void name (Json::Value & ids, Json::Value & f)			\
    {									\
	std::vector<Source*> a;						\
	type _f;							\
	Ids2Sources(ids, a);						\
	Json2AL(f, _f);							\
	size_t i;							\
	for (i = 0; i < a.size(); i++) {				\
	    a[i]-> name (_f);						\
	}								\
    }

    // dont use this anywhere!
    typedef ALfloat ALfv[3];

    FUN(gain, ALfloat)
    FUN(pitch, ALfloat)
    FUN(loop, bool)
    FUN(position, ALfv)
    FUN(velocity, ALfv)


#define ANIMATE_f(name, CLASS)						\
  void name (Json::Value & ids, Json::Value & time, Json::Value & f)	\
    {									\
	std::vector<Source*> a;						\
	Ids2Sources(ids, a);						\
	ALfloat _f, _time;						\
	size_t i;							\
	Json2AL(f, _f);							\
	Json2AL(time, _time);						\
	for (i = 0; i < a.size(); i++) {				\
	    animator.add(new CLASS(a[i], (double)_time, _f));		\
	}								\
    }

    ANIMATE_f(Fade, FadeGain);
    ANIMATE_f(Scale, ::Scale);
    ANIMATE_f(Rotate, ::Rotate);

    std::vector<ALuint> paused;

    void StopAll() {
	for (size_t i = 0; i < sources.size(); i++) {
	    paused.push_back(sources[i]->id);
	}
	alSourceStopv(paused.size(), &(paused[0]));
	animator.clear();
    }

    void PauseAll() {
	paused.clear();
	for (size_t i = 0; i < sources.size(); i++) {
	    if (sources[i]->State() == AL_PLAYING) {
		paused.push_back(sources[i]->id);
	    }
	}
	alSourcePausev(paused.size(), &(paused[0]));
    }

    void ContinueAll() {
	alSourcePlayv(paused.size(), &(paused[0]));
	paused.clear();
    }


    ~Device() {
	std::vector<SourceSettings*>::iterator it1;
	std::vector<Source*>::iterator it2;

	for (it1 = snapshot.begin(); it1 != snapshot.end(); it1++) {
	    delete(*it1);
	}
	for (it2 = sources.begin(); it2 != sources.end(); it2++) {
	    delete(*it2);
	}
	delete(l);
	alcMakeContextCurrent(NULL);
	alcDestroyContext(ctx);
	if (alcCloseDevice(dev) != ALC_TRUE) {
	    std::cerr << "could not close device" << std::endl;
	}
    }
};

Device * dev = NULL;

std::string sound_path, script_path, config_path;

Source * sourceFromFile(std::string & file, std::string & name) {
    std::string path = sound_path + file;
    Buffer * buf = new Buffer(path);
    Source * s = dev->getSource();
    s->add(buf);
    dev->addName(name, s);
    return s;
}

Source * sourceFromFile(std::string & file) {
    return sourceFromFile(file, file);
}

#define CONFIG_SET(m, s, name)    do {				\
	if ((m).isMember(#name)) (s)-> name ((m)[#name]);	\
    } while (0)

Source * sourceFromJSON(Json::Value & sinfo) {
    Source * s = NULL;

    if (sinfo.isMember("file")) {
	std::string file = sinfo["file"].asString();
	if (sinfo.isMember("name")) {
	    std::string name = sinfo["name"].asString();
	    s = sourceFromFile(file, name);
	} else {
	    s = sourceFromFile(file);
	}
	CONFIG_SET(sinfo, s, position);
	CONFIG_SET(sinfo, s, velocity);
	CONFIG_SET(sinfo, s, gain);
	CONFIG_SET(sinfo, s, pitch);
	CONFIG_SET(sinfo, s, loop);
    } else {
	std::cerr << "file location missing" << std::endl;
    }

    return s;
}

__attribute__((noreturn))
void shutdown(int code) {
    comm.send_error("shutdown");
    try {
	if (dev) delete(dev);
    } catch (const std::exception & e) {
	std::cerr << "error on shutdown: " << e.what() << std::endl;
    }
    exit(code);
}

__attribute__((noreturn))
void shutdown(int code, const char * s) {
    std::cerr << "shutdown for REASON: " << s << std::endl;
    shutdown(code);
}

void read_config(std::ifstream & cfile) {
    Json::Reader r;
    Json::Value v;
    Json::Value::ArrayIndex n;

    r.parse(cfile, config, false);

    if (!!(v = config["device"]) && v.isString()) {
	dev = new Device(v.asCString());
    } else {
	dev = new Device();
    }

    if (!!(v = config["sources"]) && v.isArray() && (n = v.size()) > 0) {
	Json::Value::ArrayIndex i;

	std::cerr << "found " << n << " sources" << std::endl;

	if (config.isMember("path")) {
	    sound_path = config["path"].asString();
	}

	if (config.isMember("script_path")) {
	    script_path = config["script_path"].asString();
	}

	for (i = 0; i < n; i++) {
	    Json::Value sinfo = v[i];
	    sourceFromJSON(sinfo);
	}
    }

    if (config.isMember("listener")) {
	v = config["listener"];
	if (!v.isObject())
	    throw std::logic_error("bad configuration 'listener'. Expected object.");
	CONFIG_SET(v, dev->l, orientation);
	CONFIG_SET(v, dev->l, position);
	CONFIG_SET(v, dev->l, velocity);
    }
}

void interpol_callback(Json::Value & root) {
    try {
	if (root["cmd"] == "play") {
	    dev->Play(root["ids"]);
	} else if (root["cmd"] == "eval") {
	    if (!root["script"].isString()) {
		throw std::logic_error("bad script file. expected string.");
	    }
	    std::string file = script_path;
	    std::cerr << "script_path: " << script_path << std::endl;
	    file.append(root["script"].asCString());
	    comm.eval(file);
	} else if (root["cmd"] == "add_source") {
	    Source * s = sourceFromJSON(root);
	    if (s) dev->snapshot.push_back(s->copy());
	} else if (root["cmd"] == "remove_source") {
	    dev->removeSources(root["ids"]);
	} else if (root["cmd"] == "stop_audio") {
	    dev->Stop(root["ids"]);
	} else if (root["cmd"] == "reset_audio") {
	    dev->StopAll();
	    dev->applySnapshot();
	} else if (root["cmd"] == "stop_all") {
	    dev->StopAll();
	} else if (root["cmd"] == "pause") {
	    dev->Pause(root["ids"]);
	} else if (root["cmd"] == "rewind") {
	    dev->Rewind(root["ids"]);
	} else if (root["cmd"] == "position") {
	    if (root.isMember("ids"))
		dev->position(root["ids"], root["position"]);
	    else
		dev->getSource(root["id"])->position(root["position"]);
	} else if (root["cmd"] == "gain") {
	    if (root.isMember("ids"))
		dev->gain(root["ids"], root["gain"]);
	    else
		dev->getSource(root["id"])->gain(root["gain"]);
	} else if (root["cmd"] == "fade") {
	    dev->Fade(root["ids"], root["time"], root["gain"]);
	} else if (root["cmd"] == "scale") {
	    dev->Scale(root["ids"], root["time"], root["speed"]);
	} else if (root["cmd"] == "rotate") {
	    dev->Rotate(root["ids"], root["time"], root["speed"]);
	} else if (root["cmd"] == "pause_all") {
	    dev->PauseAll();
	} else if (root["cmd"] == "continue_all") {
	    dev->ContinueAll();
	} else if (root["cmd"] == "loop") {
	    dev->loop(root["ids"], root["loop"]);
	} else if (root["cmd"] == "die_audio") {
	    shutdown(1, "dying");
	}
    } catch (const std::exception & e) {
	std::cerr << "error in " << root["cmd"].asString() << ": '"
		  << e.what() << "'" << std::endl;
    }
}

namespace po = boost::program_options;

int main(int argc, char ** argv) {
    struct event ev;
    po::options_description desc("Available options");
    po::variables_map vm;
    std::ifstream cfile;
    std::vector<std::string> exec;
    std::string config_file;

    desc.add_options()
	("help,h", "print usage info")
	("config,c", po::value<std::string>(&config_file), "path to config file")
	("sound-path,s", po::value<std::string>(&sound_path)->default_value("./"), "sound file search path")
	("script-path", po::value<std::string>(&script_path)->default_value("./"), "script search path")
	("exec,e", po::value< std::vector<std::string> >(&exec), "execute a given script on startup")
	("stdin", "Use '\\n' instead of '\\0' as message delimiter.")
	;

    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
	std::cout << desc << std::endl;
	return 1;
    }

    event_init();

    if (script_path[script_path.length()] != '/')
	script_path.append("/");

    if (sound_path[sound_path.length()] != '/')
	sound_path.append("/");

    if (vm.count("config")) {
	cfile.open(config_file.c_str());

	if (!cfile.fail()) {
	    try {
		read_config(cfile);
	    } catch (const std::exception & err) {
		std::cerr << "parsing config file '" << config_file << "' failed: "
			  << err.what() << std::endl;
	    }
	    cfile.close();
	}
    }

    if (vm.count("stdin")) {
	comm.seperator = '\n';
    }

    if (!dev) dev = new Device();

    dev->makeSnapshot();

    comm.send_command("ready");

    signal(SIGINT, shutdown);

    if (vm.count("exec")) {
	std::vector<std::string>::iterator it = exec.begin();

	for (; it != exec.end(); it++) {
	    std::cerr << "executing: " << *it << std::endl;
	    comm.eval(*it);
	}
    }

    event_set(&ev, 0, EV_READ | EV_PERSIST, comm.read_cb, &comm);
    event_add(&ev, NULL);
    event_dispatch();
    return 0;
}
