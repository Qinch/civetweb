#if defined(MG_MBEDTLS)  // mbedtls 隔离宏, 和NO_SSL宏一起使用

#include "mbedtls/certs.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"
#include "mbedtls/platform.h"


typedef struct {
    mbedtls_ssl_config          conf;           /* SSL configuration */
    mbedtls_x509_crt            cert;           /* Certificate (own) */
    mbedtls_ctr_drbg_context    ctr;            /* Counter random generator state */
    mbedtls_entropy_context     entropy;        /* Entropy context */
    mbedtls_pk_context          pkey;           /* Private key */
} mbed_context;


// public
int mbed_sslctx_init(mbed_context *ctx);
void mbed_sslctx_uninit(mbed_context *ctx);
void mbed_ssl_close(mbedtls_ssl_context *ssl);
int mbed_ssl_accept(mbedtls_ssl_context **ssl, mbed_context *ssl_ctx, int *sock);
int mbed_ssl_read(mbedtls_ssl_context *ssl, unsigned char *buf, int len);
int mbed_ssl_write(mbedtls_ssl_context *ssl, const unsigned char *buf, int len);

static void mbed_debug(void *context, int level, const char *file, int line, const char *str);
static int mbed_ssl_handshake(mbedtls_ssl_context *ssl);

int
mbed_sslctx_init(mbed_context *ctx)
{
    mbedtls_ssl_config *conf;
    int rc;

    fprintf(stdout, "Initializing MbedTLS SSL\n");

    mbedtls_entropy_init(&ctx->entropy);

    conf = &ctx->conf;
    mbedtls_ssl_config_init(conf);

    // 设置debug级别
    mbedtls_debug_set_threshold(1);
    mbedtls_ssl_conf_dbg(conf, mbed_debug, stdout);
    mbedtls_pk_init(&ctx->pkey);
    mbedtls_ctr_drbg_init(&ctx->ctr);
    mbedtls_x509_crt_init(&ctx->cert);
	
    if ((rc = mbedtls_ctr_drbg_seed(&ctx->ctr,
                                    mbedtls_entropy_func,
                                    &ctx->entropy,
                                    (unsigned char *)"CivetWeb",
                                    strlen("CivetWeb")))
        != 0) {
        fprintf(stderr, "Cannot seed rng\n");
        return -1;
    }

    // key.
    if (mbedtls_pk_parse_keyfile(&ctx->pkey, "./server.key", NULL) != 0) {
        fprintf(stderr, "parse key file failed\n");
        return -1;
    }

    // Load a PEM format certificate file
    if (mbedtls_x509_crt_parse_file(&ctx->cert, "./server.crt") != 0) {
        fprintf(stderr, "parse crt file faied\n");
    }

    if ((rc = mbedtls_ssl_config_defaults(conf,
                                            MBEDTLS_SSL_IS_SERVER,
                                            MBEDTLS_SSL_TRANSPORT_STREAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT))
        != 0) {
        fprintf(stderr, "Cannot set mbedtls defaults\n");
        return -1;
    }

    mbedtls_ssl_conf_rng(conf, mbedtls_ctr_drbg_random, &ctx->ctr);

    // Set auth mode if peer cert should be verified
    mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_ca_chain(conf, NULL, NULL);

    // Configure server cert and key
    if ((rc = mbedtls_ssl_conf_own_cert(conf, &ctx->cert, &ctx->pkey))
        != 0) {
        fprintf(stderr, "Cannot define certificate and private key\n");
        return -1;
    }
    return 0;
}

void 
mbed_sslctx_uninit(mbed_context *ctx)
{
    mbedtls_ctr_drbg_free(&ctx->ctr);
    mbedtls_pk_free(&ctx->pkey);
    mbedtls_x509_crt_free(&ctx->cert);
    mbedtls_entropy_free(&ctx->entropy);
    mbedtls_ssl_config_free(&ctx->conf);
}

int
mbed_ssl_accept(mbedtls_ssl_context **ssl, mbed_context *ssl_ctx, int *sock)
{
    *ssl = calloc(1, sizeof(**ssl));
    if (*ssl == NULL) {
        fprintf(stderr, "malloc ssl failed\n");
        return -1;
    }

    mbedtls_ssl_init(*ssl);
    mbedtls_ssl_setup(*ssl, &ssl_ctx->conf);
	mbedtls_ssl_set_bio(*ssl, sock, mbedtls_net_send, mbedtls_net_recv, NULL);

    if (mbed_ssl_handshake(*ssl) != 0) {
        return -1;
    }
    return 0;
}

void
mbed_ssl_close(mbedtls_ssl_context *ssl)
{
    mbedtls_ssl_close_notify(ssl);
    mbedtls_ssl_free(ssl);
    ssl = NULL;
}

/*
    Initiate or continue SSL handshaking with the peer. This routine does not block.
    Return -1 on errors, 0 incomplete and awaiting I/O, 1 if successful
 */
static int
mbed_ssl_handshake(mbedtls_ssl_context *ssl)
{
    int rc = 0;
    int retry =  0;
    while ((ssl->state != MBEDTLS_SSL_HANDSHAKE_OVER) && (retry < 3) && ((rc = mbedtls_ssl_handshake(ssl)) != 0)) {
		++retry;
		if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)  {
            continue;
        }

		char ebuf[256];
		mbedtls_strerror(-rc, ebuf, sizeof(ebuf));
        fprintf(stderr, "handshake rc:%d err:%s\n", rc, ebuf);
        break;
    }

    fprintf(stdout, "mbedtls handshake rc:%d state:%d\n", rc, ssl->state);
    return rc;
}

int
mbed_ssl_read(mbedtls_ssl_context *ssl, unsigned char *buf, int len)
{
    int rc = mbedtls_ssl_read(ssl, buf, len);
    fprintf(stdout, "mbedtls: mbedtls_ssl_read %d\n", rc);
    return rc;
}

int
mbed_ssl_write(mbedtls_ssl_context *ssl, const unsigned char *buf, int len)
{
    int rc = mbedtls_ssl_write(ssl, buf, len);
    fprintf(stdout, "mbedtls: mbedtls_ssl_write:%d\n", rc);
    return rc;
}

static void
mbed_debug(void *context, int level, const char *file, int line, const char *str)
{
    (void)level;
    mbedtls_fprintf((FILE *)context, "file:%s line:%d str:%s\n", file, line, str);
}

#endif /* MG_MBEDTLS */
