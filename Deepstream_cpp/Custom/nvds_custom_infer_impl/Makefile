CUDA_VER=11.7
ifeq ($(CUDA_VER),)
  $(error "CUDA_VER is not set")
endif

CC:=g++
NVCC:=/usr/local/cuda-$(CUDA_VER)/bin/nvcc 

CFLAGS:= -Wall -std=c++11 -shared -fPIC -Wno-error=deprecated-declarations
CFLAGS+= -I/opt/nvidia/deepstream/deepstream/sources/includes -I/usr/local/cuda-$(CUDA_VER)/include

# ifeq ($(OPENCV), 1)
COMMON= -DOPENCV
CFLAGS+= $(shell pkg-config --cflags opencv4 2> /dev/null || pkg-config --cflags opencv)
LIBS+= $(shell pkg-config --libs opencv4 2> /dev/null || pkg-config --libs opencv)
# endif

CXXFLAGS:= -fopenmp

LIBS+= -lnvinfer_plugin -lnvinfer -lnvparsers -L/usr/local/cuda-$(CUDA_VER)/lib64 \
		-lcudart -lcublas -lstdc++fs -lgomp \
		-L/opt/nvidia/deepstream/deepstream-6.1/lib \
		-lnvdsgst_meta -lnvds_meta -lnvdsgst_helper \
        -lnvdsgst_smartrecord -lnvds_utils -lnvds_msgbroker -lm -lyaml-cpp \
    	-lcuda -lgstrtspserver-1.0 
LFLAGS:= -shared -Wl,--start-group $(LIBS) -Wl,--end-group

INCS:= $(wildcard *.h)
SRCFILES:= nvds_box_parser.cpp

TARGET_LIB:= nvds_box_parser.so

TARGET_OBJS:= $(SRCFILES:.cpp=.o)
TARGET_OBJS:= $(TARGET_OBJS:.cu=.o)

SUBFOLDER:=nvds_custom_infer_impl

all: $(TARGET_LIB)

%.o: %.cpp $(INCS) Makefile
	$(CC) -c -o $@ $(CFLAGS) $(CXXFLAGS) $<

%.o: %.cu $(INCS) Makefile
	$(NVCC) -c -o $@ --compiler-options '-fPIC' $<

$(TARGET_LIB) : $(TARGET_OBJS)
	$(CC) -o $@  $(TARGET_OBJS) $(LFLAGS)

clean:
	rm -rf $(TARGET_LIB)
	rm -rf $(TARGET_OBJS)
