CC = g++
CFLAGS = -g -fpic
LDFLAGS = -shared
LIBNAME = libmatrix_wrapper.so
LIB_SRCS = ub_memory.cpp

LIBS = -lpthread -lstdc++ -lrt

all:
	@$(MAKE) clean
	@$(MAKE) build
	@$(MAKE) install

build: $(LIB_SRCS)
	@echo "Building UB Lib $(LIBNAME)"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $(LIBNAME) $^ $(LIBS)

clean:
	@rm -f $(LIBNAME)
	@rm -f $(JDK_IMAGE_DIR)/jre/lib/$(OPENJDK_TARGET_CPU)/$(LIBNAME)

install:
	@echo "Installing $(LIBNAME) to $(JDK_IMAGE_DIR)/jre/lib/$(OPENJDK_TARGET_CPU)"
	@mv $(LIBNAME) $(JDK_IMAGE_DIR)/jre/lib/$(OPENJDK_TARGET_CPU)/

.PHONY: all build clean install
