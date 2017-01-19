#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>
#include <openssl/aes.h>
#include <openssl/md5.h>
#include <strophe.h>

#include <nefit-easy.h>

static void request_done(struct nefit_easy *easy);

const unsigned char magic[] = {
	0x58, 0xf1, 0x8d, 0x70, 0xf6, 0x67, 0xc9, 0xc7,
	0x9e, 0xf7, 0xde, 0x43, 0x5b, 0xf0, 0xf9, 0xb1,
	0x55, 0x3b, 0xbb, 0x6e, 0x61, 0x81, 0x62, 0x12,
	0xab, 0x80, 0xe5, 0xb0, 0xd3, 0x51, 0xfb, 0xb1,
};

static void generate_key(char const *uuid, char const *password, unsigned char *key)
{
	MD5_CTX context;

	MD5_Init(&context);
	MD5_Update(&context, uuid, strlen(uuid));
	MD5_Update(&context, magic, sizeof(magic) / sizeof(magic[0]));
	MD5_Final(key, &context);

	MD5_Init(&context);
	MD5_Update(&context, magic, sizeof(magic) / sizeof(magic[0]));
	MD5_Update(&context, password, strlen(password));
	MD5_Final(&key[16], &context);
}

/*
 * Decrypts the payload (HTTP response).
 * This depends on the password of the user.
*/
static void decrypt(struct nefit_easy *easy, unsigned char const *encrypted,
					size_t len, char *out)
{
	unsigned char const *s = encrypted;
	unsigned char *d = (unsigned char *) out;
	size_t c = 0;

	while (c < len)
	{
		AES_ecb_encrypt(&s[c], &d[c], &easy->aesKeyDec, AES_DECRYPT);
		c += 16;
	}
}

/*
 * Initializes the encryption for payload (HTTP request / response).
 * This depends on the password of the user.
 */
static int encyption_init(struct nefit_easy *easy, char const *uuid, char const *password)
{
	unsigned char key[32];
	generate_key(uuid, password, key);
	AES_set_encrypt_key(key, 256, &easy->aesKeyEnc);
	AES_set_decrypt_key(key, 256, &easy->aesKeyDec);
	return 0;
}

static int get_request_hander(xmpp_conn_t *conn, xmpp_stanza_t *stanza, void *userdata)
{
	struct nefit_easy *easy = (struct nefit_easy *) userdata;
	xmpp_ctx_t *ctx = easy->xmpp_ctx;
	xmpp_stanza_t *body;
	char *intext;
	char *content = NULL, *decoded = NULL;
	unsigned char *rawEncoded;
	size_t raw_encoded_len;
	json_object *new_obj;
	EASY_UNUSED(conn);

	body = xmpp_stanza_get_child_by_name(stanza, "body");
	if (!body)
		return 1;

	intext = xmpp_stanza_get_text(body);
	content = strstr(intext, "\n\n");

	if (content == NULL)
		goto out;

	xmpp_base64_decode_bin(ctx, &content[2], strlen(&content[2]), &rawEncoded,
						   &raw_encoded_len);
	decoded = (char *) malloc(raw_encoded_len);
	if (decoded == NULL)
		goto out;

	decrypt(easy, rawEncoded, raw_encoded_len, decoded);
	new_obj = json_tokener_parse(decoded);
	if (easy->cb)
		easy->cb(easy, new_obj);

out:
	xmpp_free(ctx, intext);
	request_done(easy);

	return 1;
}

static void send_stanza(struct nefit_easy *easy, xmpp_stanza_t *message)
{
	easy->busy = 1;
	xmpp_send(easy->xmpp_conn, message);
}

/* The actual processing of a queued HTTP request */
static int easy_get_it(struct nefit_easy *easy)
{
	xmpp_stanza_t *message, *body, *data;
	xmpp_ctx_t *ctx = easy->xmpp_ctx;
	struct request *req;

	req	= STAILQ_FIRST(&easy->requests);

	message = xmpp_stanza_new(ctx);
	xmpp_stanza_set_name(message, "message");
	xmpp_stanza_set_attribute(message, "from", xmpp_conn_get_jid(easy->xmpp_conn));
	xmpp_stanza_set_attribute(message, "to", easy->gateway);

	body = xmpp_stanza_new(ctx);
	xmpp_stanza_set_name(body, "body");
	xmpp_stanza_add_child(message, body);
	xmpp_stanza_release(body);

	data = xmpp_stanza_new(ctx);
	xmpp_stanza_set_text(data, req->http_req);
	printf("\n%s\n", req->description);
	printf("-------------------------------------------------------------------\n");
	xmpp_stanza_add_child(body, data);
	xmpp_stanza_release(data);

	send_stanza(easy, message);
	xmpp_stanza_release(message);

	return 1;
}

/* continue with the next request if any */
static void check_pending_work(struct nefit_easy *easy)
{
	if (easy->busy || !easy->connected)
		return;

	if (STAILQ_EMPTY(&easy->requests))
		return;

	easy_get_it(easy);
}

/* dequeue current HTTP request and check if there is more work todo */
static void request_done(struct nefit_easy *easy)
{
	struct request *req;

	req	= STAILQ_FIRST(&easy->requests);
	STAILQ_REMOVE_HEAD(&easy->requests, next);
	free(req->http_req);
	free(req);
	easy->busy = 0;
	check_pending_work(easy);
}

/**
 * @brief Queues a request. When idle the request is send immediately.
 * @param easy The easy to queue the request for
 * @param url The url to request
 * @return 0 when successful
 */
int easy_get(struct nefit_easy *easy, char const *url)
{
	struct request *req	= calloc(1, sizeof(struct request));
	if (!req)
		goto error;

	if (asprintf(&req->http_req,
		"GET %s HTTP/1.0\r\n" \
		"User-Agent: NefitEasy\r\n" \
		"\r\n", url) < 0)
		goto error;

	if (asprintf(&req->description, "get %s", url) < 0)
		goto error;

	if (!req->http_req)
		goto error;

	STAILQ_INSERT_TAIL(&easy->requests, req, next);
	check_pending_work(easy);

	return 0;

error:
	free(req->http_req);
	free(req);
	return -1;
}

/* ping_handler, answers remote ping request to keep the connection alive */
static int ping_handler(xmpp_conn_t *conn, xmpp_stanza_t *stanza, void *userdata)
{
	struct nefit_easy *easy = (struct nefit_easy *) userdata;
	xmpp_ctx_t *ctx = easy->xmpp_ctx;
	xmpp_stanza_t *pong;
	char const *id, *to, *from;

	id = xmpp_stanza_get_attribute(stanza, "id");
	to = xmpp_stanza_get_attribute(stanza, "to");
	from = xmpp_stanza_get_attribute(stanza, "from");

	if (!from || !to || !id)
		return 1;

	pong = xmpp_stanza_new(ctx);
	xmpp_stanza_set_name(pong, "iq");
	xmpp_stanza_set_attribute(pong, "to", from);
	xmpp_stanza_set_attribute(pong, "from", to);
	xmpp_stanza_set_attribute(pong, "type", "result");
	xmpp_stanza_set_attribute(pong, "id", id);
	xmpp_send(conn, pong);
	xmpp_stanza_release(pong);

	return 1;
}

static int message_handler(xmpp_conn_t * const conn,
						   xmpp_stanza_t * const stanza, void * const userdata)
{
	//struct nefit_easy *easy = (struct nefit_easy *) userdata;
	EASY_UNUSED(userdata);
	EASY_UNUSED(conn);
	EASY_UNUSED(stanza);

	return 1;
}

/* define a handler for connection events */
static void conn_handler(xmpp_conn_t *conn, xmpp_conn_event_t status,
				int error, xmpp_stream_error_t *stream_error, void *userdata)
{
	struct nefit_easy *easy = (struct nefit_easy *) userdata;
	xmpp_ctx_t *ctx = easy->xmpp_ctx;
	EASY_UNUSED(error);
	EASY_UNUSED(stream_error);

	easy->connected = status == XMPP_CONN_CONNECT;
	if (easy->connected) {
		xmpp_stanza_t *pres;
		fprintf(stderr, "DEBUG: connected\n");
		xmpp_handler_add(conn, message_handler, NULL, "presence", NULL, easy);
		xmpp_handler_add(conn, ping_handler, "urn:xmpp:ping", "iq", "get", easy);
		xmpp_handler_add(conn, get_request_hander, NULL, "message", NULL, easy);

		/* Send initial <presence/> */
		pres = xmpp_stanza_new(ctx);
		xmpp_stanza_set_name(pres, "presence");
		xmpp_send(conn, pres);
		xmpp_stanza_release(pres);

		check_pending_work(easy);
	} else {
		fprintf(stderr, "DEBUG: disconnected\n");
		xmpp_stop(ctx);
	}
}

/**
 * @brief connect to a remote thermostat by xmpp
 * @param easy			the instance of this easy, can be uninitialized
 * @param serial		the thermostat to connect to, serial is printed in the manual e.g.
 * @param access_key	allows access to the broker in the first place, also
 *						printed in the manual
 * @param password		user chosen, used as part of the payload encryption
 * @param cb			function where received values are reported to
 * @return				0 when successful
 */
int easy_connect(struct nefit_easy *easy, char const *serial, char const *access_key,
				 char const *password, netif_easy_callback *cb)
{
	char *jid, *pass;
	xmpp_log_t *log;
	char const *host = "wa2-mz36-qrmzh6.bosch.de";

	memset(easy, 0, sizeof(struct nefit_easy));

	/* create a context */
	log = xmpp_get_default_logger(XMPP_LEVEL_DEBUG);
	log = NULL; /* disable logging */
	easy->xmpp_ctx = xmpp_ctx_new(NULL, log);

	encyption_init(easy, access_key, password);
	STAILQ_INIT(&easy->requests);

	/* create a connection */
	easy->xmpp_conn = xmpp_conn_new(easy->xmpp_ctx);
	easy->cb = cb;

	if (xmpp_conn_set_flags(easy->xmpp_conn, XMPP_CONN_FLAG_MANDATORY_TLS) != 0)
		return -1;

	/* setup authentication information */
	asprintf(&jid, "rrccontact_%s@%s", serial, host);
	asprintf(&easy->gateway, "rrcgateway_%s@%s", serial, host);
	xmpp_conn_set_jid(easy->xmpp_conn, jid);
	free(jid);

	asprintf(&pass, "Ct7ZR03b_%s", access_key);
	xmpp_conn_set_pass(easy->xmpp_conn, pass);
	free(pass);

	/* initiate connection */
	xmpp_connect_client(easy->xmpp_conn, 0, 0, conn_handler, easy);

	return 0;
}
