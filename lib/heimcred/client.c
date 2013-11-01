/*-
 * Copyright (c) 2013 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2013 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#import <CoreFoundation/CFRuntime.h>
#import <CoreFoundation/CFXPCBridge.h>
#import <xpc/xpc.h>

#import "heimcred.h"
#import "common.h"
#import "heimbase.h"

/*
 *
 */

static void HeimItemNotify(xpc_object_t object);
static void HeimWakeupVersion(void);

/*
 *
 */

#define HC_INIT() do { _HeimCredInit(); } while(0)

#define HC_INIT_ERROR(_error)			\
	do {					\
		if (_error) {			\
		    *(_error) = NULL;		\
		}				\
		HC_INIT();			\
	} while(0)

/*
 *
 */

static void
_HeimCredInit(void)
{
    static dispatch_once_t once;

    dispatch_once(&once, ^{
	    _HeimCredInitCommon();

	    HeimCredCTX.conn = xpc_connection_create("com.apple.GSSCred", HeimCredCTX.queue);

	    xpc_connection_set_event_handler(HeimCredCTX.conn, ^(xpc_object_t object){ HeimItemNotify(object); });
	    xpc_connection_resume(HeimCredCTX.conn);

	    HeimWakeupVersion();
	});
}

/*
 * Let the server know we are awake
 */
static void
HeimWakeupVersion(void)
{
    if (HeimCredCTX.conn == NULL) abort();
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "command", "wakeup");
    xpc_dictionary_set_int64(request, "version", 0);
    xpc_connection_send_message(HeimCredCTX.conn, request);
    xpc_release(request);
}

#if 0
static NSDictionary *
heim_send_message_with_reply_sync(NSDictionary *message)
{
    NSDictionary *reply = nil;
    NSMutableData *request;
    size_t length;
    void *ptr;
    xpc_object_t xrequest;

    request = [NSMutableData data];

    [HeimCredDecoder archiveRootObject:message toData:request];

    xrequest = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_data(xrequest, "data", [request mutableBytes], [request length]);

    xpc_object_t xreply = xpc_connection_send_message_with_reply_sync(HeimCredCTX.conn, xrequest);

    ptr = xpc_dictionary_get_value(xreply, "data", &length);

    NSData *data = [NSData dataWithBytes:ptr length:length];

    reply = [HeimCredDecoder copyUnarchiveObjectWithData:data];

    return reply;
}
#endif

/*
 *
 */

static void
HeimItemNotify(xpc_object_t object)
{
    CFUUIDRef uuid;
    
    if (object == XPC_TYPE_ERROR)
	return;
    
    uuid = HeimCredCopyUUID(object, "uuid");
    if (uuid)
	CFDictionaryRemoveValue(HeimCredCTX.items, uuid);
    CFRELEASE_NULL(uuid);
}

/*
 *
 */

/*
 *
 */

static HeimCredRef
HeimCredAddItem(xpc_object_t object)
{
    CFDictionaryRef attributes = HeimCredMessageCopyAttributes(object, "attributes");
    if (attributes == NULL)
	return NULL;
    
    CFUUIDRef uuid = CFDictionaryGetValue(attributes, kHEIMAttrUUID);
    if (uuid == NULL) {
	if (attributes)
	    CFRelease(attributes);
	return NULL;
    }

    HeimCredRef cred = HeimCredCreateItem(uuid);
    if (cred == NULL) {
	if (attributes)
	    CFRelease(attributes);
	return NULL;
    }
    
    cred->attributes = attributes;
    
    dispatch_sync(HeimCredCTX.queue, ^{
	CFDictionarySetValue(HeimCredCTX.items, cred->uuid, cred);
    });
    return cred;
}

/*
 *
 */

HeimCredRef
HeimCredCreate(CFDictionaryRef attributes, CFErrorRef *error)
{
    __block HeimCredRef cred;

    HC_INIT_ERROR(error);

    xpc_object_t xpcattrs = _CFXPCCreateXPCObjectFromCFObject(attributes);
    if (xpcattrs == NULL)
	return NULL;

    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    heim_assert(request != NULL, "xpc_dictionary_create");

    xpc_dictionary_set_string(request, "command", "create");
    xpc_dictionary_set_value(request, "attributes", xpcattrs);
    xpc_release(xpcattrs);

    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(HeimCredCTX.conn, request);
    xpc_release(request);
    if (reply == NULL)
	return NULL;
    
    heim_assert(reply != XPC_ERROR_CONNECTION_INTERRUPTED, "got XPC_ERROR_CONNECTION_INTERRUPTED");
    heim_assert(reply != XPC_ERROR_CONNECTION_INVALID, "got XPC_ERROR_CONNECTION_INVALID");

    cred = HeimCredAddItem(reply);
    xpc_release(reply);

    return cred;
}

/*
 *
 */
 
CFUUIDRef
HeimCredGetUUID(HeimCredRef cred)
{
    return cred->uuid;
}


/*
 *
 */

HeimCredRef
HeimCredCopyFromUUID(CFUUIDRef uuid)
{
    __block HeimCredRef cred;

    HC_INIT();
    
    dispatch_sync(HeimCredCTX.queue, ^{
	cred = (HeimCredRef)CFDictionaryGetValue(HeimCredCTX.items, uuid);
	if (cred == NULL) {
	    cred = HeimCredCreateItem(uuid);
	    CFDictionarySetValue(HeimCredCTX.items, uuid, cred);
	} else {
	    CFRetain(cred);
	}
    });
    return cred;
}

/*
 *
 */

bool
HeimCredSetAttribute(HeimCredRef cred, CFTypeRef key, CFTypeID value, CFErrorRef *error)
{
    const void *keys[1] = { (void *)key };
    const void *values[1] = { (void *)value };
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (attrs == NULL)
	return false;
    bool ret = HeimCredSetAttributes(cred, attrs, error);
    CFRelease(attrs);
    return ret;

}

/*
 *
 */

bool
HeimCredSetAttributes(HeimCredRef cred, CFDictionaryRef attributes, CFErrorRef *error)
{
    HC_INIT_ERROR(error);
    
    xpc_object_t xpcquery = _CFXPCCreateXPCObjectFromCFObject(attributes);
    if (xpcquery == NULL)
	return false;
    
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "command", "setattributes");
    HeimCredSetUUID(request, "uuid", cred->uuid);
    xpc_dictionary_set_value(request, "attributes", xpcquery);
    xpc_release(xpcquery);
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(HeimCredCTX.conn, request);
    xpc_release(request);
    if (reply == NULL)
	return false;
    
    dispatch_sync(HeimCredCTX.queue, ^{
	CFRELEASE_NULL(cred->attributes);
	cred->attributes = HeimCredMessageCopyAttributes(reply, "attributes");
    });
    xpc_release(reply);

    return true;
}

CFTypeRef
HeimCredCopyAttribute(HeimCredRef cred, CFTypeRef attribute)
{
    CFDictionaryRef attrs = HeimCredCopyAttributes(cred, NULL, NULL);
    if (attrs == NULL)
	return NULL;
	
    CFTypeRef ref = CFDictionaryGetValue(attrs, attribute);
    if (ref)
	CFRetain(ref);
    CFRelease(attrs);
    return ref;
}

/*
 *
 */

CFDictionaryRef
HeimCredCopyAttributes(HeimCredRef cred, CFSetRef attributes, CFErrorRef *error)
{
    __block CFDictionaryRef attrs;

    HC_INIT_ERROR(error);

    dispatch_sync(HeimCredCTX.queue, ^{
	    if (cred->attributes)
		CFRetain(cred->attributes);
	    attrs = cred->attributes;
	});

    if (attrs == NULL) {
	xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(request, "command", "fetch");
	HeimCredSetUUID(request, "uuid", cred->uuid);
	xpc_object_t reply = xpc_connection_send_message_with_reply_sync(HeimCredCTX.conn, request);
	xpc_release(request);
	if (reply == NULL)
	    return NULL;

	dispatch_sync(HeimCredCTX.queue, ^{
		CFRELEASE_NULL(cred->attributes);
		attrs = cred->attributes = HeimCredMessageCopyAttributes(reply, "attributes");
		if (attrs)
		    CFRetain(attrs);
	    });
	xpc_release(reply);
    }
    return attrs;
}

/*
 *
 */

static xpc_object_t
SendQueryCommand(const char *command, CFDictionaryRef query)
{
    xpc_object_t xpcquery = _CFXPCCreateXPCObjectFromCFObject(query);
    if (xpcquery == NULL)
	return NULL;
    
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "command", command);
    xpc_dictionary_set_value(request, "query", xpcquery);
    xpc_release(xpcquery);
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(HeimCredCTX.conn, request);
    xpc_release(request);
    return reply;
}

/*
 *
 */

CFArrayRef
HeimCredCopyQuery(CFDictionaryRef query)
{
    HC_INIT();
    
    xpc_object_t reply = SendQueryCommand("query", query);
    if (reply == NULL)
	return NULL;
    
    CFMutableArrayRef result = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    if (result == NULL) {
	xpc_release(reply);
	return NULL;
    }
    
    xpc_object_t objects = xpc_dictionary_get_value(reply, "items");
    if (objects && xpc_get_type(objects) == XPC_TYPE_ARRAY) {
	xpc_array_apply(objects, ^bool(size_t index, xpc_object_t value) {
	    CFUUIDRef uuid =_CFXPCCreateCFObjectFromXPCObject(value);
	    if (uuid == NULL)
		return (bool)true;
	    HeimCredRef cred = HeimCredCreateItem(uuid);
	    CFRelease(uuid);
	    if (cred) {
		CFArrayAppendValue(result, cred);
		CFRelease(cred);
	    }
	    return (bool)true;
	});
    }
    xpc_release(reply);

    return result;
}

/*
 *
 */

bool
HeimCredDeleteQuery(CFDictionaryRef query, CFErrorRef *error)
{
    HC_INIT_ERROR(error);
    
    xpc_object_t reply = SendQueryCommand("delete", query);
    if (reply == NULL)
	return NULL;

    xpc_release(reply);

    return false;
}

static xpc_object_t
SendItemCommand(const char *command, CFUUIDRef uuid)
{
    const void *keys[1] = { (void *)kHEIMAttrUUID };
    const void *values[1] = { (void *)uuid };
    
    CFDictionaryRef query = CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    xpc_object_t reply = SendQueryCommand(command, query);
    CFRelease(query);
    return reply;
}


void
HeimCredDeleteByUUID(CFUUIDRef uuid)
{
    HC_INIT();
    xpc_object_t reply = SendItemCommand("delete", uuid);
    if (reply)
	xpc_release(reply);
    
}
/*
 *
 */

void
HeimCredDelete(HeimCredRef cred)
{
    HeimCredDeleteByUUID(cred->uuid);
}

/*
 *
 */

void
HeimCredRetainTransient(HeimCredRef cred)
{
    HC_INIT();
    xpc_object_t reply = SendItemCommand("retain-transient", cred->uuid);
    if (reply)
	xpc_release(reply);
}

/*
 *
 */

void
HeimCredReleaseTransient(HeimCredRef cred)
{
    HC_INIT();
    xpc_object_t reply = SendItemCommand("release-transient", cred->uuid);
    if (reply)
	xpc_release(reply);
}

bool
HeimCredMove(CFUUIDRef from, CFUUIDRef to)
{
    HC_INIT();
    
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "command", "move");
    HeimCredSetUUID(request, "from", from);
    HeimCredSetUUID(request, "to", to);
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(HeimCredCTX.conn, request);
    xpc_release(request);
    xpc_release(reply);
    return true;
}


/*
 *
 */

CFDictionaryRef
HeimCredDoAuth(HeimCredRef cred, CFDictionaryRef input)
{
    HC_INIT();
    return NULL;
}

/*
typedef struct HeimAuth_s *HeimAuthRef;

HeimAuthRef
HeimCreateAuthetication(CFDictionaryRef input);

bool
HeimAuthStep(HeimAuthRef cred, CFTypeRef input, CFTypeRef *output, CFErrorRef *error);
*/
