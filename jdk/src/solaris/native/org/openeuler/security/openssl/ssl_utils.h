/*
 * Copyright (c) 2024, Huawei Technologies Co., Ltd. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#ifndef SSL_UTILS_H
#define SSL_UTILS_H

#include <openssl/ssl.h>

const BIGNUM *SSL_UTILS_RSA_get0_n(const RSA *r);

const BIGNUM *SSL_UTILS_RSA_get0_e(const RSA *r);

const BIGNUM *SSL_UTILS_RSA_get0_d(const RSA *r);

const BIGNUM *SSL_UTILS_RSA_get0_p(const RSA *r);

const BIGNUM *SSL_UTILS_RSA_get0_q(const RSA *r);

const BIGNUM *SSL_UTILS_RSA_get0_dmp1(const RSA *r);

const BIGNUM *SSL_UTILS_RSA_get0_dmq1(const RSA *r);

const BIGNUM *SSL_UTILS_RSA_get0_iqmp(const RSA *r);

RSA *SSL_UTILS_RSA_new_method(ENGINE *engine);

int SSL_UTILS_RSA_generate_key_ex(RSA *rsa, int bits, BIGNUM *e_value, BN_GENCB *cb);

void SSL_UTILS_RSA_free(RSA *rsa);

int SSL_UTILS_ERR_load_BIO_strings();

int SSL_UTILS_OpenSSL_add_all_algorithms();

int SSL_UTILS_ENGINE_free(ENGINE *e);

ENGINE *SSL_UTILS_ENGINE_by_id(const char *id);

EVP_MD *SSL_UTILS_EVP_get_digestbyname(const char *name);

void SSL_UTILS_EVP_PKEY_CTX_free(EVP_PKEY_CTX *ctx);

int SSL_UTILS_EVP_PKEY_CTX_set_rsa_padding(EVP_PKEY_CTX *ctx, int pad_mode);

int SSL_UTILS_EVP_PKEY_CTX_set_signature_md(EVP_PKEY_CTX *ctx, const EVP_MD *md);

EVP_PKEY_CTX *SSL_UTILS_EVP_PKEY_CTX_new(EVP_PKEY *pkey, ENGINE *e);

int SSL_UTILS_EVP_PKEY_sign_init(EVP_PKEY_CTX *ctx);

int SSL_UTILS_EVP_PKEY_sign(
    EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *siglen, const unsigned char *tbs, size_t tbslen);

int SSL_UTILS_EVP_PKEY_verify_init(EVP_PKEY_CTX *ctx);

int SSL_UTILS_EVP_PKEY_verify(
    EVP_PKEY_CTX *ctx, const unsigned char *sig, size_t siglen, const unsigned char *tbs, size_t tbslen);

int SSL_UTILS_EVP_PKEY_CTX_set_rsa_mgf1_md(EVP_PKEY_CTX *ctx, const EVP_MD *md);

int SSL_UTILS_EVP_PKEY_CTX_set_rsa_pss_saltlen(EVP_PKEY_CTX *ctx, int len);

int SSL_UTILS_EVP_PKEY_size(const EVP_PKEY *pkey);

EVP_CIPHER *SSL_UTILS_EVP_get_cipherbyname(const char *name);

EVP_CIPHER_CTX *SSL_UTILS_EVP_CIPHER_CTX_new(void);

int SSL_UTILS_EVP_CipherInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *type, ENGINE *impl, const unsigned char *key,
    const unsigned char *iv, int enc);

int SSL_UTILS_EVP_CIPHER_CTX_set_padding(EVP_CIPHER_CTX *ctx, int pad);

void SSL_UTILS_EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *ctx);

int SSL_UTILS_EVP_CipherUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in, int inl);

int SSL_UTILS_EVP_CipherFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl);

int SSL_UTILS_EVP_CIPHER_CTX_ctrl(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr);

BIGNUM *SSL_UTILS_BN_new(void);

BIGNUM *SSL_UTILS_BN_bin2bn(const unsigned char *s, int len, BIGNUM *ret);

void SSL_UTILS_BN_free(BIGNUM *a);

int SSL_UTILS_EVP_PKEY_CTX_set0_rsa_oaep_label(EVP_PKEY_CTX *ctx, void *label, int llen);

int SSL_UTILS_EVP_PKEY_CTX_set_rsa_oaep_md(EVP_PKEY_CTX *ctx, const EVP_MD *md);

EVP_PKEY *SSL_UTILS_EVP_PKEY_new(void);

int SSL_UTILS_RSA_set0_key(RSA *r, BIGNUM *n, BIGNUM *e, BIGNUM *d);

int SSL_UTILS_RSA_set0_factors(RSA *r, BIGNUM *p, BIGNUM *q);

int SSL_UTILS_RSA_set0_crt_params(RSA *r, BIGNUM *dmp1, BIGNUM *dmq1, BIGNUM *iqmp);

void SSL_UTILS_EVP_PKEY_free(EVP_PKEY *x);

int SSL_UTILS_RSA_private_encrypt(int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding);

int SSL_UTILS_RSA_private_decrypt(int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding);

int SSL_UTILS_RSA_public_encrypt(int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding);

int SSL_UTILS_RSA_public_decrypt(int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding);

int SSL_UTILS_EVP_PKEY_encrypt_init(EVP_PKEY_CTX *ctx);

int SSL_UTILS_EVP_PKEY_encrypt(
    EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen, const unsigned char *in, size_t inlen);

int SSL_UTILS_EVP_PKEY_decrypt_init(EVP_PKEY_CTX *ctx);

int SSL_UTILS_EVP_PKEY_decrypt(
    EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen, const unsigned char *in, size_t inlen);

EVP_MD_CTX *SSL_UTILS_EVP_MD_CTX_new(void);

int SSL_UTILS_EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl);

void SSL_UTILS_EVP_MD_CTX_free(EVP_MD_CTX *ctx);

int SSL_UTILS_EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *data, size_t count);

int SSL_UTILS_EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *size);

int SSL_UTILS_EVP_MD_CTX_copy_ex(EVP_MD_CTX *out, const EVP_MD_CTX *in);

unsigned long SSL_UTILS_ERR_get_error_line_data(const char **file, int *line, const char **data, int *flags);

void SSL_UTILS_ERR_error_string_n(unsigned long e, char *buf, size_t len);

void SSL_UTILS_ERR_clear_error(void);

HMAC_CTX *SSL_UTILS_HMAC_CTX_new(void);

int SSL_UTILS_HMAC_Init_ex(HMAC_CTX *ctx, const void *key, int len, const EVP_MD *md, ENGINE *impl);

void SSL_UTILS_HMAC_CTX_free(HMAC_CTX *ctx);

int SSL_UTILS_HMAC_Update(HMAC_CTX *ctx, const unsigned char *data, size_t len);

int SSL_UTILS_HMAC_Final(HMAC_CTX *ctx, unsigned char *md, unsigned int *len);

DH *SSL_UTILS_DH_new_method(ENGINE *engine);

int SSL_UTILS_DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g);

int SSL_UTILS_DH_set0_key(DH *dh, BIGNUM *pub_key, BIGNUM *priv_key);

int SSL_UTILS_DH_compute_key(unsigned char *key, const BIGNUM *pub_key, DH *dh);

void SSL_UTILS_DH_free(DH *r);

void SSL_UTILS_EC_POINT_free(EC_POINT *point);

void SSL_UTILS_EC_KEY_free(EC_KEY *r);

void SSL_UTILS_EC_GROUP_free(EC_GROUP *group);

int SSL_UTILS_OBJ_sn2nid(const char *s);

EC_GROUP *SSL_UTILS_EC_GROUP_new_by_curve_name(int nid);

EC_KEY *SSL_UTILS_EC_KEY_new(void);

int SSL_UTILS_EC_KEY_set_group(EC_KEY *key, const EC_GROUP *group);

EC_POINT *SSL_UTILS_EC_POINT_new(const EC_GROUP *group);

int SSL_UTILS_EC_POINT_set_affine_coordinates_GFp(
    const EC_GROUP *group, EC_POINT *point, const BIGNUM *x, const BIGNUM *y, BN_CTX *ctx);

int SSL_UTILS_EC_KEY_set_public_key(EC_KEY *key, const EC_POINT *pub_key);

int SSL_UTILS_EC_KEY_set_private_key(EC_KEY *key, const BIGNUM *priv_key);

int SSL_UTILS_EC_GROUP_get_degree(const EC_GROUP *group);

int SSL_UTILS_ECDH_compute_key(void *out, size_t outlen, const EC_POINT *pub_key, const EC_KEY *eckey,
    void *(*KDF)(const void *in, size_t inlen, void *out, size_t *outlen));

int SSL_UTILS_DH_set_length(DH *dh, long length);

int SSL_UTILS_DH_generate_key(DH *dh);

const BIGNUM *SSL_UTILS_DH_get0_priv_key(const DH *dh);

const BIGNUM *SSL_UTILS_DH_get0_pub_key(const DH *dh);

int SSL_UTILS_EC_GROUP_get_curve_GFp(const EC_GROUP *group, BIGNUM *p, BIGNUM *a, BIGNUM *b, BN_CTX *ctx);

const EC_POINT *SSL_UTILS_EC_GROUP_get0_generator(const EC_GROUP *group);

int SSL_UTILS_EC_POINT_get_affine_coordinates_GFp(
    const EC_GROUP *group, const EC_POINT *point, BIGNUM *x, BIGNUM *y, BN_CTX *ctx);

int SSL_UTILS_EC_GROUP_get_order(const EC_GROUP *group, BIGNUM *order, BN_CTX *ctx);

int SSL_UTILS_EC_GROUP_get_cofactor(const EC_GROUP *group, BIGNUM *cofactor, BN_CTX *ctx);

const EC_POINT *SSL_UTILS_EC_KEY_get0_public_key(const EC_KEY *key);

const BIGNUM *SSL_UTILS_EC_KEY_get0_private_key(const EC_KEY *key);

int SSL_UTILS_BN_set_word(BIGNUM *a, BN_ULONG w);

EC_GROUP *SSL_UTILS_EC_GROUP_new_curve_GFp(const BIGNUM *p, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx);

int SSL_UTILS_EC_GROUP_set_generator(
    EC_GROUP *group, const EC_POINT *generator, const BIGNUM *order, const BIGNUM *cofactor);

void SSL_UTILS_BN_CTX_free(BN_CTX *ctx);

int SSL_UTILS_EC_KEY_generate_key(EC_KEY *eckey);

RSA *SSL_UTILS_EVP_PKEY_get1_RSA(EVP_PKEY *pkey);

BIGNUM *SSL_UTILS_BN_dup(const BIGNUM *a);

BN_CTX *SSL_UTILS_BN_CTX_new(void);

int SSL_UTILS_EVP_PKEY_assign(EVP_PKEY *pkey, int type, void *key);

int SSL_UTILS_BN_bn2bin(const BIGNUM *a, unsigned char *to);

void SSL_UTILS_func_dl(JNIEnv *env);

jboolean SSL_UTILS_func_ptr_init(JNIEnv *env, jint useOpensslVersion);

void *open_ssl_lib(JNIEnv *env, jint useOpensslVersion, jboolean *init_result);

void check_openSSL_Engine(JNIEnv *env, jboolean *init_result, char *lib_name);

int get_sslVersion();

int SSL_UTILS_OPENSSL_init_ssl(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings);

int SSL_UTILS_OPENSSL_init_crypto(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings);

int SSL_UTILS_BN_num_bits(const BIGNUM *a);

int SSL_UTILS_ERR_GET_REASON(unsigned long errcode);

int SSL_UTILS_ERR_GET_FUNC(unsigned long errcode);

int SSL_UTILS_ERR_GET_LIB(unsigned long errcode);

void SSL_UTILS_BN_clear_free(BIGNUM *a);

EVP_MD *SSL_UTILS_EVP_sm3(void);

int SSL_UTILS_EVP_PKEY_CTX_set1_id(EVP_PKEY_CTX *ctx, const void *id, int len);

void SSL_UTILS_EVP_MD_CTX_set_pkey_ctx(EVP_MD_CTX *ctx, EVP_PKEY_CTX *pctx);

int SSL_UTILS_EVP_DigestSignInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey);

int SSL_UTILS_EVP_DigestVerifyInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey);

int SSL_UTILS_EVP_DigestSignUpdate(EVP_MD_CTX *ctx, const void *data, size_t dsize);

int SSL_UTILS_EVP_DigestVerifyUpdate(EVP_MD_CTX *ctx, const void *data, size_t dsize);

int SSL_UTILS_EVP_DigestSignFinal(EVP_MD_CTX *ctx, unsigned char *sigret, size_t *siglen);

int SSL_UTILS_EVP_DigestVerifyFinal(EVP_MD_CTX *ctx, const unsigned char *sig, size_t siglen);

EC_KEY *SSL_UTILS_EC_KEY_new_by_curve_name(int nid);

int SSL_UTILS_EC_POINT_mul(const EC_GROUP *group, EC_POINT *r, const BIGNUM *g_scalar, const EC_POINT *point,
    const BIGNUM *p_scalar, BN_CTX *ctx);

int SSL_UTILS_EVP_PKEY_set_alias_type(JNIEnv *env, EVP_PKEY *pkey, int type);

int SSL_UTILS_EVP_PKEY_assign_RSA(EVP_PKEY *pkey, void *key);

int SSL_UTILS_EVP_PKEY_assign_EC_KEY(EVP_PKEY *pkey, void *key);

void SSL_UTILS_EVP_MD_CTX_destroy(EVP_MD_CTX *ctx);

EVP_MD_CTX *SSL_UTILS_EVP_MD_CTX_create(void);

int SSL_UTILS_SSL_load_error_strings();

int SSL_UTILS_OpenSSL_add_all_algorithms();

int SSL_UTILS_BN_num_bytes(const BIGNUM *a);

#endif // SSL_UTILS_H