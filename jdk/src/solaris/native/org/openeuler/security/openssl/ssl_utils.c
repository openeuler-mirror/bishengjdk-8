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

#include <dlfcn.h>
#include <openssl/rsa.h>
#include <string.h>
#include "kae_exception.h"
#include "kae_log.h"
#include "openssl1_macro.h"
#include "openssl3_macro.h"
#include "ssl_utils.h"

typedef char *(*OpenSSL_version_func_t)(int t);
typedef RSA *(*RSA_new_method_func_t)(ENGINE *engine);
typedef int (*RSA_generate_key_ex_func_t)(RSA *rsa, int bits, BIGNUM *e_value, BN_GENCB *cb);
typedef void (*RSA_free_func_t)(RSA *rsa);
typedef int (*OPENSSL_init_ssl_func_t)(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings);
typedef int (*ERR_load_BIO_strings_func_t)(void);
typedef int (*OPENSSL_init_crypto_func_t)(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings);
typedef int (*ENGINE_free_func_t)(ENGINE *e);
typedef ENGINE *(*ENGINE_by_id_func_t)(const char *id);
typedef EVP_MD *(*EVP_get_digestbyname_func_t)(const char *name);
typedef void (*EVP_PKEY_CTX_free_func_t)(EVP_PKEY_CTX *ctx);
typedef int (*EVP_PKEY_CTX_set_rsa_padding_func_t)(EVP_PKEY_CTX *ctx, int pad_mode);
typedef int (*EVP_PKEY_CTX_set_signature_md_func_t)(EVP_PKEY_CTX *ctx, const EVP_MD *md);
typedef EVP_PKEY_CTX *(*EVP_PKEY_CTX_new_func_t)(EVP_PKEY *pkey, ENGINE *e);
typedef int (*EVP_PKEY_sign_init_func_t)(EVP_PKEY_CTX *ctx);
typedef int (*EVP_PKEY_sign_func_t)(
    EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *siglen, const unsigned char *tbs, size_t tbslen);
typedef int (*EVP_PKEY_verify_init_func_t)(EVP_PKEY_CTX *ctx);
typedef int (*EVP_PKEY_verify_func_t)(
    EVP_PKEY_CTX *ctx, const unsigned char *sig, size_t siglen, const unsigned char *tbs, size_t tbslen);
typedef int (*EVP_PKEY_CTX_set_rsa_mgf1_md_func_t)(EVP_PKEY_CTX *ctx, const EVP_MD *md);
typedef int (*EVP_PKEY_CTX_set_rsa_pss_saltlen_func_t)(EVP_PKEY_CTX *ctx, int len);
typedef int (*EVP_PKEY_size_func_t)(const EVP_PKEY *pkey);
typedef EVP_CIPHER *(*EVP_get_cipherbyname_func_t)(const char *name);
typedef EVP_CIPHER_CTX *(*EVP_CIPHER_CTX_new_func_t)(void);
typedef int (*EVP_CipherInit_ex_func_t)(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *type, ENGINE *impl,
    const unsigned char *key, const unsigned char *iv, int enc);
typedef int (*EVP_CIPHER_CTX_set_padding_func_t)(EVP_CIPHER_CTX *ctx, int pad);
typedef void (*EVP_CIPHER_CTX_free_func_t)(EVP_CIPHER_CTX *ctx);
typedef int (*EVP_CipherUpdate_func_t)(
    EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in, int inl);
typedef int (*EVP_CipherFinal_ex_func_t)(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl);
typedef int (*EVP_CIPHER_CTX_ctrl_func_t)(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr);
typedef BIGNUM *(*BN_new_func_t)(void);
typedef BIGNUM *(*BN_bin2bn_func_t)(const unsigned char *s, int len, BIGNUM *ret);
typedef void (*BN_free_func_t)(BIGNUM *a);
typedef int (*EVP_PKEY_CTX_set0_rsa_oaep_label_func_t)(EVP_PKEY_CTX *ctx, void *label, int llen);
typedef int (*EVP_PKEY_CTX_set_rsa_oaep_md_func_t)(EVP_PKEY_CTX *ctx, const EVP_MD *md);
typedef EVP_PKEY *(*EVP_PKEY_new_func_t)(void);
typedef int (*RSA_set0_key_func_t)(RSA *r, BIGNUM *n, BIGNUM *e, BIGNUM *d);
typedef int (*RSA_set0_factors_func_t)(RSA *r, BIGNUM *p, BIGNUM *q);
typedef int (*RSA_set0_crt_params_func_t)(RSA *r, BIGNUM *dmp1, BIGNUM *dmq1, BIGNUM *iqmp);
typedef void (*EVP_PKEY_free_func_t)(EVP_PKEY *x);
typedef int (*RSA_private_encrypt_func_t)(
    int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding);
typedef int (*RSA_private_decrypt_func_t)(
    int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding);
typedef int (*RSA_public_encrypt_func_t)(int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding);
typedef int (*RSA_public_decrypt_func_t)(int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding);
typedef int (*EVP_PKEY_encrypt_init_func_t)(EVP_PKEY_CTX *ctx);
typedef int (*EVP_PKEY_encrypt_func_t)(
    EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen, const unsigned char *in, size_t inlen);
typedef int (*EVP_PKEY_decrypt_init_func_t)(EVP_PKEY_CTX *ctx);
typedef int (*EVP_PKEY_decrypt_func_t)(
    EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen, const unsigned char *in, size_t inlen);
typedef EVP_MD_CTX *(*EVP_MD_CTX_new_func_t)(void);
typedef int (*EVP_DigestInit_ex_func_t)(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl);
typedef void (*EVP_MD_CTX_free_func_t)(EVP_MD_CTX *ctx);
typedef int (*EVP_DigestUpdate_func_t)(EVP_MD_CTX *ctx, const void *data, size_t count);
typedef int (*EVP_DigestFinal_ex_func_t)(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *size);
typedef int (*EVP_MD_CTX_copy_ex_func_t)(EVP_MD_CTX *out, const EVP_MD_CTX *in);
typedef unsigned long (*ERR_get_error_line_data_func_t)(const char **file, int *line, const char **data, int *flags);
typedef void (*ERR_error_string_n_func_t)(unsigned long e, char *buf, size_t len);
typedef void (*ERR_clear_error_func_t)(void);
typedef HMAC_CTX *(*HMAC_CTX_new_func_t)(void);
typedef int (*HMAC_Init_ex_func_t)(HMAC_CTX *ctx, const void *key, int len, const EVP_MD *md, ENGINE *impl);
typedef void (*HMAC_CTX_free_func_t)(HMAC_CTX *ctx);
typedef int (*HMAC_Update_func_t)(HMAC_CTX *ctx, const unsigned char *data, size_t len);
typedef int (*HMAC_Final_func_t)(HMAC_CTX *ctx, unsigned char *md, unsigned int *len);
typedef DH *(*DH_new_method_func_t)(ENGINE *engine);
typedef int (*DH_set0_pqg_func_t)(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g);
typedef int (*DH_set0_key_func_t)(DH *dh, BIGNUM *pub_key, BIGNUM *priv_key);
typedef int (*DH_compute_key_func_t)(unsigned char *key, const BIGNUM *pub_key, DH *dh);
typedef void (*DH_free_func_t)(DH *r);
typedef void (*EC_POINT_free_func_t)(EC_POINT *point);
typedef void (*EC_KEY_free_func_t)(EC_KEY *r);
typedef void (*EC_GROUP_free_func_t)(EC_GROUP *group);
typedef int (*OBJ_sn2nid_func_t)(const char *s);
typedef EC_GROUP *(*EC_GROUP_new_by_curve_name_func_t)(int nid);
typedef EC_KEY *(*EC_KEY_new_func_t)(void);
typedef int (*EC_KEY_set_group_func_t)(EC_KEY *key, const EC_GROUP *group);
typedef EC_POINT *(*EC_POINT_new_func_t)(const EC_GROUP *group);
typedef int (*EC_POINT_set_affine_coordinates_GFp_func_t)(
    const EC_GROUP *group, EC_POINT *point, const BIGNUM *x, const BIGNUM *y, BN_CTX *ctx);
typedef int (*EC_KEY_set_public_key_func_t)(EC_KEY *key, const EC_POINT *pub_key);
typedef int (*EC_KEY_set_private_key_func_t)(EC_KEY *key, const BIGNUM *priv_key);
typedef int (*EC_GROUP_get_degree_func_t)(const EC_GROUP *group);
typedef int (*ECDH_compute_key_func_t)(void *out, size_t outlen, const EC_POINT *pub_key, const EC_KEY *eckey,
    void *(*KDF)(const void *in, size_t inlen, void *out, size_t *outlen));
typedef int (*DH_set_length_func_t)(DH *dh, long length);
typedef int (*DH_generate_key_func_t)(DH *dh);
typedef BIGNUM *(*DH_get0_priv_key_func_t)(const DH *dh);
typedef BIGNUM *(*DH_get0_pub_key_func_t)(const DH *dh);
typedef int (*EC_GROUP_get_curve_GFp_func_t)(const EC_GROUP *group, BIGNUM *p, BIGNUM *a, BIGNUM *b, BN_CTX *ctx);
typedef EC_POINT *(*EC_GROUP_get0_generator_func_t)(const EC_GROUP *group);
typedef int (*EC_POINT_get_affine_coordinates_GFp_func_t)(
    const EC_GROUP *group, const EC_POINT *point, BIGNUM *x, BIGNUM *y, BN_CTX *ctx);
typedef int (*EC_GROUP_get_order_func_t)(const EC_GROUP *group, BIGNUM *order, BN_CTX *ctx);
typedef int (*EC_GROUP_get_cofactor_func_t)(const EC_GROUP *group, BIGNUM *cofactor, BN_CTX *ctx);
typedef EC_POINT *(*EC_KEY_get0_public_key_func_t)(const EC_KEY *key);
typedef BIGNUM *(*EC_KEY_get0_private_key_func_t)(const EC_KEY *key);
typedef int (*BN_set_word_func_t)(BIGNUM *a, BN_ULONG w);
typedef EC_GROUP *(*EC_GROUP_new_curve_GFp_func_t)(const BIGNUM *p, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx);
typedef int (*EC_GROUP_set_generator_func_t)(
    EC_GROUP *group, const EC_POINT *generator, const BIGNUM *order, const BIGNUM *cofactor);
typedef void (*BN_CTX_free_func_t)(BN_CTX *ctx);
typedef int (*EC_KEY_generate_key_func_t)(EC_KEY *eckey);
typedef RSA *(*EVP_PKEY_get1_RSA_func_t)(EVP_PKEY *pkey);
typedef BIGNUM *(*BN_dup_func_t)(const BIGNUM *a);
typedef BN_CTX *(*BN_CTX_new_func_t)(void);
typedef int (*EVP_PKEY_assign_func_t)(EVP_PKEY *pkey, int type, void *key);
typedef int (*BN_bn2bin_func_t)(const BIGNUM *a, unsigned char *to);
typedef const BIGNUM *(*RSA_get0_n_func_t)(const RSA *r);
typedef const BIGNUM *(*RSA_get0_e_func_t)(const RSA *r);
typedef const BIGNUM *(*RSA_get0_d_func_t)(const RSA *r);
typedef const BIGNUM *(*RSA_get0_p_func_t)(const RSA *r);
typedef const BIGNUM *(*RSA_get0_q_func_t)(const RSA *r);
typedef const BIGNUM *(*RSA_get0_dmp1_func_t)(const RSA *r);
typedef const BIGNUM *(*RSA_get0_dmq1_func_t)(const RSA *r);
typedef const BIGNUM *(*RSA_get0_iqmp_func_t)(const RSA *r);
typedef int (*EVP_PKEY_get_size_func_t)(const EVP_PKEY *pkey);
typedef int (*RSA_pkey_ctx_ctrl_func_t)(EVP_PKEY_CTX *ctx, int optype, int cmd, int p1, void *p2);
typedef int (*EVP_PKEY_CTX_ctrl_func_t)(EVP_PKEY_CTX *ctx, int keytype, int optype, int cmd, int p1, void *p2);
typedef int (*BN_num_bits_func_t)(const BIGNUM *a);
typedef int (*ERR_GET_LIB_func_t)(unsigned long errcode);
typedef int (*ERR_GET_REASON_func_t)(unsigned long errcode);
typedef void (*BN_clear_free_func_t)(BIGNUM *a);
typedef EVP_MD *(*EVP_sm3_func_t)(void);
typedef int (*EVP_PKEY_CTX_set1_id_func_t)(EVP_PKEY_CTX *ctx, const void *id, int len);
typedef void (*EVP_MD_CTX_set_pkey_ctx_func_t)(EVP_MD_CTX *ctx, EVP_PKEY_CTX *pctx);
typedef int (*EVP_DigestSignInit_func_t)(
    EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey);
typedef int (*EVP_DigestVerifyInit_func_t)(
    EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey);
typedef int (*EVP_DigestSignUpdate_func_t)(EVP_MD_CTX *ctx, const void *data, size_t dsize);
typedef int (*EVP_DigestVerifyUpdate_func_t)(EVP_MD_CTX *ctx, const void *data, size_t dsize);
typedef int (*EVP_DigestSignFinal_func_t)(EVP_MD_CTX *ctx, unsigned char *sigret, size_t *siglen);
typedef int (*EVP_DigestVerifyFinal_func_t)(EVP_MD_CTX *ctx, const unsigned char *sig, size_t siglen);
typedef EC_KEY *(*EC_KEY_new_by_curve_name_func_t)(int nid);
typedef int (*EC_POINT_mul_func_t)(const EC_GROUP *group, EC_POINT *r, const BIGNUM *g_scalar, const EC_POINT *point,
    const BIGNUM *p_scalar, BN_CTX *ctx);
typedef int (*EVP_PKEY_set_alias_type_func_t)(EVP_PKEY *pkey, int type);

static void *_lib_handle;
static int _sslVersion = 0;
const int V1 = 1;
const int V3 = 3;
const char *OPENSSL_VERSION_1_1 = "OpenSSL 1.1.";
const char *OPENSSL_VERSION_3_X = "OpenSSL 3.";
const char *OPENSSL_ENGINES_VERSION_1_1 = "/usr/local/lib/engines-1.";
const char *OPENSSL_ENGINES_VERSION_3_X = "/usr/local/lib/engines-3.";

static OpenSSL_version_func_t _OpenSSL_version;
static RSA_new_method_func_t _RSA_new_method;
static RSA_generate_key_ex_func_t _RSA_generate_key_ex;
static RSA_free_func_t _RSA_free;
static OPENSSL_init_ssl_func_t _OPENSSL_init_ssl;
static ERR_load_BIO_strings_func_t _ERR_load_BIO_strings;
static OPENSSL_init_crypto_func_t _OPENSSL_init_crypto;
static ENGINE_free_func_t _ENGINE_free;
static ENGINE_by_id_func_t _ENGINE_by_id;
static EVP_get_digestbyname_func_t _EVP_get_digestbyname;
static EVP_PKEY_CTX_free_func_t _EVP_PKEY_CTX_free;
static EVP_PKEY_CTX_set_rsa_padding_func_t _EVP_PKEY_CTX_set_rsa_padding;
static EVP_PKEY_CTX_set_signature_md_func_t _EVP_PKEY_CTX_set_signature_md;
static EVP_PKEY_CTX_new_func_t _EVP_PKEY_CTX_new;
static EVP_PKEY_sign_init_func_t _EVP_PKEY_sign_init;
static EVP_PKEY_sign_func_t _EVP_PKEY_sign;
static EVP_PKEY_verify_init_func_t _EVP_PKEY_verify_init;
static EVP_PKEY_verify_func_t _EVP_PKEY_verify;
static EVP_PKEY_CTX_set_rsa_mgf1_md_func_t _EVP_PKEY_CTX_set_rsa_mgf1_md;
static EVP_PKEY_CTX_set_rsa_pss_saltlen_func_t _EVP_PKEY_CTX_set_rsa_pss_saltlen;
static EVP_PKEY_size_func_t _EVP_PKEY_size;
static EVP_get_cipherbyname_func_t _EVP_get_cipherbyname;
static EVP_CIPHER_CTX_new_func_t _EVP_CIPHER_CTX_new;
static EVP_CipherInit_ex_func_t _EVP_CipherInit_ex;
static EVP_CIPHER_CTX_set_padding_func_t _EVP_CIPHER_CTX_set_padding;
static EVP_CIPHER_CTX_free_func_t _EVP_CIPHER_CTX_free;
static EVP_CipherUpdate_func_t _EVP_CipherUpdate;
static EVP_CipherFinal_ex_func_t _EVP_CipherFinal_ex;
static EVP_CIPHER_CTX_ctrl_func_t _EVP_CIPHER_CTX_ctrl;
static BN_new_func_t _BN_new;
static BN_bin2bn_func_t _BN_bin2bn;
static BN_free_func_t _BN_free;
static EVP_PKEY_CTX_set0_rsa_oaep_label_func_t _EVP_PKEY_CTX_set0_rsa_oaep_label;
static EVP_PKEY_CTX_set_rsa_oaep_md_func_t _EVP_PKEY_CTX_set_rsa_oaep_md;
static EVP_PKEY_new_func_t _EVP_PKEY_new;
static RSA_set0_key_func_t _RSA_set0_key;
static RSA_set0_factors_func_t _RSA_set0_factors;
static RSA_set0_crt_params_func_t _RSA_set0_crt_params;
static EVP_PKEY_free_func_t _EVP_PKEY_free;
static RSA_private_encrypt_func_t _RSA_private_encrypt;
static RSA_private_decrypt_func_t _RSA_private_decrypt;
static RSA_public_encrypt_func_t _RSA_public_encrypt;
static RSA_public_decrypt_func_t _RSA_public_decrypt;
static EVP_PKEY_encrypt_init_func_t _EVP_PKEY_encrypt_init;
static EVP_PKEY_encrypt_func_t _EVP_PKEY_encrypt;
static EVP_PKEY_decrypt_init_func_t _EVP_PKEY_decrypt_init;
static EVP_PKEY_decrypt_func_t _EVP_PKEY_decrypt;
static EVP_MD_CTX_new_func_t _EVP_MD_CTX_new;
static EVP_DigestInit_ex_func_t _EVP_DigestInit_ex;
static EVP_MD_CTX_free_func_t _EVP_MD_CTX_free;
static EVP_DigestUpdate_func_t _EVP_DigestUpdate;
static EVP_DigestFinal_ex_func_t _EVP_DigestFinal_ex;
static EVP_MD_CTX_copy_ex_func_t _EVP_MD_CTX_copy_ex;
static ERR_get_error_line_data_func_t _ERR_get_error_line_data;
static ERR_error_string_n_func_t _ERR_error_string_n;
static ERR_clear_error_func_t _ERR_clear_error;
static HMAC_CTX_new_func_t _HMAC_CTX_new;
static HMAC_Init_ex_func_t _HMAC_Init_ex;
static HMAC_CTX_free_func_t _HMAC_CTX_free;
static HMAC_Update_func_t _HMAC_Update;
static HMAC_Final_func_t _HMAC_Final;
static DH_new_method_func_t _DH_new_method;
static DH_set0_pqg_func_t _DH_set0_pqg;
static DH_set0_key_func_t _DH_set0_key;
static DH_compute_key_func_t _DH_compute_key;
static DH_free_func_t _DH_free;
static EC_POINT_free_func_t _EC_POINT_free;
static EC_KEY_free_func_t _EC_KEY_free;
static EC_GROUP_free_func_t _EC_GROUP_free;
static OBJ_sn2nid_func_t _OBJ_sn2nid;
static EC_GROUP_new_by_curve_name_func_t _EC_GROUP_new_by_curve_name;
static EC_KEY_new_func_t _EC_KEY_new;
static EC_KEY_set_group_func_t _EC_KEY_set_group;
static EC_POINT_new_func_t _EC_POINT_new;
static EC_POINT_set_affine_coordinates_GFp_func_t _EC_POINT_set_affine_coordinates_GFp;
static EC_KEY_set_public_key_func_t _EC_KEY_set_public_key;
static EC_KEY_set_private_key_func_t _EC_KEY_set_private_key;
static EC_GROUP_get_degree_func_t _EC_GROUP_get_degree;
static ECDH_compute_key_func_t _ECDH_compute_key;
static DH_set_length_func_t _DH_set_length;
static DH_generate_key_func_t _DH_generate_key;
static DH_get0_priv_key_func_t _DH_get0_priv_key;
static DH_get0_pub_key_func_t _DH_get0_pub_key;
static EC_GROUP_get_curve_GFp_func_t _EC_GROUP_get_curve_GFp;
static EC_GROUP_get0_generator_func_t _EC_GROUP_get0_generator;
static EC_POINT_get_affine_coordinates_GFp_func_t _EC_POINT_get_affine_coordinates_GFp;
static EC_GROUP_get_order_func_t _EC_GROUP_get_order;
static EC_GROUP_get_cofactor_func_t _EC_GROUP_get_cofactor;
static EC_KEY_get0_public_key_func_t _EC_KEY_get0_public_key;
static EC_KEY_get0_private_key_func_t _EC_KEY_get0_private_key;
static BN_set_word_func_t _BN_set_word;
static EC_GROUP_new_curve_GFp_func_t _EC_GROUP_new_curve_GFp;
static EC_GROUP_set_generator_func_t _EC_GROUP_set_generator;
static BN_CTX_free_func_t _BN_CTX_free;
static EC_KEY_generate_key_func_t _EC_KEY_generate_key;
static EVP_PKEY_get1_RSA_func_t _EVP_PKEY_get1_RSA;
static BN_dup_func_t _BN_dup;
static BN_CTX_new_func_t _BN_CTX_new;
static EVP_PKEY_assign_func_t _EVP_PKEY_assign;
static BN_bn2bin_func_t _BN_bn2bin;
static RSA_get0_n_func_t _RSA_get0_n;
static RSA_get0_e_func_t _RSA_get0_e;
static RSA_get0_d_func_t _RSA_get0_d;
static RSA_get0_p_func_t _RSA_get0_p;
static RSA_get0_q_func_t _RSA_get0_q;
static RSA_get0_dmp1_func_t _RSA_get0_dmp1;
static RSA_get0_dmq1_func_t _RSA_get0_dmq1;
static RSA_get0_iqmp_func_t _RSA_get0_iqmp;
static EVP_PKEY_get_size_func_t _EVP_PKEY_get_size;
static RSA_pkey_ctx_ctrl_func_t _RSA_pkey_ctx_ctrl;
static EVP_PKEY_CTX_ctrl_func_t _EVP_PKEY_CTX_ctrl;
static BN_num_bits_func_t _BN_num_bits;
static ERR_GET_LIB_func_t _ERR_GET_LIB;
static ERR_GET_REASON_func_t _ERR_GET_REASON;
static BN_clear_free_func_t _BN_clear_free;
static EVP_sm3_func_t _EVP_sm3;
static EVP_PKEY_CTX_set1_id_func_t _EVP_PKEY_CTX_set1_id;
static EVP_MD_CTX_set_pkey_ctx_func_t _EVP_MD_CTX_set_pkey_ctx;
static EVP_DigestSignInit_func_t _EVP_DigestSignInit;
static EVP_DigestVerifyInit_func_t _EVP_DigestVerifyInit;
static EVP_DigestSignUpdate_func_t _EVP_DigestSignUpdate;
static EVP_DigestVerifyUpdate_func_t _EVP_DigestVerifyUpdate;
static EVP_DigestSignFinal_func_t _EVP_DigestSignFinal;
static EVP_DigestVerifyFinal_func_t _EVP_DigestVerifyFinal;
static EC_KEY_new_by_curve_name_func_t _EC_KEY_new_by_curve_name;
static EC_POINT_mul_func_t _EC_POINT_mul;
static EVP_PKEY_set_alias_type_func_t _EVP_PKEY_set_alias_type;

const int COMMON_FUNC_START_INDEX = 0;
const int COMMON_FUNC_END_INDEX = 111;
const int V1_FUNC_START_INDEX = 112;
const int V1_FUNC_END_INDEX = 113;
const int V3_FUNC_START_INDEX = 114;
const int V3_FUNC_END_INDEX = 123;

const char *SSL_UTILS_OpenSSL_version(int t)
{
    return (*_OpenSSL_version)(t);
}
const BIGNUM *SSL_UTILS_RSA_get0_n(const RSA *r)
{
    return (*_RSA_get0_n)(r);
}

const BIGNUM *SSL_UTILS_RSA_get0_e(const RSA *r)
{
    return (*_RSA_get0_e)(r);
}

const BIGNUM *SSL_UTILS_RSA_get0_d(const RSA *r)
{
    return (*_RSA_get0_d)(r);
}

const BIGNUM *SSL_UTILS_RSA_get0_p(const RSA *r)
{
    return (*_RSA_get0_p)(r);
}

const BIGNUM *SSL_UTILS_RSA_get0_q(const RSA *r)
{
    return (*_RSA_get0_q)(r);
}

const BIGNUM *SSL_UTILS_RSA_get0_dmp1(const RSA *r)
{
    return (*_RSA_get0_dmp1)(r);
}

const BIGNUM *SSL_UTILS_RSA_get0_dmq1(const RSA *r)
{
    return (*_RSA_get0_dmq1)(r);
}

const BIGNUM *SSL_UTILS_RSA_get0_iqmp(const RSA *r)
{
    return (*_RSA_get0_iqmp)(r);
}

RSA *SSL_UTILS_RSA_new_method(ENGINE *engine)
{
    return (*_RSA_new_method)(engine);
}

int SSL_UTILS_RSA_generate_key_ex(RSA *rsa, int bits, BIGNUM *e_value, BN_GENCB *cb)
{
    return (*_RSA_generate_key_ex)(rsa, bits, e_value, cb);
}

void SSL_UTILS_RSA_free(RSA *rsa)
{
    (*_RSA_free)(rsa);
}

int SSL_UTILS_OPENSSL_init_ssl(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings)
{
    return (*_OPENSSL_init_ssl)(opts, settings);
}

int SSL_UTILS_ERR_load_BIO_strings()
{
    return (*_ERR_load_BIO_strings)();
}

int SSL_UTILS_OPENSSL_init_crypto(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings)
{
    return (*_OPENSSL_init_crypto)(opts, settings);
}

int SSL_UTILS_ENGINE_free(ENGINE *e)
{
    return (*_ENGINE_free)(e);
}

ENGINE *SSL_UTILS_ENGINE_by_id(const char *id)
{
    return (*_ENGINE_by_id)(id);
}

EVP_MD *SSL_UTILS_EVP_get_digestbyname(const char *name)
{
    return (*_EVP_get_digestbyname)(name);
}

void SSL_UTILS_EVP_PKEY_CTX_free(EVP_PKEY_CTX *ctx)
{
    (*_EVP_PKEY_CTX_free)(ctx);
}

int SSL_UTILS_EVP_PKEY_CTX_set_rsa_padding(EVP_PKEY_CTX *ctx, int pad_mode)
{
    // EVP_PKEY_CTX_set_rsa_padding is macro in openssl 1
    if (get_sslVersion() == V1) {
        KAE_TRACE("SSL_UTILS_EVP_PKEY_CTX_set_rsa_padding, openssl version is 1");
        return (*_RSA_pkey_ctx_ctrl)(ctx, -1, SSL1_EVP_PKEY_CTRL_RSA_PADDING, pad_mode, NULL);
    }
    return (*_EVP_PKEY_CTX_set_rsa_padding)(ctx, pad_mode);
}

int SSL_UTILS_EVP_PKEY_CTX_set_signature_md(EVP_PKEY_CTX *ctx, const EVP_MD *md)
{
    // EVP_PKEY_CTX_set_signature_md is macro in openssl 1
    if (get_sslVersion() == V1) {
        return (*_EVP_PKEY_CTX_ctrl)(ctx, -1, SSL1_EVP_PKEY_OP_TYPE_SIG, SSL1_EVP_PKEY_CTRL_MD, 0, (void *)(md));
    }
    return (*_EVP_PKEY_CTX_set_signature_md)(ctx, md);
}

EVP_PKEY_CTX *SSL_UTILS_EVP_PKEY_CTX_new(EVP_PKEY *pkey, ENGINE *e)
{
    return (*_EVP_PKEY_CTX_new)(pkey, e);
}

int SSL_UTILS_EVP_PKEY_sign_init(EVP_PKEY_CTX *ctx)
{
    return (*_EVP_PKEY_sign_init)(ctx);
}

int SSL_UTILS_EVP_PKEY_sign(
    EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *siglen, const unsigned char *tbs, size_t tbslen)
{
    return (*_EVP_PKEY_sign)(ctx, sig, siglen, tbs, tbslen);
}

int SSL_UTILS_EVP_PKEY_verify_init(EVP_PKEY_CTX *ctx)
{
    return (*_EVP_PKEY_verify_init)(ctx);
}

int SSL_UTILS_EVP_PKEY_verify(
    EVP_PKEY_CTX *ctx, const unsigned char *sig, size_t siglen, const unsigned char *tbs, size_t tbslen)
{
    return (*_EVP_PKEY_verify)(ctx, sig, siglen, tbs, tbslen);
}

int SSL_UTILS_EVP_PKEY_CTX_set_rsa_mgf1_md(EVP_PKEY_CTX *ctx, const EVP_MD *md)
{
    // RSA_pkey_ctx_ctrl is macro in openssl 1
    if (get_sslVersion() == V1) {
        return (*_RSA_pkey_ctx_ctrl)(ctx,
            SSL1_EVP_PKEY_OP_TYPE_SIG | SSL1_EVP_PKEY_OP_TYPE_CRYPT,
            SSL1_EVP_PKEY_CTRL_RSA_MGF1_MD,
            0,
            (void *)(md));
    }
    return (*_EVP_PKEY_CTX_set_rsa_mgf1_md)(ctx, md);
}

int SSL_UTILS_EVP_PKEY_CTX_set_rsa_pss_saltlen(EVP_PKEY_CTX *ctx, int len)
{
    // EVP_PKEY_CTX_set_rsa_pss_saltlen is macro in openssl 1
    if (get_sslVersion() == V1) {
        return (*_RSA_pkey_ctx_ctrl)(
            ctx, (SSL1_EVP_PKEY_OP_SIGN | SSL1_EVP_PKEY_OP_VERIFY), SSL1_EVP_PKEY_CTRL_RSA_PSS_SALTLEN, len, NULL);
    }
    return (*_EVP_PKEY_CTX_set_rsa_pss_saltlen)(ctx, len);
}

int SSL_UTILS_EVP_PKEY_size(const EVP_PKEY *pkey)
{
    // EVP_PKEY_size is macro in openssl 3
    if (get_sslVersion() == V3) {
        return (*_EVP_PKEY_get_size)(pkey);
    }
    return (*_EVP_PKEY_size)(pkey);
}

EVP_CIPHER *SSL_UTILS_EVP_get_cipherbyname(const char *name)
{
    return (*_EVP_get_cipherbyname)(name);
}

EVP_CIPHER_CTX *SSL_UTILS_EVP_CIPHER_CTX_new(void)
{
    return (*_EVP_CIPHER_CTX_new)();
}

int SSL_UTILS_EVP_CipherInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *type, ENGINE *impl, const unsigned char *key,
    const unsigned char *iv, int enc)
{
    return (*_EVP_CipherInit_ex)(ctx, type, impl, key, iv, enc);
}

int SSL_UTILS_EVP_CIPHER_CTX_set_padding(EVP_CIPHER_CTX *ctx, int pad)
{
    return (*_EVP_CIPHER_CTX_set_padding)(ctx, pad);
}

void SSL_UTILS_EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *ctx)
{
    (*_EVP_CIPHER_CTX_free)(ctx);
}

int SSL_UTILS_EVP_CipherUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in, int inl)
{
    return (*_EVP_CipherUpdate)(ctx, out, outl, in, inl);
}

int SSL_UTILS_EVP_CipherFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
{
    return (*_EVP_CipherFinal_ex)(ctx, out, outl);
}
int SSL_UTILS_EVP_CIPHER_CTX_ctrl(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr)
{
    return (*_EVP_CIPHER_CTX_ctrl)(ctx, type, arg, ptr);
}
BIGNUM *SSL_UTILS_BN_new(void)
{
    return (*_BN_new)();
}
BIGNUM *SSL_UTILS_BN_bin2bn(const unsigned char *s, int len, BIGNUM *ret)
{
    return (*_BN_bin2bn)(s, len, ret);
}
void SSL_UTILS_BN_free(BIGNUM *a)
{
    (*_BN_free)(a);
}

int SSL_UTILS_EVP_PKEY_CTX_set0_rsa_oaep_label(EVP_PKEY_CTX *ctx, void *label, int llen)
{
    // EVP_PKEY_CTX_set0_rsa_oaep_label is macro in openssl 1
    if (get_sslVersion() == V1) {
        return (*_EVP_PKEY_CTX_ctrl)(ctx,
            SSL1_EVP_PKEY_RSA,
            SSL1_EVP_PKEY_OP_TYPE_CRYPT,
            SSL1_EVP_PKEY_CTRL_RSA_OAEP_LABEL,
            llen,
            (void *)(label));
    }
    return (*_EVP_PKEY_CTX_set0_rsa_oaep_label)(ctx, label, llen);
}

int SSL_UTILS_EVP_PKEY_CTX_set_rsa_oaep_md(EVP_PKEY_CTX *ctx, const EVP_MD *md)
{
    // EVP_PKEY_CTX_set_rsa_oaep_md is macro in openssl 1
    if (get_sslVersion() == V1) {
        return (*_EVP_PKEY_CTX_ctrl)(
            ctx, SSL1_EVP_PKEY_RSA, SSL1_EVP_PKEY_OP_TYPE_CRYPT, SSL1_EVP_PKEY_CTRL_RSA_OAEP_MD, 0, (void *)(md));
    }
    return (*_EVP_PKEY_CTX_set_rsa_oaep_md)(ctx, md);
}
EVP_PKEY *SSL_UTILS_EVP_PKEY_new(void)
{
    return (*_EVP_PKEY_new)();
}
int SSL_UTILS_RSA_set0_key(RSA *r, BIGNUM *n, BIGNUM *e, BIGNUM *d)
{
    return (*_RSA_set0_key)(r, n, e, d);
}
int SSL_UTILS_RSA_set0_factors(RSA *r, BIGNUM *p, BIGNUM *q)
{
    return (*_RSA_set0_factors)(r, p, q);
}
int SSL_UTILS_RSA_set0_crt_params(RSA *r, BIGNUM *dmp1, BIGNUM *dmq1, BIGNUM *iqmp)
{
    return (*_RSA_set0_crt_params)(r, dmp1, dmq1, iqmp);
}
void SSL_UTILS_EVP_PKEY_free(EVP_PKEY *x)
{
    (*_EVP_PKEY_free)(x);
}
int SSL_UTILS_RSA_private_encrypt(int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding)
{
    return (*_RSA_private_encrypt)(flen, from, to, rsa, padding);
}
int SSL_UTILS_RSA_private_decrypt(int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding)
{
    return (*_RSA_private_decrypt)(flen, from, to, rsa, padding);
}
int SSL_UTILS_RSA_public_encrypt(int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding)
{
    return (*_RSA_public_encrypt)(flen, from, to, rsa, padding);
}
int SSL_UTILS_RSA_public_decrypt(int flen, const unsigned char *from, unsigned char *to, RSA *rsa, int padding)
{
    return (*_RSA_public_decrypt)(flen, from, to, rsa, padding);
}
int SSL_UTILS_EVP_PKEY_encrypt_init(EVP_PKEY_CTX *ctx)
{
    return (*_EVP_PKEY_encrypt_init)(ctx);
}
int SSL_UTILS_EVP_PKEY_encrypt(
    EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen, const unsigned char *in, size_t inlen)
{
    return (*_EVP_PKEY_encrypt)(ctx, out, outlen, in, inlen);
}
int SSL_UTILS_EVP_PKEY_decrypt_init(EVP_PKEY_CTX *ctx)
{
    return (*_EVP_PKEY_decrypt_init)(ctx);
}
int SSL_UTILS_EVP_PKEY_decrypt(
    EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen, const unsigned char *in, size_t inlen)
{
    return (*_EVP_PKEY_decrypt)(ctx, out, outlen, in, inlen);
}
EVP_MD_CTX *SSL_UTILS_EVP_MD_CTX_new(void)
{
    return (*_EVP_MD_CTX_new)();
}
int SSL_UTILS_EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl)
{
    return (*_EVP_DigestInit_ex)(ctx, type, impl);
}
void SSL_UTILS_EVP_MD_CTX_free(EVP_MD_CTX *ctx)
{
    return (*_EVP_MD_CTX_free)(ctx);
}
int SSL_UTILS_EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *data, size_t count)
{
    return (*_EVP_DigestUpdate)(ctx, data, count);
}
int SSL_UTILS_EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *size)
{
    return (*_EVP_DigestFinal_ex)(ctx, md, size);
}
int SSL_UTILS_EVP_MD_CTX_copy_ex(EVP_MD_CTX *out, const EVP_MD_CTX *in)
{
    return (*_EVP_MD_CTX_copy_ex)(out, in);
}
unsigned long SSL_UTILS_ERR_get_error_line_data(const char **file, int *line, const char **data, int *flags)
{
    return (*_ERR_get_error_line_data)(file, line, data, flags);
}
void SSL_UTILS_ERR_error_string_n(unsigned long e, char *buf, size_t len)
{
    (*_ERR_error_string_n)(e, buf, len);
}
void SSL_UTILS_ERR_clear_error(void)
{
    (*_ERR_clear_error)();
}
HMAC_CTX *SSL_UTILS_HMAC_CTX_new(void)
{
    return (*_HMAC_CTX_new)();
}
int SSL_UTILS_HMAC_Init_ex(HMAC_CTX *ctx, const void *key, int len, const EVP_MD *md, ENGINE *impl)
{
    return (*_HMAC_Init_ex)(ctx, key, len, md, impl);
}
void SSL_UTILS_HMAC_CTX_free(HMAC_CTX *ctx)
{
    (*_HMAC_CTX_free)(ctx);
}
int SSL_UTILS_HMAC_Update(HMAC_CTX *ctx, const unsigned char *data, size_t len)
{
    return (*_HMAC_Update)(ctx, data, len);
}
int SSL_UTILS_HMAC_Final(HMAC_CTX *ctx, unsigned char *md, unsigned int *len)
{
    return (*_HMAC_Final)(ctx, md, len);
}
DH *SSL_UTILS_DH_new_method(ENGINE *engine)
{
    return (*_DH_new_method)(engine);
}
int SSL_UTILS_DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g)
{
    return (*_DH_set0_pqg)(dh, p, q, g);
}
int SSL_UTILS_DH_set0_key(DH *dh, BIGNUM *pub_key, BIGNUM *priv_key)
{
    return (*_DH_set0_key)(dh, pub_key, priv_key);
}
int SSL_UTILS_DH_compute_key(unsigned char *key, const BIGNUM *pub_key, DH *dh)
{
    return (*_DH_compute_key)(key, pub_key, dh);
}
void SSL_UTILS_DH_free(DH *r)
{
    (*_DH_free)(r);
}
void SSL_UTILS_EC_POINT_free(EC_POINT *point)
{
    (*_EC_POINT_free)(point);
}
void SSL_UTILS_EC_KEY_free(EC_KEY *r)
{
    (*_EC_KEY_free)(r);
}
void SSL_UTILS_EC_GROUP_free(EC_GROUP *group)
{
    (*_EC_GROUP_free)(group);
}
int SSL_UTILS_OBJ_sn2nid(const char *s)
{
    return (*_OBJ_sn2nid)(s);
}
EC_GROUP *SSL_UTILS_EC_GROUP_new_by_curve_name(int nid)
{
    return (*_EC_GROUP_new_by_curve_name)(nid);
}
EC_KEY *SSL_UTILS_EC_KEY_new(void)
{
    return (*_EC_KEY_new)();
}
int SSL_UTILS_EC_KEY_set_group(EC_KEY *key, const EC_GROUP *group)
{
    return (*_EC_KEY_set_group)(key, group);
}
EC_POINT *SSL_UTILS_EC_POINT_new(const EC_GROUP *group)
{
    return (*_EC_POINT_new)(group);
}
int SSL_UTILS_EC_POINT_set_affine_coordinates_GFp(
    const EC_GROUP *group, EC_POINT *point, const BIGNUM *x, const BIGNUM *y, BN_CTX *ctx)
{
    return (*_EC_POINT_set_affine_coordinates_GFp)(group, point, x, y, ctx);
}
int SSL_UTILS_EC_KEY_set_public_key(EC_KEY *key, const EC_POINT *pub_key)
{
    return (*_EC_KEY_set_public_key)(key, pub_key);
}
int SSL_UTILS_EC_KEY_set_private_key(EC_KEY *key, const BIGNUM *priv_key)
{
    return (*_EC_KEY_set_private_key)(key, priv_key);
}
int SSL_UTILS_EC_GROUP_get_degree(const EC_GROUP *group)
{
    return (*_EC_GROUP_get_degree)(group);
}

int SSL_UTILS_ECDH_compute_key(void *out, size_t outlen, const EC_POINT *pub_key, const EC_KEY *eckey,
    void *(*KDF)(const void *in, size_t inlen, void *out, size_t *outlen))
{
    return (*_ECDH_compute_key)(out, outlen, pub_key, eckey, KDF);
}
int SSL_UTILS_DH_set_length(DH *dh, long length)
{
    return (*_DH_set_length)(dh, length);
}
int SSL_UTILS_DH_generate_key(DH *dh)
{
    return (*_DH_generate_key)(dh);
}
const BIGNUM *SSL_UTILS_DH_get0_priv_key(const DH *dh)
{
    return (*_DH_get0_priv_key)(dh);
}
const BIGNUM *SSL_UTILS_DH_get0_pub_key(const DH *dh)
{
    return (*_DH_get0_pub_key)(dh);
}
int SSL_UTILS_EC_GROUP_get_curve_GFp(const EC_GROUP *group, BIGNUM *p, BIGNUM *a, BIGNUM *b, BN_CTX *ctx)
{
    return (*_EC_GROUP_get_curve_GFp)(group, p, a, b, ctx);
}
const EC_POINT *SSL_UTILS_EC_GROUP_get0_generator(const EC_GROUP *group)
{
    return (*_EC_GROUP_get0_generator)(group);
}
int SSL_UTILS_EC_POINT_get_affine_coordinates_GFp(
    const EC_GROUP *group, const EC_POINT *point, BIGNUM *x, BIGNUM *y, BN_CTX *ctx)
{
    return (*_EC_POINT_get_affine_coordinates_GFp)(group, point, x, y, ctx);
}
int SSL_UTILS_EC_GROUP_get_order(const EC_GROUP *group, BIGNUM *order, BN_CTX *ctx)
{
    return (*_EC_GROUP_get_order)(group, order, ctx);
}
int SSL_UTILS_EC_GROUP_get_cofactor(const EC_GROUP *group, BIGNUM *cofactor, BN_CTX *ctx)
{
    return (*_EC_GROUP_get_cofactor)(group, cofactor, ctx);
}
const EC_POINT *SSL_UTILS_EC_KEY_get0_public_key(const EC_KEY *key)
{
    return (*_EC_KEY_get0_public_key)(key);
}
const BIGNUM *SSL_UTILS_EC_KEY_get0_private_key(const EC_KEY *key)
{
    return (*_EC_KEY_get0_private_key)(key);
}
int SSL_UTILS_BN_set_word(BIGNUM *a, BN_ULONG w)
{
    return (*_BN_set_word)(a, w);
}
EC_GROUP *SSL_UTILS_EC_GROUP_new_curve_GFp(const BIGNUM *p, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx)
{
    return (*_EC_GROUP_new_curve_GFp)(p, a, b, ctx);
}
int SSL_UTILS_EC_GROUP_set_generator(
    EC_GROUP *group, const EC_POINT *generator, const BIGNUM *order, const BIGNUM *cofactor)
{
    return (*_EC_GROUP_set_generator)(group, generator, order, cofactor);
}
void SSL_UTILS_BN_CTX_free(BN_CTX *ctx)
{
    (*_BN_CTX_free)(ctx);
}
int SSL_UTILS_EC_KEY_generate_key(EC_KEY *eckey)
{
    return (*_EC_KEY_generate_key)(eckey);
}
RSA *SSL_UTILS_EVP_PKEY_get1_RSA(EVP_PKEY *pkey)
{
    return (*_EVP_PKEY_get1_RSA)(pkey);
}
BIGNUM *SSL_UTILS_BN_dup(const BIGNUM *a)
{
    return (*_BN_dup)(a);
}
BN_CTX *SSL_UTILS_BN_CTX_new(void)
{
    return (*_BN_CTX_new)();
}
int SSL_UTILS_EVP_PKEY_assign(EVP_PKEY *pkey, int type, void *key)
{
    return (*_EVP_PKEY_assign)(pkey, type, key);
}
int SSL_UTILS_BN_bn2bin(const BIGNUM *a, unsigned char *to)
{
    return (*_BN_bn2bin)(a, to);
}
int SSL_UTILS_BN_num_bits(const BIGNUM *a)
{
    return (*_BN_num_bits)(a);
}

int SSL_UTILS_ERR_GET_REASON(unsigned long errcode)
{
    // ERR_GET_REASON is macro in openssl 1
    if (get_sslVersion() == V1) {
        return (int)((errcode)&0xFFFL);
    }
    // ERR_GET_REASON is static in openssl 3. Here is Implementation below.
    if (SSL3_ERR_SYSTEM_ERROR(errcode))
        return errcode & SSL3_ERR_SYSTEM_MASK;
    return errcode & SSL3_ERR_REASON_MASK;
}

int SSL_UTILS_ERR_GET_FUNC(unsigned long errcode)
{
    // ERR_GET_FUNC is a macro in openssl 1,and removed since openssl 3.
    return (int)(((errcode) >> 12L) & 0xFFFL);
}

int SSL_UTILS_ERR_GET_LIB(unsigned long errcode)
{
    // ERR_GET_LIB is macro in openssl 1
    if (get_sslVersion() == V1) {
        return (int)(((errcode) >> 24L) & 0x0FFL);
    }
    // ERR_GET_REASON is static in openssl 3. Here is Implementation below.
    if (SSL3_ERR_SYSTEM_ERROR(errcode))
        return SSL3_ERR_LIB_SYS;
    return (errcode >> SSL3_ERR_LIB_OFFSET) & SSL3_ERR_LIB_MASK;
}

void SSL_UTILS_BN_clear_free(BIGNUM *a)
{
    (*_BN_clear_free)(a);
}

EVP_MD *SSL_UTILS_EVP_sm3(void)
{
    return (*_EVP_sm3)();
}

int SSL_UTILS_EVP_PKEY_CTX_set1_id(EVP_PKEY_CTX *ctx, const void *id, int len)
{
    // EVP_PKEY_CTX_set1_id is macro in openssl 1
    if (get_sslVersion() == V1) {
        return (*_EVP_PKEY_CTX_ctrl)(ctx, -1, -1, SSL1_EVP_PKEY_CTRL_SET1_ID, (int)len, (void *)(id));
    }
    return (*_EVP_PKEY_CTX_set1_id)(ctx, id, len);
}

void SSL_UTILS_EVP_MD_CTX_set_pkey_ctx(EVP_MD_CTX *ctx, EVP_PKEY_CTX *pctx)
{
    (*_EVP_MD_CTX_set_pkey_ctx)(ctx, pctx);
}

int SSL_UTILS_EVP_DigestSignInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey)
{
    return (*_EVP_DigestSignInit)(ctx, pctx, type, e, pkey);
}

int SSL_UTILS_EVP_DigestVerifyInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey)
{
    return (*_EVP_DigestVerifyInit)(ctx, pctx, type, e, pkey);
}

int SSL_UTILS_EVP_DigestSignUpdate(EVP_MD_CTX *ctx, const void *data, size_t dsize)
{
    // EVP_DigestSignUpdate is macro in openssl 1
    if (get_sslVersion() == V1) {
        return (*_EVP_DigestUpdate)(ctx, data, dsize);
    }
    return (*_EVP_DigestSignUpdate)(ctx, data, dsize);
}

int SSL_UTILS_EVP_DigestVerifyUpdate(EVP_MD_CTX *ctx, const void *data, size_t dsize)
{
    // EVP_DigestVerifyUpdate is macro in openssl 1
    if (get_sslVersion() == V1) {
        return (*_EVP_DigestUpdate)(ctx, data, dsize);
    }
    return (*_EVP_DigestVerifyUpdate)(ctx, data, dsize);
}

int SSL_UTILS_EVP_DigestSignFinal(EVP_MD_CTX *ctx, unsigned char *sigret, size_t *siglen)
{
    return (*_EVP_DigestSignFinal)(ctx, sigret, siglen);
}

int SSL_UTILS_EVP_DigestVerifyFinal(EVP_MD_CTX *ctx, const unsigned char *sig, size_t siglen)
{
    return (*_EVP_DigestVerifyFinal)(ctx, sig, siglen);
}

EC_KEY *SSL_UTILS_EC_KEY_new_by_curve_name(int nid)
{
    return (*_EC_KEY_new_by_curve_name)(nid);
}

int SSL_UTILS_EC_POINT_mul(const EC_GROUP *group, EC_POINT *r, const BIGNUM *g_scalar, const EC_POINT *point,
    const BIGNUM *p_scalar, BN_CTX *ctx)
{
    return (*_EC_POINT_mul)(group, r, g_scalar, point, p_scalar, ctx);
}

int SSL_UTILS_EVP_PKEY_set_alias_type(JNIEnv *env, EVP_PKEY *pkey, int type)
{
    // EVP_PKEY_set_alias_type is removed from openssl 3, it should be supported later.
    if (get_sslVersion() == V3) {
        KAE_TRACE("OpenSSL error，SM2 is not supported on openssl 3 yet.");
        return 1;
    }
    return (*_EVP_PKEY_set_alias_type)(pkey, type);
}

int SSL_UTILS_EVP_PKEY_assign_RSA(EVP_PKEY *pkey, void *key)
{
    // change from macro, "EVP_PKEY_assign_RSA(pkey,rsa)" is same as EVP_PKEY_assign((pkey),EVP_PKEY_RSA, (rsa))
    return SSL_UTILS_EVP_PKEY_assign((pkey), EVP_PKEY_RSA, (char *)(key));
}

int SSL_UTILS_EVP_PKEY_assign_EC_KEY(EVP_PKEY *pkey, void *key)
{
    // Changed from macro, "EVP_PKEY_assign_EC_KEY(pkey,eckey)" is "EVP_PKEY_assign((pkey),EVP_PKEY_EC, (char
    // *)(eckey))" in openssl 1 and 3
    return SSL_UTILS_EVP_PKEY_assign((pkey), EVP_PKEY_EC, (key));
}

void SSL_UTILS_EVP_MD_CTX_destroy(EVP_MD_CTX *ctx)
{
    // changed from macro, "# define EVP_MD_CTX_destroy(ctx) EVP_MD_CTX_free((ctx))" in openssl 1 and 3
    SSL_UTILS_EVP_MD_CTX_free(ctx);
}

EVP_MD_CTX *SSL_UTILS_EVP_MD_CTX_create(void)
{
    return SSL_UTILS_EVP_MD_CTX_new();
}

int SSL_UTILS_SSL_load_error_strings()
{
    // Change from macro, SSL_load_error_strings is a macro in openssl 1 and 3.
    return SSL_UTILS_OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
}

int SSL_UTILS_OpenSSL_add_all_algorithms()
{
// Change from macro, OpenSSL_add_all_algorithms ia a macro, defined by OPENSSL_LOAD_CONF value.
#ifdef OPENSSL_LOAD_CONF
    return SSL_UTILS_OPENSSL_init_crypto(
        OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS | OPENSSL_INIT_LOAD_CONFIG, NULL);
#else
    return SSL_UTILS_OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
#endif
}

int SSL_UTILS_BN_num_bytes(const BIGNUM *a)
{
    // Changed from macro, BN_num_bytes(bn) is ((BN_num_bits(bn)+7)/8);
    return ((SSL_UTILS_BN_num_bits(a) + 7) / 8);
}

const char * origin_func_name[] = {
        "RSA_new_method",
        "RSA_generate_key_ex",
        "RSA_free",
        "OPENSSL_init_ssl",
        "ERR_load_BIO_strings",
        "OPENSSL_init_crypto",
        "ENGINE_free",
        "ENGINE_by_id",
        "EVP_get_digestbyname",
        "EVP_PKEY_CTX_free",
        "EVP_PKEY_CTX_new",
        "EVP_PKEY_sign_init",
        "EVP_PKEY_sign",
        "EVP_PKEY_verify_init",
        "EVP_PKEY_verify",
        "EVP_get_cipherbyname",
        "EVP_CIPHER_CTX_new",
        "EVP_CipherInit_ex",
        "EVP_CIPHER_CTX_set_padding",
        "EVP_CIPHER_CTX_free",
        "EVP_CipherUpdate",
        "EVP_CipherFinal_ex",
        "EVP_CIPHER_CTX_ctrl",
        "BN_new",
        "BN_bin2bn",
        "BN_free",
        "EVP_PKEY_new",
        "RSA_set0_key",
        "RSA_set0_factors",
        "RSA_set0_crt_params",
        "EVP_PKEY_free",
        "RSA_private_encrypt",
        "RSA_private_decrypt",
        "RSA_public_encrypt",
        "RSA_public_decrypt",
        "EVP_PKEY_encrypt_init",
        "EVP_PKEY_encrypt",
        "EVP_PKEY_decrypt_init",
        "EVP_PKEY_decrypt",
        "EVP_MD_CTX_new",
        "EVP_DigestInit_ex",
        "EVP_MD_CTX_free",
        "EVP_DigestUpdate",
        "EVP_DigestFinal_ex",
        "EVP_MD_CTX_copy_ex",
        "ERR_get_error_line_data",
        "ERR_error_string_n",
        "ERR_clear_error",
        "HMAC_CTX_new",
        "HMAC_Init_ex",
        "HMAC_CTX_free",
        "HMAC_Update",
        "HMAC_Final",
        "DH_new_method",
        "DH_set0_pqg",
        "DH_set0_key",
        "DH_compute_key",
        "DH_free",
        "EC_POINT_free",
        "EC_KEY_free",
        "EC_GROUP_free",
        "OBJ_sn2nid",
        "EC_GROUP_new_by_curve_name",
        "EC_KEY_new",
        "EC_KEY_set_group",
        "EC_POINT_new",
        "EC_POINT_set_affine_coordinates_GFp",
        "EC_KEY_set_public_key",
        "EC_KEY_set_private_key",
        "EC_GROUP_get_degree",
        "ECDH_compute_key",
        "DH_set_length",
        "DH_generate_key",
        "DH_get0_priv_key",
        "DH_get0_pub_key",
        "EC_GROUP_get_curve_GFp",
        "EC_GROUP_get0_generator",
        "EC_POINT_get_affine_coordinates_GFp",
        "EC_GROUP_get_order",
        "EC_GROUP_get_cofactor",
        "EC_KEY_get0_public_key",
        "EC_KEY_get0_private_key",
        "BN_set_word",
        "EC_GROUP_new_curve_GFp",
        "EC_GROUP_set_generator",
        "BN_CTX_free",
        "EC_KEY_generate_key",
        "EVP_PKEY_get1_RSA",
        "BN_dup",
        "BN_CTX_new",
        "EVP_PKEY_assign",
        "BN_bn2bin",
        "RSA_get0_n",
        "RSA_get0_e",
        "RSA_get0_d",
        "RSA_get0_p",
        "RSA_get0_q",
        "RSA_get0_dmp1",
        "RSA_get0_dmq1",
        "RSA_get0_iqmp",
        "RSA_pkey_ctx_ctrl",
        "EVP_PKEY_CTX_ctrl",
        "BN_num_bits",
        "BN_clear_free",
        "EVP_sm3",
        "EVP_MD_CTX_set_pkey_ctx",
        "EVP_DigestSignInit",
        "EVP_DigestVerifyInit",
        "EVP_DigestSignFinal",
        "EVP_DigestVerifyFinal",
        "EC_KEY_new_by_curve_name",
        "EC_POINT_mul",
        "EVP_PKEY_size",
        "EVP_PKEY_set_alias_type",
        "EVP_PKEY_get_size",
        "EVP_PKEY_CTX_set0_rsa_oaep_label",
        "EVP_PKEY_CTX_set_signature_md",
        "EVP_PKEY_CTX_set_rsa_oaep_md",
        "EVP_PKEY_CTX_set_rsa_mgf1_md",
        "EVP_PKEY_CTX_set_rsa_pss_saltlen",
        "EVP_PKEY_CTX_set_rsa_padding",
        "EVP_PKEY_CTX_set1_id",
        "EVP_DigestSignUpdate",
        "EVP_DigestVerifyUpdate"
};

void ** kae_ssl_func[] = {
        (void**)&_RSA_new_method,
        (void**)&_RSA_generate_key_ex,
        (void**)&_RSA_free,
        (void**)&_OPENSSL_init_ssl,
        (void**)&_ERR_load_BIO_strings,
        (void**)&_OPENSSL_init_crypto,
        (void**)&_ENGINE_free,
        (void**)&_ENGINE_by_id,
        (void**)&_EVP_get_digestbyname,
        (void**)&_EVP_PKEY_CTX_free,
        (void**)&_EVP_PKEY_CTX_new,
        (void**)&_EVP_PKEY_sign_init,
        (void**)&_EVP_PKEY_sign,
        (void**)&_EVP_PKEY_verify_init,
        (void**)&_EVP_PKEY_verify,
        (void**)&_EVP_get_cipherbyname,
        (void**)&_EVP_CIPHER_CTX_new,
        (void**)&_EVP_CipherInit_ex,
        (void**)&_EVP_CIPHER_CTX_set_padding,
        (void**)&_EVP_CIPHER_CTX_free,
        (void**)&_EVP_CipherUpdate,
        (void**)&_EVP_CipherFinal_ex,
        (void**)&_EVP_CIPHER_CTX_ctrl,
        (void**)&_BN_new,
        (void**)&_BN_bin2bn,
        (void**)&_BN_free,
        (void**)&_EVP_PKEY_new,
        (void**)&_RSA_set0_key,
        (void**)&_RSA_set0_factors,
        (void**)&_RSA_set0_crt_params,
        (void**)&_EVP_PKEY_free,
        (void**)&_RSA_private_encrypt,
        (void**)&_RSA_private_decrypt,
        (void**)&_RSA_public_encrypt,
        (void**)&_RSA_public_decrypt,
        (void**)&_EVP_PKEY_encrypt_init,
        (void**)&_EVP_PKEY_encrypt,
        (void**)&_EVP_PKEY_decrypt_init,
        (void**)&_EVP_PKEY_decrypt,
        (void**)&_EVP_MD_CTX_new,
        (void**)&_EVP_DigestInit_ex,
        (void**)&_EVP_MD_CTX_free,
        (void**)&_EVP_DigestUpdate,
        (void**)&_EVP_DigestFinal_ex,
        (void**)&_EVP_MD_CTX_copy_ex,
        (void**)&_ERR_get_error_line_data,
        (void**)&_ERR_error_string_n,
        (void**)&_ERR_clear_error,
        (void**)&_HMAC_CTX_new,
        (void**)&_HMAC_Init_ex,
        (void**)&_HMAC_CTX_free,
        (void**)&_HMAC_Update,
        (void**)&_HMAC_Final,
        (void**)&_DH_new_method,
        (void**)&_DH_set0_pqg,
        (void**)&_DH_set0_key,
        (void**)&_DH_compute_key,
        (void**)&_DH_free,
        (void**)&_EC_POINT_free,
        (void**)&_EC_KEY_free,
        (void**)&_EC_GROUP_free,
        (void**)&_OBJ_sn2nid,
        (void**)&_EC_GROUP_new_by_curve_name,
        (void**)&_EC_KEY_new,
        (void**)&_EC_KEY_set_group,
        (void**)&_EC_POINT_new,
        (void**)&_EC_POINT_set_affine_coordinates_GFp,
        (void**)&_EC_KEY_set_public_key,
        (void**)&_EC_KEY_set_private_key,
        (void**)&_EC_GROUP_get_degree,
        (void**)&_ECDH_compute_key,
        (void**)&_DH_set_length,
        (void**)&_DH_generate_key,
        (void**)&_DH_get0_priv_key,
        (void**)&_DH_get0_pub_key,
        (void**)&_EC_GROUP_get_curve_GFp,
        (void**)&_EC_GROUP_get0_generator,
        (void**)&_EC_POINT_get_affine_coordinates_GFp,
        (void**)&_EC_GROUP_get_order,
        (void**)&_EC_GROUP_get_cofactor,
        (void**)&_EC_KEY_get0_public_key,
        (void**)&_EC_KEY_get0_private_key,
        (void**)&_BN_set_word,
        (void**)&_EC_GROUP_new_curve_GFp,
        (void**)&_EC_GROUP_set_generator,
        (void**)&_BN_CTX_free,
        (void**)&_EC_KEY_generate_key,
        (void**)&_EVP_PKEY_get1_RSA,
        (void**)&_BN_dup,
        (void**)&_BN_CTX_new,
        (void**)&_EVP_PKEY_assign,
        (void**)&_BN_bn2bin,
        (void**)&_RSA_get0_n,
        (void**)&_RSA_get0_e,
        (void**)&_RSA_get0_d,
        (void**)&_RSA_get0_p,
        (void**)&_RSA_get0_q,
        (void**)&_RSA_get0_dmp1,
        (void**)&_RSA_get0_dmq1,
        (void**)&_RSA_get0_iqmp,
        (void**)&_RSA_pkey_ctx_ctrl,
        (void**)&_EVP_PKEY_CTX_ctrl,
        (void**)&_BN_num_bits,
        (void**)&_BN_clear_free,
        (void**)&_EVP_sm3,
        (void**)&_EVP_MD_CTX_set_pkey_ctx,
        (void**)&_EVP_DigestSignInit,
        (void**)&_EVP_DigestVerifyInit,
        (void**)&_EVP_DigestSignFinal,
        (void**)&_EVP_DigestVerifyFinal,
        (void**)&_EC_KEY_new_by_curve_name,
        (void**)&_EC_POINT_mul,
        (void**)&_EVP_PKEY_size,
        (void**)&_EVP_PKEY_set_alias_type,
        (void**)&_EVP_PKEY_get_size,
        (void**)&_EVP_PKEY_CTX_set0_rsa_oaep_label,
        (void**)&_EVP_PKEY_CTX_set_signature_md,
        (void**)&_EVP_PKEY_CTX_set_rsa_oaep_md,
        (void**)&_EVP_PKEY_CTX_set_rsa_mgf1_md,
        (void**)&_EVP_PKEY_CTX_set_rsa_pss_saltlen,
        (void**)&_EVP_PKEY_CTX_set_rsa_padding,
        (void**)&_EVP_PKEY_CTX_set1_id,
        (void**)&_EVP_DigestSignUpdate,
        (void**)&_EVP_DigestVerifyUpdate
};

void SSL_UTILS_func_dl(JNIEnv *env)
{
    for(int i = COMMON_FUNC_START_INDEX; i <= COMMON_FUNC_END_INDEX; i++){
        *kae_ssl_func[i] = dlsym(_lib_handle, origin_func_name[i]);
        if (*kae_ssl_func[i] == NULL) {
            dlclose(_lib_handle);
            KAE_ThrowExceptionInInitializerError(env, "OpenSSL error while Openssl common function pointer assignment, nullpointer found.");
            return;
        }
    }

    if (get_sslVersion() == V1) {
        for(int i = V1_FUNC_START_INDEX; i <= V1_FUNC_END_INDEX; i++){
            *kae_ssl_func[i] = dlsym(_lib_handle, origin_func_name[i]);
            if (*kae_ssl_func[i] == NULL) {
                dlclose(_lib_handle);
                KAE_ThrowExceptionInInitializerError(env, "OpenSSL error while Openssl 1 unique function pointer assignment, nullpointer found.");
                return;
            }
        }
    }

    if (get_sslVersion() == V3) {
        for(int i = V3_FUNC_START_INDEX; i <= V3_FUNC_END_INDEX; i++){
            *kae_ssl_func[i] = dlsym(_lib_handle, origin_func_name[i]);
            if (*kae_ssl_func[i] == NULL) {
                dlclose(_lib_handle);
                KAE_ThrowExceptionInInitializerError(env, "OpenSSL error while Openssl 3 unique function pointer assignment, nullpointer found.");
                return;
            }
        }
    }
}

jboolean SSL_UTILS_func_ptr_init(JNIEnv *env, jint useOpensslVersion)
{
    jboolean init_result = JNI_TRUE;
    _lib_handle = open_ssl_lib(env, useOpensslVersion, &init_result);
    if (!init_result) {
        return init_result;
    }
    SSL_UTILS_func_dl(env);
    return init_result;
}

void *open_ssl_lib(JNIEnv *env, jint useOpensslVersion, jboolean *init_result)
{
    // default priorly use openssl3
    _sslVersion = V3;
    char *lib_name = "libssl.so.3";
    if (useOpensslVersion == 1) {
        _sslVersion = V1;
        lib_name = "libssl.so.1.1";
    }
    void *res = NULL;
    // if user changed kae.useOpensslVersion, check openSSL_Engine
    if (useOpensslVersion != 0) {
        // check engine with openssl version
        check_openSSL_Engine(env, init_result, lib_name);
        if (!*init_result) {
            return res;
        }
    }
    // set model RTLD_NOW | RTLD_GLOBAL Otherwise openssl3 env cannot get KAEEngine
    res = dlopen(lib_name, RTLD_NOW | RTLD_GLOBAL);
    // if useOpensslVersion is default 0, and dlopen openssl failed; re-attempting dlopen openssl1.
    if (res == NULL && useOpensslVersion == 0) {
        _sslVersion = V1;
        lib_name = "libssl.so.1.1";
        check_openSSL_Engine(env, init_result, lib_name);
        if (!*init_result) {
            return res;
        }
        res = dlopen(lib_name, RTLD_NOW | RTLD_GLOBAL);
    }

    if (res == NULL) {
        *init_result = JNI_FALSE;
        char* prefix = "OpenSSL error while opening openssl lib, no matching libssl found: ";
        char* msg = (char*)malloc(strlen(prefix) + strlen(lib_name) + 1);
        strcpy(msg, prefix);
        strcat(msg, lib_name);
        KAE_ThrowExceptionInInitializerError(env, msg);
        free(msg);
        return res;
    }

    check_openSSL_Engine(env, init_result, lib_name);
    if (!*init_result) {
        dlclose(res);
        res = NULL;
    }

    return res;
}

void check_openSSL_Engine(JNIEnv *env, jboolean *init_result, char *lib_name)
{
    char *openssl_engines_path = getenv("OPENSSL_ENGINES");
    // openssl_engines_path == null not use KAE Engine, only user KAE Engine check
    if (openssl_engines_path != NULL) {
        if (0 == strncmp("libssl.so.1.1", lib_name, strlen(lib_name)) && 0 != strncmp(openssl_engines_path, OPENSSL_ENGINES_VERSION_1_1, strlen(OPENSSL_ENGINES_VERSION_1_1))) {
            *init_result = JNI_FALSE;
            KAE_ThrowExceptionInInitializerError(env, "The version of OPENSSL_ENGINES in the environment is inconsistent with the version of the loaded OpenSSL library. Please check jdk config kae.useOpensslVersion or OPENSSL_ENGINES");
            return;
        }
        if (0 == strncmp("libssl.so.3", lib_name, strlen(lib_name)) && 0 != strncmp(openssl_engines_path, OPENSSL_ENGINES_VERSION_3_X, strlen(OPENSSL_ENGINES_VERSION_3_X))) {
            *init_result = JNI_FALSE;
            KAE_ThrowExceptionInInitializerError(env, "The version of OPENSSL_ENGINES in the environment is inconsistent with the version of the loaded OpenSSL library. Please check jdk config kae.useOpensslVersion or OPENSSL_ENGINES");
            return;
        }
    }
}

int get_sslVersion()
{
    return _sslVersion;
}