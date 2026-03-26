CC = g++
CFLAGS = -g -fpic
LDFLAGS = -shared
LIBNAME = libmatrix_wrapper.so
SRCS = ub_memory.cpp

LIBS = -lpthread -lstdc++ -lrt

all: clean $(LIBNAME) install

$(LIBNAME): $(SRCS)
	@echo "Building UB Lib $(LIBNAME)"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	@rm -f $(LIBNAME)
	@rm -f $(JDK_IMAGE_DIR)/jre/lib/$(OPENJDK_TARGET_CPU)/$(LIBNAME)

install: $(LIBNAME)
	@echo "Installing $(LIBNAME) to $(JDK_IMAGE_DIR)/jre/lib/$(OPENJDK_TARGET_CPU)"
	@mv $(LIBNAME) $(JDK_IMAGE_DIR)/jre/lib/$(OPENJDK_TARGET_CPU)/

.PHONY: all clean install