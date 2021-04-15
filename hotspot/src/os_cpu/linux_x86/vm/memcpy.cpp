/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2018-2020. All rights reserved.
 */

#if defined( __GNUC__ ) && \
(__GNUC__ >= 5  ||  (__GNUC__ == 4  &&  __GNUC_MINOR__ >= 7)) 
#include <string.h>

#if (defined AMD64) || (defined amd64)
/* some systems do not have newest memcpy@@GLIBC_2.14 - stay with old good one */
asm (".symver memcpy, memcpy@GLIBC_2.2.5");

extern "C"{
  void *__wrap_memcpy(void *dest, const void *src, size_t n)
  {
    return memcpy(dest, src, n);
  }
}
#endif
#endif

