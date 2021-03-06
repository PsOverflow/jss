/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <nspr.h>
#include <secitem.h>
#include <secoidt.h>
#include <secmodt.h>
#include <pk11func.h>
#include "_jni/org_mozilla_jss_pkcs11_PK11Signature.h"
#include "_jni/org_mozilla_jss_pkcs11_SigContextProxy.h"
#include <Algorithm.h>
#include <secerr.h>
#include <cryptoht.h>
#include <cryptohi.h>
#include <keyhi.h>

#include <jssutil.h>
#include <java_ids.h>
#include <jss_exceptions.h>
#include "pk11util.h"

static PRStatus
getPrivateKey(JNIEnv *env, jobject sig, SECKEYPrivateKey**key);

static PRStatus
getPublicKey(JNIEnv *env, jobject sig, SECKEYPublicKey**key);

static PRStatus
getSomeKey(JNIEnv *env, jobject sig, void **key, short type);

static SECOidTag
getAlgorithm(JNIEnv *env, jobject sig);

static SECOidTag
getDigestAlgorithm(JNIEnv *env, jobject sig);

static void
setSigContext(JNIEnv *env, jobject sig, jobject context);

static PRStatus
getSigContext(JNIEnv *env, jobject sig, void**pContext, SigContextType* pType);

static SECStatus
getRSAPSSParamsAndSigningAlg(JNIEnv *env, jobject this, PRArenaPool *arena,
    SECAlgorithmID **alg, SECKEYPrivateKey *privk);

/***********************************************************************
 *
 * PK11Signature.initSigContext
 */
JNIEXPORT void JNICALL
Java_org_mozilla_jss_pkcs11_PK11Signature_initSigContext
  (JNIEnv *env, jobject this)
{
    SGNContext *ctxt = NULL;
    jobject contextProxy = NULL;
    SECKEYPrivateKey *privk = NULL;
    SECAlgorithmID *signAlg = NULL;
    SECStatus rv = SECFailure;
    PRArenaPool *arena = NULL;
    SECOidTag signingAlg = SEC_OID_UNKNOWN;

    /* Extract the private key from the PK11Signature */
    if (getPrivateKey(env, this, &privk) != PR_SUCCESS) {
        PR_ASSERT((*env)->ExceptionOccurred(env) != NULL);
        goto finish;
    }

    signingAlg = getAlgorithm(env,this);
    if (signingAlg == SEC_OID_PKCS1_RSA_PSS_SIGNATURE) {
        arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
        if (!arena) {
           JSS_throw(env, OUT_OF_MEMORY_ERROR);
           goto finish;
        }

        rv = getRSAPSSParamsAndSigningAlg(env, this, arena, &signAlg, privk);
        if (rv == SECFailure) {
            goto finish;
        }

        /* Start the signing operation */
        ctxt = SGN_NewContextWithAlgorithmID(signAlg, privk);
    } else {
        ctxt = SGN_NewContext(signingAlg, privk);
    }

    if (ctxt == NULL) {
        JSS_throwMsg(env, TOKEN_EXCEPTION, "Unable to create signing context");
        goto finish;
    }

    if (SGN_Begin(ctxt) != SECSuccess) {
        JSS_throwMsg(env, TOKEN_EXCEPTION, "Unable to begin signing context");
        goto finish;
    }

    /* Create a contextProxy and stick it in the PK11Signature object */
    contextProxy = JSS_PK11_wrapSigContextProxy(env, (void**)&ctxt,
                                                SGN_CONTEXT, &arena);

    if (contextProxy == NULL) {
        PR_ASSERT((*env)->ExceptionOccurred(env) != NULL);
        goto finish;
    }

    // Signature algorithm for RSA PSS allocated in the arena,
    // which is destroyed on exit.
    setSigContext(env, this, contextProxy);

finish:
    if (contextProxy == NULL && ctxt != NULL) {
        /* we created a context but not the Java wrapper, so we need to
         * delete the context here. */
        SGN_DestroyContext(ctxt, PR_TRUE /*freeit*/);
    }

    /* When contentProxy is created, arena will be NULLed and contentProxy
     * takes ownership of it. Otherwise, when arena still exists, we must
     * free it now. */
    PORT_FreeArena(arena, PR_TRUE /* zero */);
}

JNIEXPORT void JNICALL
Java_org_mozilla_jss_pkcs11_PK11Signature_initVfyContext
    (JNIEnv *env, jobject this)
{
    VFYContext *ctxt = NULL;
    jobject contextProxy = NULL;
    SECKEYPublicKey *pubk = NULL;
    SECKEYPrivateKey *privk = NULL;
    SECKEYPublicKey *tempPubKey = NULL;

    PRArenaPool *arena = NULL;
    SECAlgorithmID *signAlg = NULL;
    SECStatus rv = SECFailure;
    SECOidTag signingAlg = SEC_OID_UNKNOWN;

    if (getPublicKey(env, this, &pubk) != PR_SUCCESS) {
        PR_ASSERT((*env)->ExceptionOccurred(env) != NULL);
        goto finish;
    }

    signingAlg = getAlgorithm(env,this);
    if (signingAlg == SEC_OID_PKCS1_RSA_PSS_SIGNATURE) {
        /* Create place holder private key, just to create the PSS Params. */
        unsigned key_bits = SECKEY_PublicKeyStrengthInBits(pubk);
        privk = SECKEY_CreateRSAPrivateKey(key_bits, &tempPubKey, NULL);
        if (privk == NULL) {
            JSS_throwMsg(env, TOKEN_EXCEPTION,
                         "Unable to create temporary RSA key");
            goto finish;
        }

        arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
        if (arena == NULL) {
            JSS_throw(env, OUT_OF_MEMORY_ERROR);
            goto finish;
        }

        rv = getRSAPSSParamsAndSigningAlg(env, this, arena, &signAlg, privk);
        if (rv == SECFailure) {
            PR_ASSERT((*env)->ExceptionOccurred(env) != NULL);
            goto finish;
        }

        SECOidTag digestAlg = SEC_OID_UNKNOWN;
        digestAlg = getDigestAlgorithm(env, this);
        ctxt = VFY_CreateContextWithAlgorithmID(pubk, NULL, signAlg,
                                                &digestAlg, NULL);
    } else {
        ctxt = VFY_CreateContext(pubk, NULL /*sig*/, signingAlg,
                                 NULL /*wincx*/);
    }

    if (ctxt == NULL) {
        JSS_throwMsg(env, TOKEN_EXCEPTION, "Unable to create vfy context");
        goto finish;
    }

    if (VFY_Begin(ctxt) != SECSuccess) {
        JSS_throwMsg(env, TOKEN_EXCEPTION,
                     "Unable to begin verification context");
        goto finish;
    }

    /* create a ContextProxy and stick it in the PK11Signature object */
    contextProxy = JSS_PK11_wrapSigContextProxy(env, (void**)&ctxt,
                                                VFY_CONTEXT, &arena);
    if (contextProxy == NULL) {
        PR_ASSERT((*env)->ExceptionOccurred(env) != NULL);
        goto finish;
    }

    setSigContext(env, this, contextProxy);

finish:
    if (contextProxy == NULL && ctxt != NULL) {
        /* we created a context but not the Java wrapper, so we need to
         * delete the context here */
        VFY_DestroyContext(ctxt, PR_TRUE /*freeit*/);
    }

    SECKEY_DestroyPublicKey(tempPubKey);
    SECKEY_DestroyPrivateKey(privk);

    /* When contentProxy is created, arena will be NULLed and contentProxy
     * takes ownership of it. Otherwise, when arena still exists, we must
     * free it now. */
    PORT_FreeArena(arena, PR_TRUE /* zero */);
}

/**********************************************************************
 *
 * PK11Signature.engineUpdateNative
 *
 */
JNIEXPORT void JNICALL
Java_org_mozilla_jss_pkcs11_PK11Signature_engineUpdateNative
    (JNIEnv *env, jobject this, jbyteArray bArray, jint offset, jint length)
{
    SigContextType type;
    void *ctxt;
    jbyte *bytes=NULL;
    jint numBytes;

    /* Extract the signature context */
    if( getSigContext(env, this, &ctxt, &type) != PR_SUCCESS) {
        PR_ASSERT( (*env)->ExceptionOccurred(env) != NULL);
        goto finish;
    }
    PR_ASSERT(ctxt != NULL);

    /* Get the bytes to be updated */
    if (!JSS_RefByteArray(env, bArray, &bytes, &numBytes)) {
        ASSERT_OUTOFMEM(env);
        goto finish;
    }

    if( offset < 0 || offset >= numBytes || length < 0 ||
            (offset+length) > numBytes || (offset+length) < 0 )
    {
        JSS_throw(env, ARRAY_INDEX_OUT_OF_BOUNDS_EXCEPTION);
        goto finish;
    }

    /* Update the context */
    if(type == SGN_CONTEXT) {
        if( SGN_Update( (SGNContext*)ctxt,
                        (unsigned char*)bytes + offset,
                        (unsigned)length ) != SECSuccess)
        {
            JSS_throwMsg(env, SIGNATURE_EXCEPTION, "update failed");
            goto finish;
        }
    } else {
        PR_ASSERT( type == VFY_CONTEXT );
        if( VFY_Update( (VFYContext*)ctxt,
                        (unsigned char*)bytes + offset,
                        (unsigned) length ) != SECSuccess)
        {
            JSS_throwMsg(env, SIGNATURE_EXCEPTION, "update failed");
            goto finish;
        }
    }

finish:
    JSS_DerefByteArray(env, bArray, bytes, JNI_ABORT);
}


/**********************************************************************
 *
 * PK11Signature.engineSignNative
 *
 */
JNIEXPORT jbyteArray JNICALL
Java_org_mozilla_jss_pkcs11_PK11Signature_engineSignNative
    (JNIEnv *env, jobject this)
{
    SGNContext *ctxt;
    SigContextType type;
    SECItem signature;
    jbyteArray sigArray=NULL;

    PR_ASSERT(env!=NULL && this!=NULL);

    signature.data = NULL;

    /*
     * Extract the signature context from the Java wrapper
     */
    if( getSigContext(env, this, (void**)&ctxt, &type) != PR_SUCCESS) {
        PR_ASSERT( (*env)->ExceptionOccurred(env) != NULL);
        goto finish;
    }
    PR_ASSERT(ctxt!=NULL && type==SGN_CONTEXT);

    /*
     * Finish the signing operation.
     */
    if( SGN_End(ctxt, &signature) != SECSuccess) {
        JSS_throwMsgPrErr(env, SIGNATURE_EXCEPTION,
            "Signing operation failed");
        goto finish;
    }

    /*
     * Convert SECItem signature to Java byte array
     */
    sigArray = JSS_ToByteArray(env, signature.data, signature.len);
    if (sigArray == NULL) {
        ASSERT_OUTOFMEM(env);
        goto finish;
    }

finish:
    if( signature.data != NULL ) {
        PR_Free(signature.data);
    }
    return sigArray;
}

JNIEXPORT jboolean JNICALL
Java_org_mozilla_jss_pkcs11_PK11Signature_engineVerifyNative
	(JNIEnv *env, jobject this, jbyteArray sigArray)
{
	jboolean verified = JNI_FALSE;
	VFYContext *ctxt;
	SigContextType type;
	SECItem sigItem = {siBuffer, NULL, 0};

	PR_ASSERT( env!=NULL && this!=NULL && sigArray!=NULL);

	/*
	 * Lookup the context
	 */
	if( getSigContext(env, this, (void**)&ctxt, &type) != PR_SUCCESS) {
		PR_ASSERT(PR_FALSE);
		JSS_throwMsg(env, SIGNATURE_EXCEPTION,
			"Unable to retrieve verification context");
		goto finish;
	}
	if(type != VFY_CONTEXT) {
		PR_ASSERT(PR_FALSE);
		JSS_throwMsg(env, SIGNATURE_EXCEPTION,
			"Verification engine has signature context");
		goto finish;
	}

	/*
	 * Convert signature to SECItem
	 */
	if (!JSS_RefByteArray(env, sigArray, (jbyte **) &sigItem.data, (jsize *) &sigItem.len)) {
		ASSERT_OUTOFMEM(env);
		goto finish;
	}

	/*
	 * Finish the verification operation
	 */
	if( VFY_EndWithSignature(ctxt, &sigItem) == SECSuccess) {
		verified = JNI_TRUE;
	} else if( PR_GetError() != SEC_ERROR_BAD_SIGNATURE) {
		PR_ASSERT(PR_FALSE);
		JSS_throwMsg(env, SIGNATURE_EXCEPTION,
			"Failed to complete verification operation");
		goto finish;
	}

finish:
	JSS_DerefByteArray(env, sigArray, sigItem.data, JNI_ABORT);
	return verified;
}

/*
 * Extract the algorithm from a PK11Signature.
 *
 * sig: a PK11Signature.
 * Returns: the algorithm of this signature, or SEC_OID_UNKNOWN if an
 * 		error occurs.
 */
static SECOidTag
getAlgorithm(JNIEnv *env, jobject sig)
{
    jclass sigClass;
    jfieldID algField;
    jobject alg;

    PR_ASSERT(env != NULL && sig != NULL);

    sigClass = (*env)->GetObjectClass(env, sig);
    if (sigClass == NULL) {
        ASSERT_OUTOFMEM(env);
        return SEC_OID_UNKNOWN;
    }

    algField = (*env)->GetFieldID(env, sigClass, SIG_ALGORITHM_FIELD,
                                  SIG_ALGORITHM_SIG);
    if (algField == NULL) {
        ASSERT_OUTOFMEM(env);
        return SEC_OID_UNKNOWN;
    }

    alg = (*env)->GetObjectField(env, sig, algField);
    if (alg == NULL) {
        ASSERT_OUTOFMEM(env);
        return SEC_OID_UNKNOWN;
    }

    return JSS_getOidTagFromAlg(env, alg);
}

static SECOidTag
getDigestAlgorithm(JNIEnv *env, jobject sig)
{
    jclass sigClass;
    jfieldID algField;
    jobject alg;

    PR_ASSERT(env != NULL && sig != NULL);

    sigClass = (*env)->GetObjectClass(env, sig);
    if (sigClass == NULL) {
        ASSERT_OUTOFMEM(env);
        return SEC_OID_UNKNOWN;
    }

    algField = (*env)->GetFieldID(env, sigClass, SIG_DIGEST_ALGORITHM_FIELD,
                                  SIG_ALGORITHM_SIG);
    if (algField == NULL) {
        ASSERT_OUTOFMEM(env);
        return SEC_OID_UNKNOWN;
    }

    alg = (*env)->GetObjectField(env, sig, algField);
    if (alg == NULL) {
        /* Do not ASSERT_OUTOFMEM: it is possible for digestAlgorithm to be
         * NULL in sig and thus alg will be NULL here; no exception will be
         * raised. */
        return SEC_OID_UNKNOWN;
    }

    return JSS_getOidTagFromAlg(env, alg);
}

static SECStatus
getRSAPSSParamsAndSigningAlg(JNIEnv *env, jobject this, PRArenaPool *arena,
    SECAlgorithmID **alg, SECKEYPrivateKey *privk)
{
    SECItem *sigAlgParams = NULL;
    SECAlgorithmID *signAlg = NULL;
    SECOidTag digestAlg = SEC_OID_UNKNOWN;
    SECStatus rv = SECFailure;

    if (alg == NULL) {
        return rv;
    }

    signAlg = (SECAlgorithmID *)PORT_ArenaZAlloc(arena, sizeof(SECAlgorithmID));
    if (signAlg == NULL) {
        JSS_throw(env, OUT_OF_MEMORY_ERROR);
        return rv;
    }

    digestAlg = getDigestAlgorithm(env, this);

    sigAlgParams = SEC_CreateSignatureAlgorithmParameters(arena, NULL,
                       SEC_OID_PKCS1_RSA_PSS_SIGNATURE, digestAlg, NULL,
                       privk);
    if (sigAlgParams == NULL) {
        JSS_throwMsg(env, TOKEN_EXCEPTION,
                     "Unable to create signature algorithm parameters");
        return rv;
    }

    *alg = signAlg;
    rv = SECOID_SetAlgorithmID(arena, *alg, SEC_OID_PKCS1_RSA_PSS_SIGNATURE,
                               sigAlgParams);
    if (rv != SECSuccess) {
        JSS_throwMsg(env, TOKEN_EXCEPTION,
                     "Unable to set RSA-PSS Algorithm ID");
    }

    return rv;
}

/*
 * Set the contextProxy member of a PK11Signature.
 *
 * sig: the PK11Signature whose contextProxy we are setting.
 * context: the ContextProxy we are setting in the signature.  It may be NULL.
**/
static void
setSigContext(JNIEnv *env, jobject sig, jobject context)
{
    jclass sigClass;
    jfieldID contextField;

    PR_ASSERT(env!=NULL && sig!=NULL);

    sigClass = (*env)->GetObjectClass(env, sig);
    PR_ASSERT(sigClass!=NULL);

    contextField = (*env)->GetFieldID(
                                      env,
                                      sigClass,
                                      SIG_CONTEXT_PROXY_FIELD,
                                      SIG_CONTEXT_PROXY_SIG);
    if(contextField == NULL) {
        ASSERT_OUTOFMEM(env);
        /* This function doesn't advertise that it can throw exceptions,
         * so we shouldn't throw one */
        (*env)->ExceptionClear(env);
        return;
    }

    (*env)->SetObjectField(env, sig, contextField, context);
}

/*
 * Don't call this if there is no context.
 */
static PRStatus
getSigContext(JNIEnv *env, jobject sig, void**pContext, SigContextType* pType)
{
    jfieldID contextField;
    jclass sigClass;
    jobject proxy;

    PR_ASSERT(env!=NULL && sig!=NULL && pContext!=NULL && pType!=NULL);

    sigClass = (*env)->GetObjectClass(env, sig);
#ifdef DEBUG
    {
        jclass realSigClass = (*env)->FindClass(env, PK11SIGNATURE_CLASS_NAME);
        PR_ASSERT( (*env)->IsInstanceOf(env, sig, realSigClass) );
    }
#endif

    contextField = (*env)->GetFieldID(env, sigClass, SIG_CONTEXT_PROXY_FIELD,
                        SIG_CONTEXT_PROXY_SIG);
    if(contextField == NULL) {
        ASSERT_OUTOFMEM(env);
        return PR_FAILURE;
    }

    proxy = (*env)->GetObjectField(env, sig, contextField);
    if(proxy == NULL) {
        PR_ASSERT(PR_FALSE);
        JSS_throw(env, TOKEN_EXCEPTION);
        return PR_FAILURE;
    }

    if( JSS_PK11_getSigContext(env, proxy, pContext, pType) != PR_SUCCESS ) {
        PR_ASSERT( (*env)->ExceptionOccurred(env) != NULL);
        return PR_FAILURE;
    }
    PR_ASSERT(*pContext != NULL);

    return PR_SUCCESS;
}

#define PUBLICKEYTYPE 0
#define PRIVATEKEYTYPE 1
/**********************************************************************
 *
 * g e t P r i v a t e K e y
 */
static PRStatus
getPrivateKey(JNIEnv *env, jobject sig, SECKEYPrivateKey**key)
{
	return getSomeKey(env, sig, (void**)key, PRIVATEKEYTYPE);
}

/**********************************************************************
 *
 * g e t P u b l i c K e y
 */
static PRStatus
getPublicKey(JNIEnv *env, jobject sig, SECKEYPublicKey**key)
{
	return getSomeKey(env, sig, (void**)key, PUBLICKEYTYPE);
}

static PRStatus
getSomeKey(JNIEnv *env, jobject sig, void **key, short type)
{
    jfieldID keyField;
    jclass sigClass;
    jobject keyProxy;

    PR_ASSERT(env!=NULL && sig!=NULL && key!=NULL);

    sigClass = (*env)->GetObjectClass(env, sig);
#ifdef DEBUG
    {
    jclass realSigClass = (*env)->FindClass(env, PK11SIGNATURE_CLASS_NAME);
    PR_ASSERT( (*env)->IsInstanceOf(env, sig, realSigClass) );
    }
#endif

    keyField = (*env)->GetFieldID(env, sigClass, SIG_KEY_FIELD, SIG_KEY_SIG);
    if(keyField == NULL) {
        ASSERT_OUTOFMEM(env);
        return PR_FAILURE;
    }

    keyProxy = (*env)->GetObjectField(env, sig, keyField);
    if(keyProxy == NULL) {
        PR_ASSERT(PR_FALSE);
        JSS_throw(env, TOKEN_EXCEPTION);
        return PR_FAILURE;
    }

	if(type == PRIVATEKEYTYPE) {
	    if( JSS_PK11_getPrivKeyPtr(env, keyProxy, (SECKEYPrivateKey**)key)
															 != PR_SUCCESS)
		{
    	    PR_ASSERT( (*env)->ExceptionOccurred(env) != NULL);
        	return PR_FAILURE;
    	}
	} else {
	    if( JSS_PK11_getPubKeyPtr(env, keyProxy, (SECKEYPublicKey**)key)
															!= PR_SUCCESS)
		{
    	    PR_ASSERT( (*env)->ExceptionOccurred(env) != NULL);
        	return PR_FAILURE;
    	}
	}
    PR_ASSERT(*key != NULL);

    return PR_SUCCESS;
}

struct SigContextProxyStr {
    void *ctxt;
    SigContextType type;
    PRArenaPool *arena;
};

/***********************************************************************
 * J S S _ P K 1 1 _ g e t S i g C o n t e x t
 *
 * Extracts the context pointer from a SigContextProxy.
 * proxy: a non-NULL SigContextProxy object.
 * pContext: address of a SGNContext* or VFYContext* where the pointer will be
 *      stored.
 * pType: address of a SigContextType where will be stored the type
 *      of the context either SGN_CONTEXT or VFY_CONTEXT.
 * Returns: PR_SUCCESS, unless an exception was thrown.
 */
PRStatus
JSS_PK11_getSigContext(JNIEnv *env, jobject proxy, void**pContext,
        SigContextType *pType)
{
    SigContextProxy *ctxtProxy;

    PR_ASSERT(env!=NULL && proxy!=NULL && pContext!=NULL && pType!=NULL);

    if( JSS_getPtrFromProxy(env, proxy, (void**)&ctxtProxy) != PR_SUCCESS)
    {
        ASSERT_OUTOFMEM(env);
        return PR_FAILURE;
    }

    /* Make sure the pointers are OK */
    if(ctxtProxy==NULL || ctxtProxy->ctxt==NULL) {
        PR_ASSERT(PR_FALSE);
        JSS_throw(env, SIGNATURE_EXCEPTION);
        return PR_FAILURE;
    }

    /* Everything looks good, return the pointer */
    *pContext = ctxtProxy->ctxt;
    *pType = ctxtProxy->type;
    return PR_SUCCESS;
}

/**********************************************************************
 *
 * J S S _ P K 1 1 _ w r a p S i g C o n t e x t P r o x y
 *
 * Wraps a SGNContext in a SigContextProxy.
 *
 * ctxt: address of ptr to a SGNContext or VfyContext, which must not be NULL.
 *  It will be eaten by the wrapper and set to NULL.
 *		
 * type: type of context, either SGN_CONTEXT or VFY_CONTEXT
 * Returns: a new SigContextProxy object wrapping the given SGNContext, or
 *  NULL if an exception was thrown.
 */
jobject
JSS_PK11_wrapSigContextProxy(JNIEnv *env, void **ctxt, SigContextType type, PRArenaPool **arena)
{
    jclass proxyClass;
    jmethodID constructor;
    jbyteArray byteArray;
    SigContextProxy *proxy = NULL;
    jobject Context = NULL;

    /* arena and *arena can safely be NULL */
    PR_ASSERT(env != NULL && ctxt != NULL && *ctxt != NULL);

    /* Create the proxy structure */
    proxy = (SigContextProxy*) PR_Malloc(sizeof(SigContextProxy));
    if (proxy == NULL) {
        JSS_throw(env, OUT_OF_MEMORY_ERROR);
        goto finish;
    }
    proxy->ctxt = *ctxt;
    proxy->type = type;
    proxy->arena = NULL;
    if (arena != NULL) {
        proxy->arena = *arena;
    }

    byteArray = JSS_ptrToByteArray(env, (void*)proxy);

    /*
     * Lookup the class and constructor
     */
    proxyClass = (*env)->FindClass(env, SIG_CONTEXT_PROXY_CLASS_NAME);
    if (proxyClass == NULL) {
        ASSERT_OUTOFMEM(env);
        goto finish;
    }

    constructor = (*env)->GetMethodID(env, proxyClass,
                                      SIG_CONTEXT_PROXY_CONSTRUCTOR_NAME,
                                      SIG_CONTEXT_PROXY_CONSTRUCTOR_SIG);
    if (constructor == NULL) {
        ASSERT_OUTOFMEM(env);
        goto finish;
    }

    /* call the constructor */
    Context = (*env)->NewObject(env, proxyClass, constructor, byteArray);

finish:
    if (Context == NULL) {
        /* didn't work, so free resources */
        if (proxy != NULL) {
            PR_Free(proxy);
        }

        if (type == SGN_CONTEXT) {
            SGN_DestroyContext((SGNContext*)*ctxt, PR_TRUE /*freeit*/);
        } else {
            PR_ASSERT(type == VFY_CONTEXT);
            VFY_DestroyContext((VFYContext*)*ctxt, PR_TRUE /*freeit*/);
        }

        if (arena != NULL) {
            PORT_FreeArena(*arena, PR_TRUE /* zero */);
        }
    }

    *ctxt = NULL;
    if (arena != NULL) {
        /* We either take ownership of Arena (and place in Context), or we
         * free it when construction fails. */
        *arena = NULL;
    }
    return Context;
}

/***********************************************************************
 *
 * SigContextProxy.releaseNativeResources
 *
 * Deletes the SGNContext wrapped by this SigContextProxy object.
 */
JNIEXPORT void JNICALL
Java_org_mozilla_jss_pkcs11_SigContextProxy_releaseNativeResources
  (JNIEnv *env, jobject this)
{
    SigContextProxy *proxy = NULL;

    /* Retrieve the proxy pointer */
    if (JSS_getPtrFromProxy(env, this, (void**)&proxy) != PR_SUCCESS) {
        return;
    }

    if (proxy == NULL) {
        return;
    }

    /* Free the context and the proxy */
    if(proxy->type == SGN_CONTEXT) {
        SGN_DestroyContext( (SGNContext*)proxy->ctxt, PR_TRUE /*freeit*/);
    } else {
        PR_ASSERT(proxy->type == VFY_CONTEXT);
        VFY_DestroyContext( (VFYContext*)proxy->ctxt, PR_TRUE /*freeit*/);
    }
    PORT_FreeArena(proxy->arena, PR_TRUE /* zero */);
    proxy->arena = NULL;

    PR_Free(proxy);
}

/***********************************************************************
 * PK11Signature.engineRawSignNative
 */
JNIEXPORT jbyteArray JNICALL
Java_org_mozilla_jss_pkcs11_PK11Signature_engineRawSignNative
    (JNIEnv *env, jclass clazz, jobject tokenObj, jobject keyObj,
    jbyteArray hashBA)
{
    SECKEYPrivateKey *key = NULL;
    SECItem *sig = NULL;
    SECItem *hash = NULL;
    jbyteArray sigBA = NULL;

    PR_ASSERT(env!=NULL && tokenObj!=NULL && keyObj!=NULL && hashBA!=NULL);

    /* Get the private key */
    if( JSS_PK11_getPrivKeyPtr(env, keyObj, &key) != PR_SUCCESS ) {
        /* exception was thrown */
        goto finish;
    }

    /* get the hash */
    hash = JSS_ByteArrayToSECItem(env, hashBA);

    /* prepare space for the sig */
    sig = PR_NEW(SECItem);
    sig->len = PK11_SignatureLen(key);
    sig->data = PR_Malloc(sig->len);

    /* perform the signature operation */
    if( PK11_Sign(key, sig, hash) != SECSuccess ) {
        JSS_throwMsg(env, SIGNATURE_EXCEPTION, "Signature operation failed"
            " on token");
        goto finish;
    }

    /* convert signature to byte array */
    sigBA = JSS_SECItemToByteArray(env, sig);

finish:
    if(sig) {
        SECITEM_FreeItem(sig, PR_TRUE /*freeit*/);
    }
    if(hash) {
        SECITEM_FreeItem(hash, PR_TRUE /*freeit*/);
    }
    return sigBA;
}

/***********************************************************************
 * PK11Signature.engineRawVerifyNative
 */
JNIEXPORT jboolean JNICALL
Java_org_mozilla_jss_pkcs11_PK11Signature_engineRawVerifyNative
    (JNIEnv *env, jclass clazz, jobject tokenObj, jobject keyObj,
    jbyteArray hashBA, jbyteArray sigBA)
{
    SECItem *sig=NULL;
    SECItem *hash=NULL;
    SECKEYPublicKey *key=NULL;
    jboolean verified=JNI_FALSE;
    SECStatus status;

    PR_ASSERT(env!=NULL && tokenObj!=NULL && keyObj!=NULL && hashBA!=NULL
        && sigBA!=NULL);

    sig = JSS_ByteArrayToSECItem(env, sigBA);
    if(sig==NULL) {
        goto finish;
    }
    hash = JSS_ByteArrayToSECItem(env, hashBA);
    if(hash==NULL) {
        goto finish;
    }

    if( JSS_PK11_getPubKeyPtr(env, keyObj, &key) != PR_SUCCESS ) {
        goto finish;
    }

    /* perform the operation */
    status = PK11_Verify(key, sig, hash, NULL /*wincx*/);
    if( status == SECSuccess ) {
        verified = JNI_TRUE;
    } else if( PR_GetError() != SEC_ERROR_BAD_SIGNATURE ) {
        JSS_throwMsg(env, SIGNATURE_EXCEPTION, "Verification operation"
            " failed on token");
        goto finish;
    }

finish:
    if(sig) {
        SECITEM_FreeItem(sig, PR_TRUE /*freeit*/);
    }
    if(hash) {
        SECITEM_FreeItem(hash, PR_TRUE /*freeit*/);
    }
    return verified;
}
