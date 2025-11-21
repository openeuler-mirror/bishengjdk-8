CC = g++
CFLAGS = -g -fpic
LDFLAGS = -shared
LIBNAME = libmatrix_wrapper.so
SRCS = memory_file.cpp

LIBS = -lpthread -lstdc++

all: clean $(LIBNAME) install

$(LIBNAME): $(SRCS)
	@echo "Building UB lib $(LIBNAME)"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

clean:
	@rm -f $(LIBNAME)
	@rm -f $(JDK_IMAGE_DIR)/jre/lib/$(OPENJDK_TRAGET_CPU)/$(LIBNAME)

install: $(LIBNAME)
	@echo "Installing $(LIBNAME) to $(JDK_IMAGE_DIR)/jre/lib/$(OPENJDK_TRAGET_CPU)"
	@mv $(LIBNAME) $(JDK_IMAGE_DIR)/jre/lib/$(OPENJDK_TRAGET_CPU)/

.PHONY: all clean install