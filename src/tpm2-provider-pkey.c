/*******************************************************************************
 * Copyright 2017-2018, Fraunhofer SIT sponsored by Infineon Technologies AG
 * All rights reserved.
 * Copyright (c) 2019, Wind River Systems.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of tpm2-tss-engine nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#include <string.h>

#include <openssl/pem.h>
#include <openssl/rand.h>

#include <tss2/tss2_mu.h>
#include "tpm2-provider-pkey.h"

TPM2B_DIGEST ownerauth = { .size = 0 };
TPM2B_DIGEST parentauth = { .size = 0 };

ASN1_SEQUENCE(TSSPRIVKEY) = {
    ASN1_SIMPLE(TSSPRIVKEY, type, ASN1_OBJECT),
    ASN1_EXP_OPT(TSSPRIVKEY, emptyAuth, ASN1_BOOLEAN, 0),
    ASN1_SIMPLE(TSSPRIVKEY, parent, ASN1_INTEGER),
    ASN1_SIMPLE(TSSPRIVKEY, pubkey, ASN1_OCTET_STRING),
    ASN1_SIMPLE(TSSPRIVKEY, privkey, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(TSSPRIVKEY)

#define TSSPRIVKEY_PEM_STRING "TSS2 PRIVATE KEY"

IMPLEMENT_ASN1_FUNCTIONS(TSSPRIVKEY);
IMPLEMENT_PEM_write_bio(TSSPRIVKEY, TSSPRIVKEY, TSSPRIVKEY_PEM_STRING, TSSPRIVKEY);
IMPLEMENT_PEM_read_bio(TSSPRIVKEY, TSSPRIVKEY, TSSPRIVKEY_PEM_STRING, TSSPRIVKEY);

/** Serialize tpm2data onto disk
 *
 * Write the tpm2tss key data into a file using PEM encoding.
 * @param tpm2Data The data to be written to disk.
 * @param filename The filename to write the data to.
 * @retval 1 on success
 * @retval 0 on failure
 */
int
tpm2_tpm2data_write(TPM2_PROVIDER_CTX *prov, const TPM2_DATA *tpm2Data, BIO *bio)
{
    TSS2_RC r;
    TSSPRIVKEY *tpk = NULL;

    uint8_t privbuf[sizeof(tpm2Data->priv)];
    uint8_t pubbuf[sizeof(tpm2Data->pub)];
    size_t privbuf_len = 0, pubbuf_len = 0;

    tpk = TSSPRIVKEY_new();
    if (!tpk) {
        TPM2_ERROR_raise(prov, ERR_R_MALLOC_FAILURE);
        goto error;
    }

    r = Tss2_MU_TPM2B_PRIVATE_Marshal(&tpm2Data->priv, &privbuf[0],
                                      sizeof(privbuf), &privbuf_len);
    if (r) {
        TPM2_ERROR_raise(prov, TPM2TSS_R_DATA_CORRUPTED);
        goto error;
    }

    r = Tss2_MU_TPM2B_PUBLIC_Marshal(&tpm2Data->pub, &pubbuf[0],
                                     sizeof(pubbuf), &pubbuf_len);
    if (r) {
        TPM2_ERROR_raise(prov, TPM2TSS_R_DATA_CORRUPTED);
        goto error;
    }
    tpk->type = OBJ_txt2obj(OID_loadableKey, 1);
    tpk->parent = ASN1_INTEGER_new();
    tpk->privkey = ASN1_OCTET_STRING_new();
    tpk->pubkey = ASN1_OCTET_STRING_new();
    if (!tpk->type || !tpk->privkey || !tpk->pubkey || !tpk->parent) {
        TPM2_ERROR_raise(prov, ERR_R_MALLOC_FAILURE);
        goto error;
    }

    tpk->emptyAuth = ! !tpm2Data->emptyAuth;
    if (tpm2Data->parent != 0)
        ASN1_INTEGER_set(tpk->parent, tpm2Data->parent);
    else
        ASN1_INTEGER_set(tpk->parent, TPM2_RH_OWNER);

    ASN1_STRING_set(tpk->privkey, &privbuf[0], privbuf_len);
    ASN1_STRING_set(tpk->pubkey, &pubbuf[0], pubbuf_len);

    PEM_write_bio_TSSPRIVKEY(bio, tpk);
    TSSPRIVKEY_free(tpk);

    return 1;
error:
    if (tpk)
        TSSPRIVKEY_free(tpk);
    return 0;
}

/** Deserialize tpm2data from disk
 *
 * Read the tpm2tss key data from a file using PEM encoding.
 * @param filename The filename to read the data from.
 * @param tpm2Datap The data after read.
 * @retval 1 on success
 * @retval 0 on failure
 */
int
tpm2_tpm2data_read(TPM2_PROVIDER_CTX *prov, BIO *bio, TPM2_DATA **tpm2Datap)
{
    TSS2_RC r;
    TSSPRIVKEY *tpk = NULL;
    TPM2_DATA *tpm2Data = NULL;
    char type_oid[64];

    tpk = PEM_read_bio_TSSPRIVKEY(bio, NULL, NULL, NULL);
    if (!tpk) {
        TPM2_ERROR_raise(prov, TPM2TSS_R_DATA_CORRUPTED);
        goto error;
    }

    tpm2Data = OPENSSL_zalloc(sizeof(TPM2_DATA));
    if (tpm2Data == NULL) {
        TPM2_ERROR_raise(prov, ERR_R_MALLOC_FAILURE);
        goto error;
    }

    tpm2Data->privatetype = KEY_TYPE_BLOB;
    tpm2Data->emptyAuth = tpk->emptyAuth;

    tpm2Data->parent = ASN1_INTEGER_get(tpk->parent);
    if (tpm2Data->parent == 0)
        tpm2Data->parent = TPM2_RH_OWNER;

    if (!OBJ_obj2txt(type_oid, sizeof(type_oid), tpk->type, 1) ||
            strcmp(type_oid, OID_loadableKey)) {
        TPM2_ERROR_raise(prov, TPM2TSS_R_CANNOT_MAKE_KEY);
        goto error;
    }
    r = Tss2_MU_TPM2B_PRIVATE_Unmarshal(tpk->privkey->data,
                                        tpk->privkey->length, NULL,
                                        &tpm2Data->priv);
    if (r) {
        TPM2_ERROR_raise(prov, TPM2TSS_R_DATA_CORRUPTED);
        goto error;
    }
    r = Tss2_MU_TPM2B_PUBLIC_Unmarshal(tpk->pubkey->data, tpk->pubkey->length,
                                       NULL, &tpm2Data->pub);
    if (r) {
        TPM2_ERROR_raise(prov, TPM2TSS_R_DATA_CORRUPTED);
        goto error;
    }

    TSSPRIVKEY_free(tpk);

    *tpm2Datap = tpm2Data;
    return 1;
 error:
    if (tpm2Data)
        OPENSSL_free(tpm2Data);
    if (tpk)
        TSSPRIVKEY_free(tpk);

    return 0;
}

/** Create tpm2data from a TPM key
 *
 * Retrieve the public key of tpm2data from the TPM for a given handle.
 * @param handle The TPM's key handle.
 * @param tpm2Datap The data after read.
 * @retval 1 on success
 * @retval 0 on failure
 */
int
tpm2_tpm2data_readtpm(TPM2_PROVIDER_CTX *prov,
                      uint32_t handle, TPM2_DATA **tpm2Datap)
{
    TSS2_RC r;
    TPM2_DATA *tpm2Data = NULL;
    ESYS_TR keyHandle = ESYS_TR_NONE;
    TPM2B_PUBLIC *outPublic;

    tpm2Data = OPENSSL_zalloc(sizeof(TPM2_DATA));
    if (tpm2Data == NULL) {
        TPM2_ERROR_raise(prov, ERR_R_MALLOC_FAILURE);
        goto error;
    }

    tpm2Data->privatetype = KEY_TYPE_HANDLE;
    tpm2Data->handle = handle;

    r = Esys_TR_FromTPMPublic(prov->esys_ctx, tpm2Data->handle,
                              ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
                              &keyHandle);
    if (r) {
        TPM2_ERROR_raise(prov, TPM2TSS_R_GENERAL_FAILURE);
        goto error;
    }

    r = Esys_ReadPublic(prov->esys_ctx, keyHandle,
                        ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
                        &outPublic, NULL, NULL);
    if (r) {
        TPM2_ERROR_raise(prov, TPM2TSS_R_GENERAL_FAILURE);
        goto error;
    }

    /* If the persistent key has the NODA flag set, we check whether it does
       have an empty authValue. If NODA is not set, then we don't check because
       that would increment the DA lockout counter */
    if ((outPublic->publicArea.objectAttributes & TPMA_OBJECT_NODA) != 0) {
        ESYS_TR session;
        TPMT_SYM_DEF sym = {.algorithm = TPM2_ALG_AES,
                            .keyBits = {.aes = 128},
                            .mode = {.aes = TPM2_ALG_CFB}
        };

        /* Esys_StartAuthSession() and session handling use OpenSSL for random
           bytes and thus might end up inside this engine again. This becomes
           a problem if we have no resource manager, i.e. the tpm simulator. */
        const RAND_METHOD *rand_save = RAND_get_rand_method();
#if OPENSSL_VERSION_NUMBER < 0x10100000
        RAND_set_rand_method(RAND_SSLeay());
#else /* OPENSSL_VERSION_NUMBER < 0x10100000 */
        RAND_set_rand_method(RAND_OpenSSL());
#endif

        /* We do the check by starting a bound audit session and executing a
           very cheap command. */
        r = Esys_StartAuthSession(prov->esys_ctx, ESYS_TR_NONE, keyHandle,
                                  ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
                                  NULL, TPM2_SE_HMAC, &sym, TPM2_ALG_SHA256,
                                  &session);
        /* Though this response code is sub-optimal, it's the only way to
           detect the bug in ESYS. */
        if (r == TSS2_ESYS_RC_GENERAL_FAILURE) {
            DBG("Running tpm2-tss < 2.2 which has a bug here. Requiring auth.");
            tpm2Data->emptyAuth = 0;
            goto session_error;
        } else if (r) {
            TPM2_ERROR_raise(prov, TPM2TSS_R_GENERAL_FAILURE);
            goto error;
        }
        Esys_TRSess_SetAttributes(prov->esys_ctx, session,
                                  TPMA_SESSION_ENCRYPT, TPMA_SESSION_ENCRYPT);
        Esys_TRSess_SetAttributes(prov->esys_ctx, session,
                                  TPMA_SESSION_CONTINUESESSION,
                                  TPMA_SESSION_CONTINUESESSION);

        r = Esys_ReadPublic(prov->esys_ctx, keyHandle,
                            session, ESYS_TR_NONE, ESYS_TR_NONE,
                            NULL, NULL, NULL);

        RAND_set_rand_method(rand_save);

        /* tpm2-tss < 2.2 has some bugs. (1) it may miscalculate the auth from
           above leading to a password query in case of empty auth and (2) it
           may return an error because the object's auth value is "\0". */
        if (r == TSS2_RC_SUCCESS) {
            DBG("Object does not require auth");
            tpm2Data->emptyAuth = 1;
        } else if (r == (TPM2_RC_BAD_AUTH | TPM2_RC_S | TPM2_RC_1)) {
            DBG("Object does require auth");
            tpm2Data->emptyAuth = 0;
        } else {
            TPM2_ERROR_raise(prov, TPM2TSS_R_GENERAL_FAILURE);
            goto error;
        }

        Esys_FlushContext(prov->esys_ctx, session);
    }

session_error:

    Esys_TR_Close(prov->esys_ctx, &keyHandle);

    tpm2Data->pub = *outPublic;

    *tpm2Datap = tpm2Data;
    return 1;
 error:
    if (keyHandle != ESYS_TR_NONE)
        Esys_TR_Close(prov->esys_ctx, &keyHandle);
    if (tpm2Data)
        OPENSSL_free(tpm2Data);
    return 0;
}

static TPM2B_PUBLIC primaryEccTemplate = TPM2B_PUBLIC_PRIMARY_ECC_TEMPLATE;
static TPM2B_PUBLIC primaryRsaTemplate = TPM2B_PUBLIC_PRIMARY_RSA_TEMPLATE;

static TPM2B_SENSITIVE_CREATE primarySensitive = {
    .sensitive = {
        .userAuth = {
             .size = 0,
         },
        .data = {
             .size = 0,
         }
    }
};

static TPM2B_DATA allOutsideInfo = {
    .size = 0,
};

static TPML_PCR_SELECTION allCreationPCR = {
    .count = 0,
};

/** Initialize the ESYS TPM connection and primary/persistent key
 *
 * Establish a connection with the TPM using ESYS libraries and create a primary
 * key under the owner hierarchy or to initialize the ESYS object for a
 * persistent if provided.
 * @param esys_ctx The resulting ESYS context.
 * @param parentHandle The TPM handle of a persistent key or TPM2_RH_OWNER or 0
 * @param parent The resulting ESYS_TR handle for the parent key.
 * @retval TSS2_RC_SUCCESS on success
 * @retval TSS2_RCs according to the error
 */
TSS2_RC
init_tpm_parent(TPM2_PROVIDER_CTX *prov,
                TPM2_HANDLE parentHandle, ESYS_TR *parent)
{
    TSS2_RC r;
    TPM2B_PUBLIC *primaryTemplate = NULL;
    TPMS_CAPABILITY_DATA *capabilityData = NULL;
    UINT32 index;
    *parent = ESYS_TR_NONE;

    if (parentHandle && parentHandle != TPM2_RH_OWNER) {
        DBG("Connecting to a persistent parent key.\n");
        r = Esys_TR_FromTPMPublic(prov->esys_ctx, parentHandle,
                                  ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
                                  parent);
        TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);

        r = Esys_TR_SetAuth(prov->esys_ctx, *parent, &parentauth);
        TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);

        return TSS2_RC_SUCCESS;
    }

    DBG("Creating primary key under owner.\n");
    r = Esys_TR_SetAuth(prov->esys_ctx, ESYS_TR_RH_OWNER, &ownerauth);
    TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);

    r = Esys_GetCapability(prov->esys_ctx,
                           ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
                           TPM2_CAP_ALGS, 0, TPM2_MAX_CAP_ALGS,
                           NULL, &capabilityData);
    TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);

    for (index = 0; index < capabilityData->data.algorithms.count; index++) {
        if (capabilityData->data.algorithms.algProperties[index].alg == TPM2_ALG_ECC) {
            primaryTemplate = &primaryEccTemplate;
            break;
        }
    }

    if (primaryTemplate == NULL) {
        for (index = 0; index < capabilityData->data.algorithms.count; index++) {
            if (capabilityData->data.algorithms.algProperties[index].alg == TPM2_ALG_RSA) {
                primaryTemplate = &primaryRsaTemplate;
                break;
            }
        }
    }

    if (capabilityData != NULL)
        free (capabilityData);

    if (primaryTemplate == NULL) {
        TPM2_ERROR_raise(prov, TPM2TSS_R_UNKNOWN_ALG);
        goto error;
    }

    r = Esys_CreatePrimary(prov->esys_ctx, ESYS_TR_RH_OWNER,
                           ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                           &primarySensitive, primaryTemplate, &allOutsideInfo,
                           &allCreationPCR,
                           parent, NULL, NULL, NULL, NULL);
    if (r == 0x000009a2) {
        TPM2_ERROR_raise(prov, TPM2TSS_R_OWNER_AUTH_FAILED);
        goto error;
    }
    TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);

    return TSS2_RC_SUCCESS;
 error:
    if (*parent != ESYS_TR_NONE)
        Esys_FlushContext(prov->esys_ctx, *parent);
    *parent = ESYS_TR_NONE;

    return r;
}

/** Initialize the ESYS TPM connection and load the key
 *
 * Establish a connection with the TPM using ESYS libraries, create a primary
 * key under the owner hierarchy and then load the TPM key and set its auth
 * value.
 * @param esys_ctx The ESYS_CONTEXT to be populated.
 * @param keyHandle The resulting handle for the key key.
 * @param tpm2Data The key data, owner auth and key auth to be used
 * @retval TSS2_RC_SUCCESS on success
 * @retval TSS2_RCs according to the error
 */
TSS2_RC
tpm2_init_key(TPM2_PROVIDER_CTX *prov, TPM2_DATA *tpm2Data, ESYS_TR *keyObject)
{
    TSS2_RC r = 0;
    ESYS_TR parent = ESYS_TR_NONE;

    *keyObject = ESYS_TR_NONE;
    if (tpm2Data->privatetype == KEY_TYPE_HANDLE) {
        r = Esys_TR_FromTPMPublic(prov->esys_ctx, tpm2Data->handle,
                                  ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
                                  keyObject);
        TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);
    } else if (tpm2Data->privatetype == KEY_TYPE_BLOB
               && tpm2Data->parent != TPM2_RH_OWNER) {
        r = init_tpm_parent(prov, tpm2Data->parent, &parent);
        TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);

        DBG("Loading key blob wth custom parent.\n");
        r = Esys_Load(prov->esys_ctx, parent,
                      ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                      &tpm2Data->priv, &tpm2Data->pub, keyObject);
        Esys_TR_Close(prov->esys_ctx, &parent);
        TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);
    } else if (tpm2Data->privatetype == KEY_TYPE_BLOB) {
        r = init_tpm_parent(prov, 0, &parent);
        TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);

        DBG("Loading key blob.\n");
        r = Esys_Load(prov->esys_ctx, parent,
                      ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                      &tpm2Data->priv, &tpm2Data->pub, keyObject);
        TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);

        r = Esys_FlushContext(prov->esys_ctx, parent);
        TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);
        parent = ESYS_TR_NONE;
    } else {
        TPM2_ERROR_raise(prov, TPM2TSS_R_TPM2DATA_READ_FAILED);
        return -1;
    }

    r = Esys_TR_SetAuth(prov->esys_ctx, *keyObject, &tpm2Data->userauth);
    TPM2_CHECK_RC(prov, r, TPM2TSS_R_GENERAL_FAILURE, goto error);

    return TSS2_RC_SUCCESS;
 error:
    if (parent != ESYS_TR_NONE)
        Esys_FlushContext(prov->esys_ctx, parent);

    if (*keyObject != ESYS_TR_NONE)
        Esys_FlushContext(prov->esys_ctx, *keyObject);
    *keyObject = ESYS_TR_NONE;

    return r;
}
