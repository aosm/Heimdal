/*
 * Copyright (c) 2009 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 - 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "hi_locl.h"
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

struct heim_icred {
    uid_t uid;
    gid_t gid;
    pid_t pid;
    pid_t session;
    struct sockaddr *sa;
    struct sockaddr_storage __ss;
    krb5_socklen_t sa_size;
};

void
heim_ipc_free_cred(heim_icred cred)
{
    free(cred);
}

uid_t
heim_ipc_cred_get_uid(heim_icred cred)
{
    return cred->uid;
}

gid_t
heim_ipc_cred_get_gid(heim_icred cred)
{
    return cred->gid;
}

pid_t
heim_ipc_cred_get_pid(heim_icred cred)
{
    return cred->pid;
}

pid_t
heim_ipc_cred_get_session(heim_icred cred)
{
    return cred->session;
}

struct sockaddr *
heim_ipc_cred_get_address(heim_icred cred, krb5_socklen_t *sa_size)
{
    *sa_size = cred->sa_size;
    return cred->sa;
}

int
_heim_ipc_create_cred(uid_t uid, gid_t gid, pid_t pid, pid_t session, heim_icred *cred)
{
    *cred = calloc(1, sizeof(**cred));
    if (*cred == NULL)
	return ENOMEM;
    (*cred)->uid = uid;
    (*cred)->gid = gid;
    (*cred)->pid = pid;
    (*cred)->session = session;
    return 0;
}

int
_heim_ipc_create_network_cred(struct sockaddr *sa, krb5_socklen_t sa_size, heim_icred *cred)
{
    *cred = calloc(1, sizeof(**cred));
    if (*cred == NULL)
	return ENOMEM;
    (*cred)->uid = (uid_t)-1;
    (*cred)->gid = (uid_t)-1;
    (*cred)->pid = (uid_t)-1;
    (*cred)->session = (uid_t)-1;

    if (sa_size > sizeof((*cred)->__ss))
	sa_size = sizeof((*cred)->__ss);
    memcpy(&(*cred)->__ss, sa, sa_size);
    (*cred)->sa_size = sa_size;
    (*cred)->sa = (struct sockaddr *)&(*cred)->__ss;
    return 0;
}

heim_isemaphore
heim_ipc_semaphore_create(long value)
{
#ifdef __APPLE__
    return (heim_isemaphore)dispatch_semaphore_create(value);
#else
    abort();
#endif
}

long
heim_ipc_semaphore_wait(heim_isemaphore s, time_t t)
{
#ifdef __APPLE__
    uint64_t timeout;
    if (t == HEIM_IPC_WAIT_FOREVER)
	timeout = DISPATCH_TIME_FOREVER;
    else
	timeout = (uint64_t)t * NSEC_PER_SEC;

    return dispatch_semaphore_wait((dispatch_semaphore_t)s, timeout);
#else
    abort();
#endif
}

long
heim_ipc_semaphore_signal(heim_isemaphore s)
{
#ifdef __APPLE__
    return dispatch_semaphore_signal((dispatch_semaphore_t)s);
#else
    abort();
#endif
}

void
heim_ipc_semaphore_release(heim_isemaphore s)
{
#ifdef __APPLE__
    return dispatch_release((dispatch_semaphore_t)s);
#else
    abort();
#endif
}

void
heim_ipc_free_data(heim_idata *data)
{
    if (data->data)
	free(data->data);
    data->data = NULL;
    data->length = 0;
}
