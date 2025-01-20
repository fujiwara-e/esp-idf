#include "common/defs.h"
#include "utils/base64.h"
#include "utils/json.h"
#include "utils/common.h"
#include "crypto/crypto.h"
#include "crypto/random.h"
#include "crypto/aes.h"
#include "crypto/aes_siv.h"
#include "crypto/sha256.h"
#include "dpp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// エラーハンドリング関数
void handleErrors(void) {
    ERR_print_errors_fp(stderr);
    abort();
}

// ECDH鍵ペアの生成関数
EC_KEY* generate_ec_key(void) {
    EC_KEY *key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1); // P-256
    if (key == NULL) {
        handleErrors();
    }

    if (EC_KEY_generate_key(key) != 1) {
        handleErrors();
    }

    return key;
}

// EC_KEY 構造体から公開鍵を取得してバイト列にエンコードする関数
int encode_ec_public_key(EC_KEY *key, unsigned char **out_pub_key, size_t *out_pub_key_len) {
    const EC_POINT *pub_key = EC_KEY_get0_public_key(key);
    const EC_GROUP *group = EC_KEY_get0_group(key);
    if (pub_key == NULL || group == NULL) {
        handleErrors();
        return 0;
    }

    size_t key_len = EC_POINT_point2oct(group, pub_key, POINT_CONVERSION_UNCOMPRESSED, NULL, 0, NULL);
    if (key_len == 0) {
        handleErrors();
        return 0;
    }

    *out_pub_key = (unsigned char *)malloc(key_len);
    if (*out_pub_key == NULL) {
        handleErrors();
        return 0;
    }

    if (EC_POINT_point2oct(group, pub_key, POINT_CONVERSION_UNCOMPRESSED, *out_pub_key, key_len, NULL) != key_len) {
        handleErrors();
        free(*out_pub_key);
        return 0;
    }

    *out_pub_key_len = key_len;
    return 1;
}


// EC_KEY 構造体から秘密鍵を取得してバイト列にエンコードする関数
int encode_ec_private_key(EC_KEY *key, unsigned char **out_priv_key, size_t *out_priv_key_len) {
	const BIGNUM *priv_key = EC_KEY_get0_private_key(key);
    if (priv_key == NULL) {
        handleErrors();
        return 0;
    }

    *out_priv_key_len = BN_num_bytes(priv_key);
    *out_priv_key = (unsigned char *)OPENSSL_malloc(*out_priv_key_len);
    if (*out_priv_key == NULL) {
        handleErrors();
        return 0;
    }

    BN_bn2bin(priv_key, *out_priv_key);
    return 1;
}



// 鍵をバイト列として表示する関数
void print_key(const EC_KEY *key) {
    const BIGNUM *priv_key = EC_KEY_get0_private_key(key);
    const EC_POINT *pub_key = EC_KEY_get0_public_key(key);
    const EC_GROUP *group = EC_KEY_get0_group(key);

    if (priv_key) {
        unsigned char *priv_key_bin = OPENSSL_malloc(BN_num_bytes(priv_key));
        if (priv_key_bin == NULL) {
            handleErrors();
        }
        int priv_key_len = BN_bn2bin(priv_key, priv_key_bin);
        printf("Private key: ");
        for (int i = 0; i < priv_key_len; i++) {
            printf("%02x", priv_key_bin[i]);
        }
        printf("\n");
        OPENSSL_free(priv_key_bin);
    } else {
        printf("No private key\n");
    }

    if (pub_key && group) {
        unsigned char *pub_key_bin = OPENSSL_malloc(EC_POINT_point2oct(group, pub_key, POINT_CONVERSION_UNCOMPRESSED, NULL, 0, NULL));
        if (pub_key_bin == NULL) {
            handleErrors();
        }
        int pub_key_len = EC_POINT_point2oct(group, pub_key, POINT_CONVERSION_UNCOMPRESSED, pub_key_bin, EC_POINT_point2oct(group, pub_key, POINT_CONVERSION_UNCOMPRESSED, NULL, 0, NULL), NULL);
        printf("Public key: ");
        for (int i = 0; i < pub_key_len; i++) {
            printf("%02x", pub_key_bin[i]);
        }
        printf("\n");
        OPENSSL_free(pub_key_bin);
    } else {
        printf("No public key\n");
    }
}

// 共通秘密鍵の計算関数
size_t compute_ecdh_secret(EC_KEY *own_key, EC_KEY *peer_key, unsigned char **secret) {
    size_t secret_len = 0;
    const EC_POINT *peer_pub_key = EC_KEY_get0_public_key(peer_key);
    const EC_GROUP *group = EC_KEY_get0_group(own_key);

    if (peer_pub_key == NULL || group == NULL) {
        handleErrors();
    }

    secret_len = (EC_GROUP_get_degree(group) + 7) / 8;
    *secret = OPENSSL_malloc(secret_len);
    if (*secret == NULL) {
        handleErrors();
    }

    secret_len = ECDH_compute_key(*secret, secret_len, peer_pub_key, own_key, NULL);
    if (secret_len <= 0) {
        handleErrors();
    }

    return secret_len;
}

// HKDFを使用した鍵導出関数
void derive_key_with_hkdf(const unsigned char *secret, size_t secret_len, unsigned char *out_key, size_t out_len) {
    const unsigned char info[] = "secound intermediate key";
    size_t info_len = strlen((const char*)info);

    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
    if (!pctx) {
        handleErrors();
    }

    if (EVP_PKEY_derive_init(pctx) <= 0) {
        handleErrors();
    }

    if (EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0) {
        handleErrors();
    }

    if (EVP_PKEY_CTX_set1_hkdf_salt(pctx, NULL, 0) <= 0) {
        handleErrors();
    }

    if (EVP_PKEY_CTX_set1_hkdf_key(pctx, secret, secret_len) <= 0) {
        handleErrors();
    }

    if (EVP_PKEY_CTX_add1_hkdf_info(pctx, info, info_len) <= 0) {
        handleErrors();
    }

    if (EVP_PKEY_derive(pctx, out_key, &out_len) <= 0) {
        handleErrors();
    }

    EVP_PKEY_CTX_free(pctx);
}

int main() {
    // 鍵ペアの生成
    EC_KEY *Ini_key = generate_ec_key();

    // Responder Bootstrap Key 
    uint8_t *Res_Boot_key = "\x92\x2d\xdd\x7a\x3e\xd6\x9f\x46\x12\x5d\x77\x2b\xbe\x60\x17\xcd\x4e\x03\x87\x0d\xc0\x14\x50\x9e\x38\xb5\x46\x28\xe1\x57\xa8\x7d";
    
	// 公開鍵のバイト列へのエンコード
	unsigned char *Ini_pub_key_bytes = NULL; //Initiator public key
	unsigned char *Res_pub_key_bytes = NULL; //Responder public key
    size_t pub_key_len = 0;
    if (!encode_ec_public_key(Ini_key, &Ini_pub_key_bytes, &pub_key_len)) {
        handleErrors();
    }
	pub_key_len = 0;

	// 秘密鍵をバイト列にエンコードする
    unsigned char *Ini_priv_key_bytes = NULL;
    size_t priv_key_len = 0;
    if (!encode_ec_private_key(Ini_key, &Ini_priv_key_bytes, &priv_key_len)) {
        handleErrors();
    }

    // 自身の鍵ペアの表示
    printf("Own Key Pair:\n");
    print_key(own_key);

    // ピアの鍵ペアの表示
    printf("Peer Key Pair:\n");
    print_key(peer_key);

    // 共通秘密鍵Nxの計算
    unsigned char *secret = NULL;
    size_t secret_len = compute_ecdh_secret(own_key, peer_key, &secret);

    // HKDFで導出するキーの長さ（例：32バイト = 256ビット）
    size_t out_len = 32;
    unsigned char out_key[out_len];

    // HKDFを使用してk2を導出
    derive_key_with_hkdf(secret, secret_len, out_key, out_len);

    // 導出されたキーの表示
    printf("k2 = : ");
    for (size_t i = 0; i < out_len; i++) {
        printf("%02x", out_key[i]);
    }
    printf("\n");

    // メモリの解放
    OPENSSL_free(secret);
    EC_KEY_free(own_key);
    EC_KEY_free(peer_key);

    return 0;
}
