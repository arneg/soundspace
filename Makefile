LKLIB          = -ldl -levent -ljsoncpp -lm
INTERPOL_OBJS  = common/cpp/interpol.o common/cpp/json_builder.o
INTERPOL_DEPS  = interpol.h json_builder.h $(INTERPOL_OBJS)
INCL	       = -L/usr/libs/jsoncpp -I/usr/include/jsoncpp -Icommon/cpp/
CXX	       = g++ -O0 -Wall -g $(INCL)

all: soundspace/soundspace soundspace/test_soundspace

VPATH = common/cpp soundspace

.SECONDARY:

common/cpp/%.o: %.cpp %.h
	$(CXX) -c $< -o $@

soundspace/%: %.cpp $(INTERPOL_DEPS)
	$(CXX) $(GX_CXXFLAGS) -o $@ $(INTERPOL_OBJS) $< $(LKLIB) -lopenal -lm

soundspace/test_%: %.cpp $(INTERPOL_DEPS)
	$(CXX) $(GX_CXXFLAGS) -DTESTING -o $@ $(INTERPOL_OBJS) $< $(LKLIB) -lopenal -lm
