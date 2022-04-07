LIVE555_DIR = live
DEPDIR = .deps
PREFIX = out/usr/local
INCLUDES = -I$(LIVE555_DIR)/UsageEnvironment/include -I$(LIVE555_DIR)/groupsock/include -I$(LIVE555_DIR)/liveMedia/include -I$(LIVE555_DIR)/BasicUsageEnvironment/include
# Default library filename suffixes for each library that we link with.  The "config.*" file might redefine these later.
libliveMedia_LIB_SUFFIX = $(LIB_SUFFIX)
libBasicUsageEnvironment_LIB_SUFFIX = $(LIB_SUFFIX)
libUsageEnvironment_LIB_SUFFIX = $(LIB_SUFFIX)
libgroupsock_LIB_SUFFIX = $(LIB_SUFFIX)
##### Change the following for your environment:
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d
EASYLOGGING_FLAGS = -DELPP_THREAD_SAFE

COMPILE_OPTS =	-g $(EASYLOGGING_FLAGS) $(DEPFLAGS) $(INCLUDES) -m64 -fPIC -I/usr/local/include -I. -O0 -DSOCKLEN_T=socklen_t -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64
C =			c
C_COMPILER =		cc
C_FLAGS =		$(COMPILE_OPTS)  -fsanitize=address -fno-omit-frame-pointer
CPP =			cpp
CPLUSPLUS_COMPILER =	c++
CPLUSPLUS_FLAGS = -std=c++11 $(COMPILE_OPTS) -Wall -DBSD=1 -Wno-range-loop-construct  -fsanitize=address -fno-omit-frame-pointer
OBJ =			o
LINK =			c++ -Wl,-v -o
LINK_OPTS =		-L. -fsanitize=address -fno-omit-frame-pointer
CONSOLE_LINK_OPTS =	$(LINK_OPTS)
LIBRARY_LINK =		ar cr
LIBRARY_LINK_OPTS =
LIB_SUFFIX =			a
LIBS_FOR_CONSOLE_APPLICATION = -lavcodec -lavutil -lavformat -lavdevice
LIBS_FOR_GUI_APPLICATION =
EXE =
##### End of variables to change

MEDIA_SERVER = LiveRTSP

ALL = live555 $(MEDIA_SERVER)
all: $(ALL)

.PHONY: live555

live555:
	cd $(LIVE555_DIR); ./genMakefiles linux-no-openssl-asan
	make -C $(LIVE555_DIR)/BasicUsageEnvironment
	make -C $(LIVE555_DIR)/groupsock
	make -C $(LIVE555_DIR)/liveMedia
	make -C $(LIVE555_DIR)/UsageEnvironment
	make -C $(LIVE555_DIR)/mediaServer

EASYLOGGING_CC_SRCS = easyloggingpp/easylogging++.cc

MEDIA_SERVER_CC_SRCS = Main.cc \
					   LiveRTSPServer.cc \
					   H264LiveMediaSubsession.cc \
					   H265LiveMediaSubsession.cc \
					   SimpleLiveMediaSubsession.cc \
					   ADTSLiveMediaSubsession.cc \
					   LiveMediaFactory.cc \
					   LiveMediaSubsession.cc \
					   LiveMediaInputSource.cc \
					   FFH264InputSource.cc

MEDIA_SERVER_OBJS = $(MEDIA_SERVER_C_SRCS:%.c=%.o) $(MEDIA_SERVER_CC_SRCS:%.cc=%.o) $(EASYLOGGING_CC_SRCS:.cc=.o)
DEPFILES := $(MEDIA_SERVER_C_SRCS:%.c=$(DESTDIR)/%.d) $(MEDIA_SERVER_CC_SRCS:%.cc=$(DEPDIR)/%.d)
#$(info $(DEPFILES))

USAGE_ENVIRONMENT_DIR = $(LIVE555_DIR)/UsageEnvironment
USAGE_ENVIRONMENT_LIB = $(USAGE_ENVIRONMENT_DIR)/libUsageEnvironment.$(libUsageEnvironment_LIB_SUFFIX)
BASIC_USAGE_ENVIRONMENT_DIR = $(LIVE555_DIR)/BasicUsageEnvironment
BASIC_USAGE_ENVIRONMENT_LIB = $(BASIC_USAGE_ENVIRONMENT_DIR)/libBasicUsageEnvironment.$(libBasicUsageEnvironment_LIB_SUFFIX)
LIVEMEDIA_DIR = $(LIVE555_DIR)/liveMedia
LIVEMEDIA_LIB = $(LIVEMEDIA_DIR)/libliveMedia.$(libliveMedia_LIB_SUFFIX)
GROUPSOCK_DIR = $(LIVE555_DIR)/groupsock
GROUPSOCK_LIB = $(GROUPSOCK_DIR)/libgroupsock.$(libgroupsock_LIB_SUFFIX)
LOCAL_LIBS =	$(LIVEMEDIA_LIB) $(GROUPSOCK_LIB) \
		$(BASIC_USAGE_ENVIRONMENT_LIB) $(USAGE_ENVIRONMENT_LIB)
LIBS =			$(LOCAL_LIBS) $(LIBS_FOR_CONSOLE_APPLICATION)

$(MEDIA_SERVER): $(MEDIA_SERVER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MEDIA_SERVER_OBJS) $(LIBS)

clean:
	-make -C live clean
	-rm -rf *.$(OBJ) $(ALL) core *.core *~ include/*~

install: $(MEDIA_SERVER)
	  install -d $(DESTDIR)$(PREFIX)/bin
	  install -m 755 $(MEDIA_SERVER) $(DESTDIR)$(PREFIX)/bin

%.o : %.c
%.o : %.c $(DEPDIR)/%.d | $(DEPDIR)
	$(C_COMPILER) -c $(C_FLAGS) $< -o $@

%.o : %.cc
%.o : %.cc $(DEPDIR)/%.d | $(DEPDIR)
	$(info $@ $<)
	$(CPLUSPLUS_COMPILER) -c $(CPLUSPLUS_FLAGS) $< -o $@

easyloggingpp/%.o : easyloggingpp/%.cc
	$(CPLUSPLUS_COMPILER) -c $(CPLUSPLUS_FLAGS) $< -o $@

$(DEPDIR): ; @mkdir -p $@

$(DEPFILES):
	$(info depfiles: $@)
include $(wildcard $(DEPFILES))
##### Any additional, platform-specific rules come here:
