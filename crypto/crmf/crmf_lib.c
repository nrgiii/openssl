/*-
 * Copyright 2007-2020 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright Nokia 2007-2018
 * Copyright Siemens AG 2015-2019
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * CRMF implementation by Martin Peylo, Miikka Viljanen, and David von Oheimb.
 */

/*
 * This file contains the functions that handle the individual items inside
 * the CRMF structures
 */

/*
 * NAMING
 *
 * The 0 functions use the supplied structure pointer directly in the parent and
 * it will be freed up when the parent is freed.
 *
 * The 1 functions use a copy of the supplied structure pointer (or in some
 * cases increases its link count) in the parent and so both should be freed up.
 */

#include <openssl/asn1t.h>

#include "crmf_local.h"
#include "internal/constant_time.h"

/* explicit #includes not strictly needed since implied by the above: */
#include <openssl/crmf.h>
#include <openssl/err.h>
#include <openssl/evp.h>

DEFINE_STACK_OF(X509_EXTENSION)
DEFINE_STACK_OF(OSSL_CRMF_MSG)

/*-
 * atyp = Attribute Type
 * valt = Value Type
 * ctrlinf = "regCtrl" or "regInfo"
 */
#define IMPLEMENT_CRMF_CTRL_FUNC(atyp, valt, ctrlinf)                     \
int OSSL_CRMF_MSG_set1_##ctrlinf##_##atyp(OSSL_CRMF_MSG *msg,             \
                                          const valt *in)                 \
{                                                                         \
    OSSL_CRMF_ATTRIBUTETYPEANDVALUE *atav = NULL;                         \
                                                                          \
    if (msg == NULL || in == NULL)                                       \
        goto err;                                                         \
    if ((atav = OSSL_CRMF_ATTRIBUTETYPEANDVALUE_new()) == NULL)           \
        goto err;                                                         \
    if ((atav->type = OBJ_nid2obj(NID_id_##ctrlinf##_##atyp)) == NULL)    \
        goto err;                                                         \
    if ((atav->value.atyp = valt##_dup(in)) == NULL)                      \
        goto err;                                                         \
    if (!OSSL_CRMF_MSG_push0_##ctrlinf(msg, atav))                        \
        goto err;                                                         \
    return 1;                                                             \
 err:                                                                     \
    OSSL_CRMF_ATTRIBUTETYPEANDVALUE_free(atav);                           \
    return 0;                                                             \
}


/*-
 * Pushes the given control attribute into the controls stack of a CertRequest
 * (section 6)
 * returns 1 on success, 0 on error
 */
static int OSSL_CRMF_MSG_push0_regCtrl(OSSL_CRMF_MSG *crm,
                                       OSSL_CRMF_ATTRIBUTETYPEANDVALUE *ctrl)
{
    int new = 0;

    if (crm == NULL || crm->certReq == NULL || ctrl == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_PUSH0_REGCTRL, CRMF_R_NULL_ARGUMENT);
        return 0;
    }

    if (crm->certReq->controls == NULL) {
        crm->certReq->controls = sk_OSSL_CRMF_ATTRIBUTETYPEANDVALUE_new_null();
        if (crm->certReq->controls == NULL)
            goto err;
        new = 1;
    }
    if (!sk_OSSL_CRMF_ATTRIBUTETYPEANDVALUE_push(crm->certReq->controls, ctrl))
        goto err;

    return 1;
 err:
    if (new != 0) {
        sk_OSSL_CRMF_ATTRIBUTETYPEANDVALUE_free(crm->certReq->controls);
        crm->certReq->controls = NULL;
    }
    return 0;
}

/* id-regCtrl-regToken Control (section 6.1) */
IMPLEMENT_CRMF_CTRL_FUNC(regToken, ASN1_STRING, regCtrl)

/* id-regCtrl-authenticator Control (section 6.2) */
#define ASN1_UTF8STRING_dup ASN1_STRING_dup
IMPLEMENT_CRMF_CTRL_FUNC(authenticator, ASN1_UTF8STRING, regCtrl)

int OSSL_CRMF_MSG_set0_SinglePubInfo(OSSL_CRMF_SINGLEPUBINFO *spi,
                                     int method, GENERAL_NAME *nm)
{
    if (spi == NULL
            || method < OSSL_CRMF_PUB_METHOD_DONTCARE
            || method > OSSL_CRMF_PUB_METHOD_LDAP) {
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_SET0_SINGLEPUBINFO,
                ERR_R_PASSED_INVALID_ARGUMENT);
        return 0;
    }

    if (!ASN1_INTEGER_set(spi->pubMethod, method))
        return 0;
    GENERAL_NAME_free(spi->pubLocation);
    spi->pubLocation = nm;
    return 1;
}

int
OSSL_CRMF_MSG_PKIPublicationInfo_push0_SinglePubInfo(OSSL_CRMF_PKIPUBLICATIONINFO *pi,
                                                     OSSL_CRMF_SINGLEPUBINFO *spi)
{
    if (pi == NULL || spi == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_PKIPUBLICATIONINFO_PUSH0_SINGLEPUBINFO,
                CRMF_R_NULL_ARGUMENT);
        return 0;
    }
    if (pi->pubInfos == NULL)
        pi->pubInfos = sk_OSSL_CRMF_SINGLEPUBINFO_new_null();
    if (pi->pubInfos == NULL)
        return 0;

    return sk_OSSL_CRMF_SINGLEPUBINFO_push(pi->pubInfos, spi);
}

int OSSL_CRMF_MSG_set_PKIPublicationInfo_action(OSSL_CRMF_PKIPUBLICATIONINFO *pi,
                                                int action)
{
    if (pi == NULL
            || action < OSSL_CRMF_PUB_ACTION_DONTPUBLISH
            || action > OSSL_CRMF_PUB_ACTION_PLEASEPUBLISH) {
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_SET_PKIPUBLICATIONINFO_ACTION,
                ERR_R_PASSED_INVALID_ARGUMENT);
        return 0;
    }

    return ASN1_INTEGER_set(pi->action, action);
}

/* id-regCtrl-pkiPublicationInfo Control (section 6.3) */
IMPLEMENT_CRMF_CTRL_FUNC(pkiPublicationInfo, OSSL_CRMF_PKIPUBLICATIONINFO,
                         regCtrl)

/* id-regCtrl-oldCertID Control (section 6.5) from the given */
IMPLEMENT_CRMF_CTRL_FUNC(oldCertID, OSSL_CRMF_CERTID, regCtrl)

OSSL_CRMF_CERTID *OSSL_CRMF_CERTID_gen(const X509_NAME *issuer,
                                       const ASN1_INTEGER *serial)
{
    OSSL_CRMF_CERTID *cid = NULL;

    if (issuer == NULL || serial == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_CERTID_GEN, CRMF_R_NULL_ARGUMENT);
        return NULL;
    }

    if ((cid = OSSL_CRMF_CERTID_new()) == NULL)
        goto err;

    if (!X509_NAME_set(&cid->issuer->d.directoryName, issuer))
        goto err;
    cid->issuer->type = GEN_DIRNAME;

    ASN1_INTEGER_free(cid->serialNumber);
    if ((cid->serialNumber = ASN1_INTEGER_dup(serial)) == NULL)
        goto err;

    return cid;

 err:
    OSSL_CRMF_CERTID_free(cid);
    return NULL;
}

/*
 * id-regCtrl-protocolEncrKey Control (section 6.6)
 */
IMPLEMENT_CRMF_CTRL_FUNC(protocolEncrKey, X509_PUBKEY, regCtrl)

/*-
 * Pushes the attribute given in regInfo in to the CertReqMsg->regInfo stack.
 * (section 7)
 * returns 1 on success, 0 on error
 */
static int OSSL_CRMF_MSG_push0_regInfo(OSSL_CRMF_MSG *crm,
                                       OSSL_CRMF_ATTRIBUTETYPEANDVALUE *ri)
{
    STACK_OF(OSSL_CRMF_ATTRIBUTETYPEANDVALUE) *info = NULL;

    if (crm == NULL || ri == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_PUSH0_REGINFO, CRMF_R_NULL_ARGUMENT);
        return 0;
    }

    if (crm->regInfo == NULL)
        crm->regInfo = info = sk_OSSL_CRMF_ATTRIBUTETYPEANDVALUE_new_null();
    if (crm->regInfo == NULL)
        goto err;
    if (!sk_OSSL_CRMF_ATTRIBUTETYPEANDVALUE_push(crm->regInfo, ri))
        goto err;
    return 1;

 err:
    if (info != NULL)
        crm->regInfo = NULL;
    sk_OSSL_CRMF_ATTRIBUTETYPEANDVALUE_free(info);
    return 0;
}

/* id-regInfo-utf8Pairs to regInfo (section 7.1) */
IMPLEMENT_CRMF_CTRL_FUNC(utf8Pairs, ASN1_UTF8STRING, regInfo)

/* id-regInfo-certReq to regInfo (section 7.2) */
IMPLEMENT_CRMF_CTRL_FUNC(certReq, OSSL_CRMF_CERTREQUEST, regInfo)


/* retrieves the certificate template of crm */
OSSL_CRMF_CERTTEMPLATE *OSSL_CRMF_MSG_get0_tmpl(const OSSL_CRMF_MSG *crm)
{
    if (crm == NULL || crm->certReq == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_GET0_TMPL, CRMF_R_NULL_ARGUMENT);
        return NULL;
    }
    return crm->certReq->certTemplate;
}


int OSSL_CRMF_MSG_set_validity(OSSL_CRMF_MSG *crm, time_t from, time_t to)
{
    OSSL_CRMF_OPTIONALVALIDITY *vld = NULL;
    ASN1_TIME *from_asn = NULL;
    ASN1_TIME *to_asn = NULL;
    OSSL_CRMF_CERTTEMPLATE *tmpl = OSSL_CRMF_MSG_get0_tmpl(crm);

    if (tmpl == NULL) { /* also crm == NULL implies this */
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_SET_VALIDITY, CRMF_R_NULL_ARGUMENT);
        return 0;
    }

    if (from != 0 && ((from_asn = ASN1_TIME_set(NULL, from)) == NULL))
        goto err;
    if (to != 0 && ((to_asn = ASN1_TIME_set(NULL, to)) == NULL))
        goto err;
    if ((vld = OSSL_CRMF_OPTIONALVALIDITY_new()) == NULL)
        goto err;

    vld->notBefore = from_asn;
    vld->notAfter = to_asn;

    tmpl->validity = vld;

    return 1;
 err:
    ASN1_TIME_free(from_asn);
    ASN1_TIME_free(to_asn);
    return 0;
}


int OSSL_CRMF_MSG_set_certReqId(OSSL_CRMF_MSG *crm, int rid)
{
    if (crm == NULL || crm->certReq == NULL || crm->certReq->certReqId == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_SET_CERTREQID, CRMF_R_NULL_ARGUMENT);
        return 0;
    }

    return ASN1_INTEGER_set(crm->certReq->certReqId, rid);
}

/* get ASN.1 encoded integer, return -1 on error */
static int crmf_asn1_get_int(const ASN1_INTEGER *a)
{
    int64_t res;

    if (!ASN1_INTEGER_get_int64(&res, a)) {
        CRMFerr(0, ASN1_R_INVALID_NUMBER);
        return -1;
    }
    if (res < INT_MIN) {
        CRMFerr(0, ASN1_R_TOO_SMALL);
        return -1;
    }
    if (res > INT_MAX) {
        CRMFerr(0, ASN1_R_TOO_LARGE);
        return -1;
    }
    return (int)res;
}

int OSSL_CRMF_MSG_get_certReqId(const OSSL_CRMF_MSG *crm)
{
    if (crm == NULL || /* not really needed: */ crm->certReq == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_GET_CERTREQID, CRMF_R_NULL_ARGUMENT);
        return -1;
    }
    return crmf_asn1_get_int(crm->certReq->certReqId);
}


int OSSL_CRMF_MSG_set0_extensions(OSSL_CRMF_MSG *crm,
                                  X509_EXTENSIONS *exts)
{
    OSSL_CRMF_CERTTEMPLATE *tmpl = OSSL_CRMF_MSG_get0_tmpl(crm);

    if (tmpl == NULL) { /* also crm == NULL implies this */
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_SET0_EXTENSIONS, CRMF_R_NULL_ARGUMENT);
        return 0;
    }

    if (sk_X509_EXTENSION_num(exts) == 0) {
        sk_X509_EXTENSION_free(exts);
        exts = NULL; /* do not include empty extensions list */
    }

    sk_X509_EXTENSION_pop_free(tmpl->extensions, X509_EXTENSION_free);
    tmpl->extensions = exts;
    return 1;
}


int OSSL_CRMF_MSG_push0_extension(OSSL_CRMF_MSG *crm,
                                  X509_EXTENSION *ext)
{
    int new = 0;
    OSSL_CRMF_CERTTEMPLATE *tmpl = OSSL_CRMF_MSG_get0_tmpl(crm);

    if (tmpl == NULL || ext == NULL) { /* also crm == NULL implies this */
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_PUSH0_EXTENSION, CRMF_R_NULL_ARGUMENT);
        return 0;
    }

    if (tmpl->extensions == NULL) {
        if ((tmpl->extensions = sk_X509_EXTENSION_new_null()) == NULL)
            goto err;
        new = 1;
    }

    if (!sk_X509_EXTENSION_push(tmpl->extensions, ext))
        goto err;
    return 1;
 err:
    if (new != 0) {
        sk_X509_EXTENSION_free(tmpl->extensions);
        tmpl->extensions = NULL;
    }
    return 0;
}

/* TODO: support cases 1+2 (besides case 3) defined in RFC 4211, section 4.1. */
static int CRMF_poposigningkey_init(OSSL_CRMF_POPOSIGNINGKEY *ps,
                                    OSSL_CRMF_CERTREQUEST *cr,
                                    EVP_PKEY *pkey, int dgst)
{
    int ret = 0;
    EVP_MD *fetched_md = NULL;
    const EVP_MD *md = EVP_get_digestbynid(dgst);

    if (ps == NULL || cr == NULL || pkey == NULL) {
        CRMFerr(CRMF_F_CRMF_POPOSIGNINGKEY_INIT, CRMF_R_NULL_ARGUMENT);
        return 0;
    }

    /* If we didn't find legacy MD, we try an implicit fetch */
    if (md == NULL)
        md = fetched_md = EVP_MD_fetch(NULL, OBJ_nid2sn(dgst), NULL);

    if (md == NULL) {
        CRMFerr(CRMF_F_CRMF_POPOSIGNINGKEY_INIT,
                CRMF_R_UNSUPPORTED_ALG_FOR_POPSIGNINGKEY);
        return 0;
    }

    ret = ASN1_item_sign(ASN1_ITEM_rptr(OSSL_CRMF_CERTREQUEST),
                         ps->algorithmIdentifier, NULL, ps->signature,
                         cr, pkey, md);

    EVP_MD_free(fetched_md);
    return ret;
}


int OSSL_CRMF_MSG_create_popo(OSSL_CRMF_MSG *crm, EVP_PKEY *pkey,
                              int dgst, int ppmtd)
{
    OSSL_CRMF_POPO *pp = NULL;
    ASN1_INTEGER *tag = NULL;

    if (crm == NULL || (ppmtd == OSSL_CRMF_POPO_SIGNATURE && pkey == NULL)) {
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_CREATE_POPO, CRMF_R_NULL_ARGUMENT);
        return 0;
    }

    if (ppmtd == OSSL_CRMF_POPO_NONE)
        goto end;
    if ((pp = OSSL_CRMF_POPO_new()) == NULL)
        goto err;
    pp->type = ppmtd;

    switch (ppmtd) {
    case OSSL_CRMF_POPO_RAVERIFIED:
        if ((pp->value.raVerified = ASN1_NULL_new()) == NULL)
            goto err;
        break;

    case OSSL_CRMF_POPO_SIGNATURE:
        {
            OSSL_CRMF_POPOSIGNINGKEY *ps = OSSL_CRMF_POPOSIGNINGKEY_new();
            if (ps == NULL
                    || !CRMF_poposigningkey_init(ps, crm->certReq, pkey, dgst)) {
                OSSL_CRMF_POPOSIGNINGKEY_free(ps);
                goto err;
            }
            pp->value.signature = ps;
        }
        break;

    case OSSL_CRMF_POPO_KEYENC:
        if ((pp->value.keyEncipherment = OSSL_CRMF_POPOPRIVKEY_new()) == NULL)
            goto err;
        tag = ASN1_INTEGER_new();
        pp->value.keyEncipherment->type =
            OSSL_CRMF_POPOPRIVKEY_SUBSEQUENTMESSAGE;
        pp->value.keyEncipherment->value.subsequentMessage = tag;
        if (tag == NULL
                || !ASN1_INTEGER_set(tag, OSSL_CRMF_SUBSEQUENTMESSAGE_ENCRCERT))
            goto err;
        break;

    default:
        CRMFerr(CRMF_F_OSSL_CRMF_MSG_CREATE_POPO,
                CRMF_R_UNSUPPORTED_METHOD_FOR_CREATING_POPO);
        goto err;
    }

 end:
    OSSL_CRMF_POPO_free(crm->popo);
    crm->popo = pp;

    return 1;
 err:
    OSSL_CRMF_POPO_free(pp);
    return 0;
}

/* returns 0 for equal, -1 for a < b or error on a, 1 for a > b or error on b */
static int X509_PUBKEY_cmp(X509_PUBKEY *a, X509_PUBKEY *b)
{
    X509_ALGOR *algA = NULL, *algB = NULL;
    int res = 0;

    if (a == b)
        return 0;
    if (a == NULL || !X509_PUBKEY_get0_param(NULL, NULL, NULL, &algA, a)
            || algA == NULL)
        return -1;
    if (b == NULL || !X509_PUBKEY_get0_param(NULL, NULL, NULL, &algB, b)
            || algB == NULL)
        return 1;
    if ((res = X509_ALGOR_cmp(algA, algB)) != 0)
        return res;
    return EVP_PKEY_cmp(X509_PUBKEY_get0(a), X509_PUBKEY_get0(b));
}

/* verifies the Proof-of-Possession of the request with the given rid in reqs */
int OSSL_CRMF_MSGS_verify_popo(const OSSL_CRMF_MSGS *reqs,
                               int rid, int acceptRAVerified)
{
    OSSL_CRMF_MSG *req = NULL;
    X509_PUBKEY *pubkey = NULL;
    OSSL_CRMF_POPOSIGNINGKEY *sig = NULL;

    if (reqs == NULL || (req = sk_OSSL_CRMF_MSG_value(reqs, rid)) == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_MSGS_VERIFY_POPO, CRMF_R_NULL_ARGUMENT);
        return 0;
    }

    if (req->popo == NULL) {
        CRMFerr(0, CRMF_R_POPO_MISSING);
        return 0;
    }

    switch (req->popo->type) {
    case OSSL_CRMF_POPO_RAVERIFIED:
        if (!acceptRAVerified) {
            CRMFerr(0, CRMF_R_POPO_RAVERIFIED_NOT_ACCEPTED);
            return 0;
        }
        break;
    case OSSL_CRMF_POPO_SIGNATURE:
        pubkey = req->certReq->certTemplate->publicKey;
        if (pubkey == NULL) {
            CRMFerr(0, CRMF_R_POPO_MISSING_PUBLIC_KEY);
            return 0;
        }
        sig = req->popo->value.signature;
        if (sig->poposkInput != NULL) {
            /*
             * According to RFC 4211: publicKey contains a copy of
             * the public key from the certificate template. This MUST be
             * exactly the same value as contained in the certificate template.
             */
            if (sig->poposkInput->publicKey == NULL) {
                CRMFerr(0, CRMF_R_POPO_MISSING_PUBLIC_KEY);
                return 0;
            }
            if (X509_PUBKEY_cmp(pubkey, sig->poposkInput->publicKey) != 0) {
                CRMFerr(0, CRMF_R_POPO_INCONSISTENT_PUBLIC_KEY);
                return 0;
            }
            /*
             * TODO check the contents of the authInfo sub-field,
             * see RFC 4211 https://tools.ietf.org/html/rfc4211#section-4.1
             */
            if (ASN1_item_verify(ASN1_ITEM_rptr(OSSL_CRMF_POPOSIGNINGKEYINPUT),
                                 sig->algorithmIdentifier, sig->signature,
                                 sig->poposkInput,
                                 X509_PUBKEY_get0(pubkey)) < 1)
                return 0;
        } else {
            if (req->certReq->certTemplate->subject == NULL) {
                CRMFerr(0, CRMF_R_POPO_MISSING_SUBJECT);
                return 0;
            }
            if (ASN1_item_verify(ASN1_ITEM_rptr(OSSL_CRMF_CERTREQUEST),
                                 sig->algorithmIdentifier, sig->signature,
                                 req->certReq, X509_PUBKEY_get0(pubkey)) < 1)
                return 0;
        }
        break;
    case OSSL_CRMF_POPO_KEYENC:
        /*
         * TODO: when OSSL_CMP_certrep_new() supports encrypted certs,
         * return 1 if the type of req->popo->value.keyEncipherment
         * is OSSL_CRMF_POPOPRIVKEY_SUBSEQUENTMESSAGE and
         * its value.subsequentMessage == OSSL_CRMF_SUBSEQUENTMESSAGE_ENCRCERT
         */
    case OSSL_CRMF_POPO_KEYAGREE:
    default:
        CRMFerr(CRMF_F_OSSL_CRMF_MSGS_VERIFY_POPO,
                CRMF_R_UNSUPPORTED_POPO_METHOD);
        return 0;
    }
    return 1;
}

/* retrieves the serialNumber of the given cert template or NULL on error */
ASN1_INTEGER
*OSSL_CRMF_CERTTEMPLATE_get0_serialNumber(const OSSL_CRMF_CERTTEMPLATE *tmpl)
{
    return tmpl != NULL ? tmpl->serialNumber : NULL;
}

/* retrieves the issuer name of the given cert template or NULL on error */
const X509_NAME
    *OSSL_CRMF_CERTTEMPLATE_get0_issuer(const OSSL_CRMF_CERTTEMPLATE *tmpl)
{
    return tmpl != NULL ? tmpl->issuer : NULL;
}

/* retrieves the issuer name of the given CertId or NULL on error */
const X509_NAME *OSSL_CRMF_CERTID_get0_issuer(const OSSL_CRMF_CERTID *cid)
{
    return cid != NULL && cid->issuer->type == GEN_DIRNAME ?
        cid->issuer->d.directoryName : NULL;
}

/* retrieves the serialNumber of the given CertId or NULL on error */
ASN1_INTEGER *OSSL_CRMF_CERTID_get0_serialNumber(const OSSL_CRMF_CERTID *cid)
{
    return cid != NULL ? cid->serialNumber : NULL;
}

/*-
 * fill in certificate template.
 * Any value argument that is NULL will leave the respective field unchanged.
 */
int OSSL_CRMF_CERTTEMPLATE_fill(OSSL_CRMF_CERTTEMPLATE *tmpl,
                                EVP_PKEY *pubkey,
                                const X509_NAME *subject,
                                const X509_NAME *issuer,
                                const ASN1_INTEGER *serial)
{
    if (tmpl == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_CERTTEMPLATE_FILL, CRMF_R_NULL_ARGUMENT);
        return 0;
    }
    if (subject != NULL && !X509_NAME_set((X509_NAME **)&tmpl->subject, subject))
        return 0;
    if (issuer != NULL && !X509_NAME_set((X509_NAME **)&tmpl->issuer, issuer))
        return 0;
    if (serial != NULL) {
        ASN1_INTEGER_free(tmpl->serialNumber);
        if ((tmpl->serialNumber = ASN1_INTEGER_dup(serial)) == NULL)
            return 0;
    }
    if (pubkey != NULL && !X509_PUBKEY_set(&tmpl->publicKey, pubkey))
        return 0;
    return 1;
}


/*-
 * Decrypts the certificate in the given encryptedValue using private key pkey.
 * This is needed for the indirect PoP method as in RFC 4210 section 5.2.8.2.
 *
 * returns a pointer to the decrypted certificate
 * returns NULL on error or if no certificate available
 */
X509 *OSSL_CRMF_ENCRYPTEDVALUE_get1_encCert(const OSSL_CRMF_ENCRYPTEDVALUE *ecert,
                                            EVP_PKEY *pkey)
{
    X509 *cert = NULL; /* decrypted certificate */
    EVP_CIPHER_CTX *evp_ctx = NULL; /* context for symmetric encryption */
    unsigned char *ek = NULL; /* decrypted symmetric encryption key */
    size_t eksize = 0; /* size of decrypted symmetric encryption key */
    const EVP_CIPHER *cipher = NULL; /* used cipher */
    int cikeysize = 0; /* key size from cipher */
    unsigned char *iv = NULL; /* initial vector for symmetric encryption */
    unsigned char *outbuf = NULL; /* decryption output buffer */
    const unsigned char *p = NULL; /* needed for decoding ASN1 */
    int symmAlg = 0; /* NIDs for symmetric algorithm */
    int n, outlen = 0;
    EVP_PKEY_CTX *pkctx = NULL; /* private key context */

    if (ecert == NULL || ecert->symmAlg == NULL || ecert->encSymmKey == NULL
            || ecert->encValue == NULL || pkey == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_ENCRYPTEDVALUE_GET1_ENCCERT,
                CRMF_R_NULL_ARGUMENT);
        return NULL;
    }
    if ((symmAlg = OBJ_obj2nid(ecert->symmAlg->algorithm)) == 0) {
        CRMFerr(CRMF_F_OSSL_CRMF_ENCRYPTEDVALUE_GET1_ENCCERT,
                CRMF_R_UNSUPPORTED_CIPHER);
        return NULL;
    }
    /* select symmetric cipher based on algorithm given in message */
    if ((cipher = EVP_get_cipherbynid(symmAlg)) == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_ENCRYPTEDVALUE_GET1_ENCCERT,
                CRMF_R_UNSUPPORTED_CIPHER);
        goto end;
    }
    cikeysize = EVP_CIPHER_key_length(cipher);
    /* first the symmetric key needs to be decrypted */
    pkctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (pkctx != NULL && EVP_PKEY_decrypt_init(pkctx)) {
        ASN1_BIT_STRING *encKey = ecert->encSymmKey;
        size_t failure;
        int retval;

        if (EVP_PKEY_decrypt(pkctx, NULL, &eksize,
                             encKey->data, encKey->length) <= 0
                || (ek = OPENSSL_malloc(eksize)) == NULL)
            goto end;
        retval = EVP_PKEY_decrypt(pkctx, ek, &eksize,
                                  encKey->data, encKey->length);
        ERR_clear_error(); /* error state may have sensitive information */
        failure = ~constant_time_is_zero_s(constant_time_msb(retval)
                                           | constant_time_is_zero(retval));
        failure |= ~constant_time_eq_s(eksize, (size_t)cikeysize);
        if (failure) {
            CRMFerr(CRMF_F_OSSL_CRMF_ENCRYPTEDVALUE_GET1_ENCCERT,
                    CRMF_R_ERROR_DECRYPTING_SYMMETRIC_KEY);
            goto end;
        }
    } else {
        goto end;
    }
    if ((iv = OPENSSL_malloc(EVP_CIPHER_iv_length(cipher))) == NULL)
        goto end;
    if (ASN1_TYPE_get_octetstring(ecert->symmAlg->parameter, iv,
                                  EVP_CIPHER_iv_length(cipher))
        != EVP_CIPHER_iv_length(cipher)) {
        CRMFerr(CRMF_F_OSSL_CRMF_ENCRYPTEDVALUE_GET1_ENCCERT,
                CRMF_R_MALFORMED_IV);
        goto end;
    }

    /*
     * d2i_X509 changes the given pointer, so use p for decoding the message and
     * keep the original pointer in outbuf so the memory can be freed later
     */
    if ((p = outbuf = OPENSSL_malloc(ecert->encValue->length +
                                     EVP_CIPHER_block_size(cipher))) == NULL
            || (evp_ctx = EVP_CIPHER_CTX_new()) == NULL)
        goto end;
    EVP_CIPHER_CTX_set_padding(evp_ctx, 0);

    if (!EVP_DecryptInit(evp_ctx, cipher, ek, iv)
            || !EVP_DecryptUpdate(evp_ctx, outbuf, &outlen,
                                  ecert->encValue->data,
                                  ecert->encValue->length)
            || !EVP_DecryptFinal(evp_ctx, outbuf + outlen, &n)) {
        CRMFerr(CRMF_F_OSSL_CRMF_ENCRYPTEDVALUE_GET1_ENCCERT,
                CRMF_R_ERROR_DECRYPTING_CERTIFICATE);
        goto end;
    }
    outlen += n;

    /* convert decrypted certificate from DER to internal ASN.1 structure */
    if ((cert = d2i_X509(NULL, &p, outlen)) == NULL) {
        CRMFerr(CRMF_F_OSSL_CRMF_ENCRYPTEDVALUE_GET1_ENCCERT,
                CRMF_R_ERROR_DECODING_CERTIFICATE);
    }
 end:
    EVP_PKEY_CTX_free(pkctx);
    OPENSSL_free(outbuf);
    EVP_CIPHER_CTX_free(evp_ctx);
    OPENSSL_clear_free(ek, eksize);
    OPENSSL_free(iv);
    return cert;
}
