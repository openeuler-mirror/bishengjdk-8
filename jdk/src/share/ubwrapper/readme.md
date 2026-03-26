## UB Wrapper
this project provides build for the following features:
- ub memory file

## Usage
```
// libmatrix_wrapper.so will be generated 
make
sudo cp ./libmatrix_wrapper.so /usr/lib64/

// install lib to $(JDK_IMAGE_DIR)/jre/lib/$(OPENJDK_TARGET_CPU)
make install

// clean this build
make clean

```