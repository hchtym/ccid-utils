/*
 * This file is part of ccid-utils
 * Copyright (c) 2008 Gianni Tedesco <gianni@scaramanga.co.uk>
 * Released under the terms of the GNU GPL version 3
*/

#include <ccid.h>
#include <list.h>
#include <emv.h>
#include <ber.h>
#include "emv-internal.h"

struct sda_req {
	unsigned int ca_pk_idx;
	const uint8_t *pk_cert;
	size_t pk_cert_len;
	const uint8_t *pk_r;
	size_t pk_r_len;
	const uint8_t *pk_exp;
	size_t pk_exp_len;
	const uint8_t *ssa_data;
	size_t ssa_data_len;
};

static int emsa_pss_decode(const uint8_t *msg, size_t msg_len,
				const uint8_t *em, size_t em_len)
{
	uint8_t md[SHA_DIGEST_LENGTH];
	size_t mdb_len;

	/* 2. mHash < Hash(m) */
	SHA1(msg, msg_len, md);

	/* 4. if the rightmost octet of em does not have hexadecimal
	 * value 0xBC, output “invalid” */
	if ( em[em_len - 1] != 0xbc ) {
		printf("emsa-pss: bad trailer\n");
		return 0;
	}
	
	mdb_len = em_len - sizeof(md) - 1;

	if ( memcmp(em + mdb_len, md, SHA_DIGEST_LENGTH) )
		return 0;

	return 1;
}

static int get_required_data(struct _emv *e, struct sda_req *req)
{
	const struct _emv_data *d;
	int i;

	d = _emv_retrieve_data(e, EMV_TAG_CA_PK_INDEX);
	i = emv_data_int(d);
	if ( i < 0 )
		return 0;

	req->ca_pk_idx = (unsigned int)i;

	d = _emv_retrieve_data(e, EMV_TAG_ISS_PK_CERT);
	if ( NULL == d )
		return 0;
	req->pk_cert = d->d_data;
	req->pk_cert_len = d->d_len;
	if ( NULL == req->pk_cert )
		return 0;

	d = _emv_retrieve_data(e, EMV_TAG_ISS_PK_R);
	if ( NULL == d )
		return 0;
	req->pk_r = d->d_data;
	req->pk_r_len = d->d_len;
	if ( NULL == req->pk_r )
		return 0;

	d = _emv_retrieve_data(e, EMV_TAG_ISS_PK_EXP);
	if ( NULL == d )
		return 0;
	req->pk_exp = d->d_data;
	req->pk_exp_len = d->d_len;
	if ( NULL == req->pk_exp)
		return 0;

	d = _emv_retrieve_data(e, EMV_TAG_SSA_DATA);
	if ( NULL == d )
		return 0;
	req->ssa_data = d->d_data;
	req->ssa_data_len = d->d_len;
	if ( NULL == req->ssa_data)
		return 0;

	return 1;
}

static RSA *get_ca_key(unsigned int idx, emv_mod_cb_t mod,
			emv_exp_cb_t exp, size_t *key_len, void *priv)
{
	const uint8_t *modulus, *exponent;
	size_t mod_len, exp_len;
	RSA *key;

	modulus = (*mod)(priv, idx, &mod_len);
	if ( NULL == modulus )
		return NULL;

	exponent = (*exp)(priv, idx, &exp_len);
	if ( NULL == exponent )
		return NULL;
	
	key = RSA_new();
	if ( NULL == key )
		return NULL;

	key->n = BN_bin2bn(modulus, mod_len, NULL);
	if ( NULL == key->n )
		goto err_free_key;

	key->e = BN_bin2bn(exponent, exp_len, NULL);
	if ( NULL == key->e )
		goto err_free_mod;

	*key_len = mod_len;

	return key;
err_free_mod:
	BN_free(key->n);
err_free_key:
	RSA_free(key);
	return NULL;
}

static int recover(uint8_t *ptr, size_t len, RSA *key)
{
	uint8_t *tmp;
	int ret;

	tmp = malloc(len);
	if ( NULL == tmp )
		return 0;

	ret = RSA_public_encrypt(len, ptr, tmp, key, RSA_NO_PADDING);
	if ( ret < 0 || (unsigned)ret != len ) {
		free(tmp);
		return 0;
	}

	memcpy(ptr, tmp, len);
	free(tmp);

	return 1;
}

static int check_pk_cert(struct sda_req *req)
{
	uint8_t *msg, *tmp;
	size_t msg_len;
	int ret;

	if ( req->pk_cert[0] != 0x6a ) {
		printf("emv-sda: bad certificate header\n");
		return 0;
	}

	if ( req->pk_cert[1] != 0x02 ) {
		printf("emv-sda: bad certificate format\n");
		return 0;
	}

	if ( req->pk_cert[11] != 0x01 ) {
		printf("emv-sda: unexpected hash algorithm\n");
		return 0;
	}

	msg_len = req->pk_cert_len - (SHA_DIGEST_LENGTH + 2) +
			req->pk_r_len + req->pk_exp_len;
	tmp = msg = malloc(msg_len);
	if ( NULL == msg )
		return 0;

	memcpy(tmp, req->pk_cert + 1, req->pk_cert_len -
		(SHA_DIGEST_LENGTH + 2));
	tmp += req->pk_cert_len - (SHA_DIGEST_LENGTH + 2);
	memcpy(tmp, req->pk_r, req->pk_r_len);
	tmp += req->pk_r_len;
	memcpy(tmp, req->pk_exp, req->pk_exp_len);

//	printf("Encoded message of %u bytes:\n", msg_len);
//	hex_dump(msg, msg_len, 16);

	ret = emsa_pss_decode(msg, msg_len, req->pk_cert, req->pk_cert_len);
	free(msg);

	return ret;
}

static RSA *make_issuer_pk(struct sda_req *req)
{
	uint8_t *tmp;
	const uint8_t *kb;
	size_t kb_len;
	RSA *key;

	tmp = malloc(req->pk_cert_len);
	if ( NULL == tmp )
		return NULL;

	kb = req->pk_cert + 15;
	kb_len = req->pk_cert_len - (15 + 21);

	memcpy(tmp, kb, kb_len);
	memcpy(tmp + kb_len, req->pk_r, req->pk_r_len);
	printf("Retrieved issuer public key:\n");
	hex_dump(kb, kb_len, 16);
	key = RSA_new();
	if ( NULL == key ) {
		free(tmp);
		return NULL;
	}

	key->n = BN_bin2bn(tmp, req->pk_cert_len, NULL);
	key->e = BN_bin2bn(req->pk_exp, req->pk_exp_len, NULL);
	free(tmp);
	if ( NULL == key->n || NULL == key->e ) {
		RSA_free(key);
		return NULL;
	}

	return key;
}

static RSA *get_issuer_pk(struct sda_req *req, RSA *ca_key, size_t key_len)
{

	if ( req->pk_cert_len != key_len ) {
		printf("emv-sda: issuer pubkey cert failed len check\n");
		return NULL;
	}

	if ( !recover((uint8_t *)req->pk_cert, key_len, ca_key) ) {
		printf("emv-sda: RSA recovery failed on pubkey cert\n");
		return NULL;
	}

//	printf("recovered issuer pubkey cert:\n");
//	hex_dump(req->pk_cert, key_len, 16);

	if ( !check_pk_cert(req) )
		return NULL;

	return make_issuer_pk(req);
}

static int check_ssa(const uint8_t *ptr, size_t len,
				struct _emv_data **rec, unsigned int num,
				const uint8_t *aip)
{
	uint8_t *tmp, *msg;
	const uint8_t *cf;
	size_t msg_len, cf_len, data_len;
	unsigned int i;
	int ret;

	for(data_len = 2, i = 0; i < num; i++)
		data_len += rec[i]->d_len;

	cf = ptr + 1;
	cf_len = len - (SHA_DIGEST_LENGTH + 2);
	msg_len = cf_len + data_len;

	tmp = msg = malloc(msg_len);
	if ( NULL == msg )
		return 0;

	memcpy(msg, cf, cf_len), tmp += cf_len;

	for(i = 0; i < num; i++) {
		memcpy(tmp, rec[i]->d_data, rec[i]->d_len);
		tmp += rec[i]->d_len;
	}

	memcpy(tmp, aip, 2);

#if 0
	printf("%u byte ssa certificate:\n", msg_len);
	hex_dump(ptr, len, 16);

	printf("%u byte ssa message:\n", msg_len);
	hex_dump(msg, msg_len, 16);
#endif

	ret = emsa_pss_decode(msg, msg_len, ptr, len);
	free(msg);

	return ret;
}

static int verify_ssa_data(struct _emv_data **rec, unsigned int num_rec,
				struct sda_req *req, RSA *iss_key,
				const uint8_t *aip)
{
	if ( !recover((uint8_t *)req->ssa_data, req->ssa_data_len, iss_key) ) {
		printf("emv-sda: RSA recovery failed on SSA data\n");
		return 0;
	}

	if ( req->ssa_data[0] != 0x6a ) {
		printf("error: SSA data bad signature byte\n");
		return 0;
	}
	
	if ( req->ssa_data[1] != 0x03 ) {
		printf("error: SSA data bad format\n");
		return 0;
	}

	if ( req->ssa_data[2] != 0x01 ) {
		printf("error: unexpected hash format in SSA data\n");
		return 0;
	}

	return check_ssa(req->ssa_data, req->ssa_data_len,
			rec, num_rec, aip);
}

int emv_authenticate_static_data(emv_t e, emv_mod_cb_t mod, emv_exp_cb_t exp,
					void *priv)
{
	struct sda_req req;
	size_t ca_key_len;
	RSA *ca_key;

	if ( !(e->e_aip[0] & EMV_AIP_SDA) ) {
		printf("emv-sda: card doesn't support SDA\n");
		return 0;
	}

	if ( !get_required_data(e, &req) ) {
		printf("emv-sda: required data elements not present\n");
		return 0;
	}

	ca_key = get_ca_key(req.ca_pk_idx, mod, exp, &ca_key_len, priv);
	if ( NULL == ca_key ) {
		printf("emv-sda: bad CA key\n");
		return 0;
	}

	e->e_ca_pk = ca_key;

	e->e_iss_pk = get_issuer_pk(&req, ca_key, ca_key_len);
	if ( NULL == e->e_iss_pk )
		return 0;

	if ( !verify_ssa_data(e->e_db.db_sda, e->e_db.db_numsda,
				&req, e->e_iss_pk, e->e_aip) )
		return 0;

	e->e_sda_ok = 1;
	return 1;
}

int emv_sda_ok(emv_t e)
{
	return !!e->e_sda_ok;
}