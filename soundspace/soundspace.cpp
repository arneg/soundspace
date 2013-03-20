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
#include <cstring>

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alut.h>

void interpol_callback(Json::Value&);

Interpol comm = Interpol("soundspace", interpol_callback);
Json::Value config;

static inline void checkError() {
    ALenum err = alGetError();
    switch (err) {
#define CASE(name)  case name : throw("Got error " #name );
    CASE(AL_INVALID_NAME)
    CASE(AL_INVALID_ENUM)
    CASE(AL_INVALID_VALUE)
    CASE(AL_INVALID_OPERATION)
    CASE(AL_OUT_OF_MEMORY)
    case AL_NO_ERROR:
	return;
    }
}


static inline void Json2AL(Json::Value & v, ALfloat a[],
			   const unsigned int n = 3) {
    if (!v.isArray()) throw("Bad argument 1 to Json2fv. Expected Array.");
    for (unsigned int i = 0; i < n; i++) {
	if (!v[i].isNumeric()) throw("Bad element in vector. Expected numeric");
	a[i] = (ALfloat)v[i].asDouble();
    }
}

static inline void Json2AL(Json::Value & v, ALfloat & f) {
    if (!v.isNumeric()) throw("Bad argument 1 to Json2f. Expected numeric");
    f = (ALfloat)v.asDouble();
}

static inline void Json2AL(Json::Value & v, ALint & i) {
    if (!v.isNumeric()) throw("Bad argument 1 to Json2f. Expected numeric");
    i = (ALint)v.asInt();
}

static inline void Json2AL(Json::Value & v, bool & b) {
    if (!v.isBool()) throw("Bad argument 1 to Json2f. Expected true or false");
    b = (ALfloat)v.asBool();
}

class Buffer {
public:
    ALuint id;
#ifndef ALUT
    void * data;
    int fd;
    struct stat st;
#endif

    Buffer() {
	alGenBuffers(1, &id);
    }

    void fromFile(const char * f) {
#ifdef ALUT
	ALint channels = 1;
	id = alutCreateBufferFromFile(f);
	if (id == AL_NONE) {
	    std::cerr << "could not load file " << f << std::endl;
	    throw("loading file failed.");
	}
	alGetBufferi(id, AL_CHANNELS, &channels);
	if (channels > 1) {
	    std::cerr << "Warning: '" << f << "' contains stereo data and"
			 " will be played without spatialization." << std::endl;
	}
#else

	fd = open(f, O_RDONLY);

	if (fd == -1)
	    throw("could not open file");

	if (fstat(fd, &st) == -1)
	    throw("could not stat file");

	if (!S_ISREG (st.st_mode))
	    throw("not a regular file");

#ifdef TESTING
	std::cerr << "open file " << f << " with size " << st.st_size << std::endl;
#endif

	data = mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);

	if (data == MAP_FAILED) {
	    close(fd);
	    throw("mmap failed");
	}

	madvise(data, st.st_size, MADV_SEQUENTIAL);

	{
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
	    const struct riff_header * rhead;
	    const struct wave_format * whead;
	    const struct pcm_header * phead;
	    const char * buf = (const char *) data;
	    ALenum format;
	    ALuint frequency;
	    const int HEADER_SIZE = sizeof(struct riff_header) + sizeof(struct wave_format)
				    + sizeof(struct pcm_header);

	    if (st.st_size < HEADER_SIZE) {
		throw("muha");
	    }

	    rhead = (const struct riff_header*)buf;
	    buf += sizeof(struct riff_header);
	    whead = (const struct wave_format*)buf;
	    buf += sizeof(struct wave_format);
	    phead = (const struct pcm_header*)buf;
	    buf += sizeof(struct pcm_header);

	    if (strncmp(rhead->riff, "RIFF", 4) || strncmp(rhead->wave, "WAVE", 4)) {
		throw("bad riff wave header");
	    }

	    if (rhead->length + 8 != st.st_size)
		throw("someone is lying about the size of this wave");

	    if (strncmp(whead->fmt, "fmt ", 4) || whead->len != 16)
		throw("bad wave format");

#ifdef TESTING
	    std::cerr << "bits: " << whead->bits_per_sample
		      << ", channels: " << whead->channels << std::endl;
#endif
	    if (whead->channels == 1) {
		format = (whead->bits_per_sample == 8) ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
	    } else if (whead->channels == 2) {
		std::cerr << "Warning: '" << f << "' contains stereo data and"
			     " will be played without spatialization." << std::endl;
		format = (whead->bits_per_sample == 8) ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
	    } else throw("bad number of channels");

	    frequency = (ALuint)whead->sample_rate;

	    if (strncmp(phead->data, "data", 4))
		throw("bad pcm header");

	    alGenBuffers(1, &id);
#ifdef TESTING
	    std::cerr << "generated buffer " << id << std::endl;
#endif
	    alBufferData(id, format, buf, st.st_size - HEADER_SIZE, frequency);
	}
#endif
    }

    Buffer(const char * f) {
	fromFile(f);
    }

    Buffer(std::string & file) {
	fromFile(file.c_str());
    }

    Buffer(Json::Value & s) {
	if (!s.isString())
	    throw("Bad argument one to Buffer(). Expected string.");
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
	std::cerr << "deleting buffer " << id << std::endl;
	alDeleteBuffers(1, &id);
#ifndef ALUT
	munmap(data, st.st_size);
	close(fd);
#endif
    }

};

class Listener {
public:
#define fvFUN(name, FLAG)    ALfloat name ## value[3];			    \
    ALfloat * name (ALfloat v[]) {					    \
	name ## value[0] = v[0];					    \
	name ## value[1] = v[1];					    \
	name ## value[2] = v[2];					    \
	alListenerfv(FLAG, name ## value);				    \
	return name ## value;						    \
    }									    \
    ALfloat * name (ALfloat x, ALfloat y, ALfloat z) {			    \
	name ## value[0] = x;						    \
	name ## value[1] = y;						    \
	name ## value[2] = z;						    \
	alListenerfv(FLAG, name ## value);				    \
	return name ## value;						    \
    }									    \
    ALfloat * name (Json::Value & v) {					    \
	Json2AL(v, name ## value);					    \
	alListenerfv(FLAG, name ## value);				    \
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
	static const ALfloat default_orientation[] = {
	    1.0, 0.0, 0.0,
	    0.0, 1.0, 0.0
	};
	orientation(default_orientation);
	position(0.0, 0.0, 0.0);
	velocity(0.0, 0.0, 0.0);
    }
};

// forward definition
class Device;

class Source {
    void get(ALenum pname, ALint & i) {
	alGetSourcei(id, pname, & i);
	checkError();
    }

    void get(ALenum pname, ALfloat & f) {
	alGetSourcef(id, pname, & f);
	checkError();
    }

    void get(ALenum pname, ALfloat f[]) {
	alGetSourcefv(id, pname, f);
	checkError();
    }

    void set(ALenum pname, ALint i) {
	alSourcei(id, pname, i);
	checkError();
    }

    void set(ALenum pname, ALfloat f) {
	alSourcef(id, pname, f);
	checkError();
    }

    void set(ALenum pname, ALfloat f[]) {
	alSourcefv(id, pname, f);
	checkError();
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
    Buffer * buffer;
    Device * dev;
    ALuint id;
    bool is_copy;

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
    bFUN(loop, AL_LOOPING);
    iFUN(state, AL_SOURCE_STATE);

    void update() {
	position();
	velocity();
	pitch();
	gain();
	min_gain();
	max_gain();
	loop();
    }

    void apply() {
	position(position_value);
	velocity(velocity_value);
	pitch(pitch_value);
	gain(gain_value);
	min_gain(min_gain_value);
	max_gain(max_gain_value);
	loop(loop_value);
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
	alSourcei(id, AL_BUFFER, buf->id);
    }

#define SOURCE_ACTION(name) void name () {		    \
	alSource ## name(id);				    \
    }
    SOURCE_ACTION(Play);
    SOURCE_ACTION(Pause);
    SOURCE_ACTION(Stop);
    SOURCE_ACTION(Rewind);

    Source(Device * _dev) : dev(_dev) {
	is_copy = false;
	buffer = NULL;
	alGenSources(1, &id);
#ifdef TESTING
	std::cerr << "created source " << id << std::endl;
#endif
    }

    Source(Device * _dev, ALuint _id, Buffer * _buffer = NULL) : buffer(_buffer), dev(_dev), id(_id) {
#ifdef TESTING
	std::cerr << "copied source " << id << std::endl;
#endif
	update();
	is_copy = true;
#if 0
	position(0.0, 0.0, 0.0);
	velocity(0.0, 0.0, 0.0);
	min_gain(0.0f);
	max_gain(1.0f);
	gain(1.0f);
	pitch(1.0f);
	loop(false);
#endif
    }

    Source * copy() {
	Source * t = new Source(dev, id, buffer);
	return t;
    }
    
    ~Source() {
	Stop();
	if (!is_copy) {
	    delete(buffer);
	    alDeleteSources(1, &id);
#ifdef TESTING
	    std::cerr << "deleted source " << id << std::endl;
#endif
	} else {
#ifdef TESTING
	    std::cerr << "deleted copied source " << id << std::endl;
#endif
	}
    }

};

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
#if TESTING
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
};

// animation interval is 50 ms
static const struct timeval soundtimer_t = { 0, 20*1000 };
struct event timer_ev;
class Animator {
    std::list<Animation*> l;

public:
    void add(Animation * a) {
	if (l.size() == 0) {
	    evtimer_add(&timer_ev, &soundtimer_t);
	}
	l.push_back(a);
    }

    static bool is_done(Animation * a) {
	return a->done();
    }

    void run() {
	std::list<Animation*>::iterator it;
	for (it = l.begin(); it != l.end(); it++) {
	    Animation * a = * it;

	    a->update();
	    a->step();
	}

	l.remove_if(is_done);

	if (l.size()) {
	    evtimer_add(&timer_ev, &soundtimer_t);
	}
    }

    void clear() {
	if (l.size()) {
	    evtimer_del(&timer_ev);
	    l.clear();
	}
    }

    static void animation_callback(int, short int, void * o) {
	((Animator*)o)->run();
    }

    Animator() {
	evtimer_set(&timer_ev, animation_callback, this);
    }
};

class Device {
public:
    std::vector<Source*> sources, snapshot;
    std::map<std::string,Source*> name2source;
    Listener l;
    Animator animator;
#ifndef ALUT
    ALCdevice * dev;
    ALCcontext * ctx;
#endif

    Device() {
#if 0
	if (alIsExtensionPresent("ALC_ENUMERATION_EXT") == AL_FALSE) {
	    std::cerr << "enumeration extension is missin???" << std::endl;
	    return;
	}
	Device(alcGetString(NULL, ALC_DEVICE_SPECIFIER));
#endif
#ifdef ALUT
	// fake and cheap
	alutInit(0, NULL);
#else
	dev = alcOpenDevice(NULL);
	if (!dev) {
	    throw("foo");
	}
	ctx = alcCreateContext(dev, NULL);
	if (!ctx) {
	    throw("bar");
	}
	alcMakeContextCurrent(ctx);
#endif
    }

    Device(const ALCchar * name) {
	//device = alcOpenDevice(name);
	//std::cerr << "opened device " << name << std::endl;
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
	    throw("mismatching snapshot");
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
	    throw("Could not find source by name.");
	return it->second;
    }

    Source * getSource(Json::Value & v) {
	if (v.isNumeric()) {
	    return getSource((size_t)v.asUInt());
	} else if (v.isString()) {
	    const std::string s = v.asString();
	    return getSource(s);
	} else
	    throw("Bad argument 1 to getSource(). Expected uint or string.");
    }

    void removeSource(Json::Value & ids) {
	std::vector<Source*> a;
	Ids2Sources(ids, a);
	std::vector<Source*>::iterator it1;

	for (it1 = a.begin(); it1 != a.end(); it1++) {
	    ALuint id = (*it1)->id;
	    std::vector<Source*>::reverse_iterator it2;

	    for (it2 = snapshot.rbegin(); it2 != snapshot.rend(); it2++) {

		if ((*it2)->id == id) {
		    delete(*it2);
		    snapshot.erase(--it2.base());
		    break;
		}
	    }

	    for (it2 = sources.rbegin(); it2 != sources.rend(); it2++) {
		if ((*it2)->id == id) {
		    delete(*it2);
		    sources.erase(--it2.base());
		    break;
		}
	    }

	    std::map<std::string,Source*>::iterator it;

	    for (it = name2source.begin(); it != name2source.end(); it++) {
		if (it->second == *it1) {
		    name2source.erase(it);
		    break;
		}
	    }
	}
    }

    void checkSource(size_t id) throw(const char *) {
	if (id > sources.size())
	    throw("Source ID is out of range.");
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
	    } else throw("bad argument one to Json2Ids. Expected string|int|array|true");
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
	    } else throw("bad argument one to Json2Ids. Expected string|int|array|true");
	} else a.push_back(getSource(ids));
    }

#define DEVICE_ACTION(name) void name (Json::Value & ids)		\
    {									\
	std::vector<ALuint> a;						\
	Json2Ids(ids, a);						\
									\
	alSource ## name ## v(a.size(), &(a[0]));			\
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
	    if (sources[i]->state() == AL_PLAYING) {
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
	std::vector<Source*>::iterator it;

	for (it = snapshot.begin(); it != snapshot.end(); it++) {
	    delete(*it);
	}
	for (it = sources.begin(); it != sources.end(); it++) {
	    delete(*it);
	}
#ifdef ALUT
	alutExit();
#else
	alcMakeContextCurrent(NULL);
	alcDestroyContext(ctx);
	alcCloseDevice(dev);
#endif
    }
};

Device * dev = NULL;

std::string sound_path, script_path;

static const char * conf_names[] = {
    "../soundspace/soundspace.conf",
    "../immigration/soundspace.conf",
    "../soundspace/soundspace.config",
    "../immigration/soundspace.config",
    "soundspace.conf",
    "soundspace.config",
    "soundspace.conf.sample",
    "/opt/memopol/immigration/soundspace.conf"
};


Source * sourceFromFile(std::string & file, std::string & name) {
    std::string path = sound_path + file;
    Source * s = dev->getSource();
    Buffer * buf = new Buffer(path);
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
    std::cerr << "disconnected" << std::endl;
    std::cerr.flush();
    comm.send_error("shutdown");
    if (dev) delete(dev);
    exit(code);
}

__attribute__((noreturn))
void shutdown(int code, const char * s) {
    std::cerr << "shutdown for REASON: " << s << std::endl;
    shutdown(code);
}

void setup() {
    std::ifstream cfile;
    Json::Reader r;
    Json::Value v;
    Json::Value::ArrayIndex n;
    unsigned int i;

    for (i = 0; i < sizeof(*conf_names); i++) {
	std::cerr << "trying to open config file '" << conf_names[i] << "'" << std::endl;
	cfile.open(conf_names[i]);
	if (!cfile.fail()) {
	    std::cerr << "opened config file '" << conf_names[i] << "'" << std::endl;
	    break;
	}
    }

    if (cfile.fail()) {
	shutdown(1, "could not open config file");
    }

    try {
	r.parse(cfile, config, false);
    } catch(...) {
	shutdown(1, "error while parsing configuration 'soundspace.config'");
    }

    try {
	if (!!(v = config["sources"]) && v.isArray() && (n = v.size()) > 0) {
	    Json::Value::ArrayIndex i;

	    std::cerr << "found " << n << " sources" << std::endl;

	    if (config.isMember("path")) {
		sound_path = config["path"].asString();
		sound_path.append("/");
	    }

	    if (config.isMember("script_path")) {
		script_path = config["script_path"].asString();
		script_path.append("/");
	    }

	    for (i = 0; i < n; i++) {
		Json::Value sinfo = v[i];
		sourceFromJSON(sinfo);
	    }
	} else shutdown(1, "no sources found");

	if (config.isMember("listener")) {
	    v = config["listener"];
	    if (!v.isObject())
		throw("bad configuration 'listener'. Expected object.");
	    CONFIG_SET(v, &(dev->l), orientation);
	    CONFIG_SET(v, &(dev->l), position);
	    CONFIG_SET(v, &(dev->l), velocity);
	} else shutdown(1, "no listener found");

    } catch (const char * s) {
	shutdown(1, s);
    }

    dev->makeSnapshot();

#if 0//def TESTING
    for (size_t i = 0; i < n; i++) {
	std::cerr << "testing source " << i << std::endl;
	dev->sources[i]->Play();
	sleep(1);
    }
#endif
}

void interpol_callback(Json::Value & root) {
    try {
	if (root["cmd"] == "play") {
	    dev->Play(root["ids"]);
	} else if (root["cmd"] == "eval") {
	    if (!root["script"].isString()) {
		throw("bad script file. expected string.");
	    }
	    std::string file = script_path;
	    std::cerr << "script_path: " << script_path << std::endl;
	    file.append(root["script"].asCString());
	    comm.eval(file);
	} else if (root["cmd"] == "add_source") {
	    Source * s = sourceFromJSON(root);
	    if (s) dev->snapshot.push_back(s->copy());
	} else if (root["cmd"] == "remove_source") {
	    dev->removeSource(root["ids"]);
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
    } catch (const char * s) {
	std::cerr << "error in " << root["cmd"].asString() << ": '"
		  << s << "'" << std::endl;
    } catch (...) {
	std::cerr << "unknown error in " << root["cmd"].asString() << std::endl;
    }
}

int main(int argc, char ** argv) {
    struct event ev;

    std::cerr << "Init()"<<std::endl;
    event_init();
#ifdef TESTING
    comm.seperator = '\n';
#endif

    alutInit(&argc, argv);

    dev = new Device();

    setup();

    comm.send_command("ready");

    signal(SIGINT, shutdown);

    if (argc > 1) {
	comm.eval(argv[1]);
    }

    event_set(&ev, 0, EV_READ | EV_PERSIST, comm.read_cb, &comm);
    event_add(&ev, NULL);
    std::cerr << "dispatching" << std::endl;
    event_dispatch();
    return 0;
}
