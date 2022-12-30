CUDA_VER?=
ifeq ($(CUDA_VER),)
  $(error "CUDA_VER is not set")
endif

NVDS_VERSION:=6.1

LIB_INSTALL_DIR?=/opt/nvidia/deepstream/deepstream-$(NVDS_VERSION)/lib/
APP_INSTALL_DIR?=/opt/nvidia/deepstream/deepstream-$(NVDS_VERSION)/bin/

APP:= deepstream-app-custom

CC = g++
TARGET_DEVICE = $(shell gcc -dumpmachine | cut -f1 -d -)

CFLAGS:= -fvisibility=hidden -Wall -Werror -Wno-unused-variable -Wno-unused-function

ifeq ($(TARGET_DEVICE),aarch64)
  CFLAGS+= -DPLATFORM_TEGRA
endif

APP_SRCS:= $(wildcard *.cpp)
APP_INCS:= $(wildcard *.h)

PKGS:= gstreamer-1.0

CFLAGS+= -I../../../includes \
         -I ../../../gst-plugins/gst-nvdspreprocess/include \
	 	-I /usr/local/cuda-$(CUDA_VER)/include \
	 	-I../../apps-common/includes \
		-I ../includes \
         -fPIC -std=c++14
		 

CFLAGS+= $(shell pkg-config --cflags $(PKGS))
LIBS:= $(shell pkg-config --libs $(PKGS))

CFLAGS+= $(shell pkg-config --cflags opencv4 2> /dev/null || pkg-config --cflags opencv)
LIBS+= $(shell pkg-config --libs opencv4 2> /dev/null || pkg-config --libs opencv)

LIBS+= -L/usr/local/cuda-$(CUDA_VER)/lib64/ -lcudart -lnvdsgst_helper -lm \
		-L/opt/nvidia/deepstream/deepstream-$(NVDS_VERSION)/lib/ \
		-L$(LIB_INSTALL_DIR) -lnvdsgst_meta -lnvds_meta \
		-lcuda -Wl,-rpath,$(LIB_INSTALL_DIR)

LIBS+= -lnvinfer_plugin -lnvinfer -lnvparsers -L/usr/local/cuda-$(CUDA_VER)/lib64 \
		-lcudart -lcublas -lstdc++fs -lgomp \
		-L/opt/nvidia/deepstream/deepstream-$(NVDS_VERSION)/lib \
		-lnvdsgst_meta -lnvds_meta -lnvdsgst_helper \
        -lnvdsgst_smartrecord -lnvds_utils -lnvds_msgbroker -lm -lyaml-cpp \
    	-lcuda -lgstrtspserver-1.0 

APP_OBJS:= $(APP_SRCS:.cpp=.o)

all: $(APP) $(SUBFOLDER)

%.o: %.cpp $(APP_INCS) Makefile
	$(CC) -c -o $@ $(CFLAGS) $<

$(APP): $(APP_OBJS) Makefile
	$(CC) -o $(APP) $(APP_OBJS) $(LIBS)

$(SUBFOLDER):
	$(MAKE) -C $@ $(MAKECMDGOALS)

install: $(APP) $(SUBFOLDER)
	cp -rv $(APP) $(APP_INSTALL_DIR)

clean: $(SUBFOLDER)
	rm -rf $(APP_OBJS) $(APP)

.PHONY: all $(SUBFOLDER)