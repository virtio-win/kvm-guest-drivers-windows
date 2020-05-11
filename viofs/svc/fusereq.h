/*
 * Copyright (C) 2019-2020 Red Hat, Inc.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma once

#include "fuse.h"

typedef struct
{
    struct fuse_in_header   hdr;
    struct fuse_init_in     init;

} FUSE_INIT_IN;

typedef struct
{
    struct fuse_out_header  hdr;
    struct fuse_init_out    init;

} FUSE_INIT_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    char                    name[MAX_PATH];

} FUSE_LOOKUP_IN;

typedef struct
{
    struct fuse_out_header  hdr;
    struct fuse_entry_out   entry;

} FUSE_LOOKUP_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    struct fuse_getattr_in  getattr;

} FUSE_GETATTR_IN, *PFUSE_GETATTR_IN;

typedef struct
{
    struct fuse_out_header   hdr;
    struct fuse_attr_out     attr;

} FUSE_GETATTR_OUT, *PFUSE_GETATTR_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    struct fuse_setattr_in  setattr;

} FUSE_SETATTR_IN, *PFUSE_SETATTR_IN;

typedef struct
{
    struct fuse_out_header   hdr;
    struct fuse_attr_out     attr;

} FUSE_SETATTR_OUT, *PFUSE_SETATTR_OUT;

typedef struct
{
    struct fuse_in_header   hdr;

} FUSE_STATFS_IN, *PFUSE_STATFS_IN;

typedef struct
{
    struct fuse_out_header   hdr;
    struct fuse_statfs_out   statfs;

} FUSE_STATFS_OUT, *PFUSE_STATFS_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    struct fuse_create_in   create;
    char                    name[MAX_PATH];

} FUSE_CREATE_IN, *PFUSE_CREATE_IN;

typedef struct
{
    struct fuse_out_header  hdr;
    struct fuse_entry_out   entry;
    struct fuse_open_out    open;

} FUSE_CREATE_OUT, *PFUSE_CREATE_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    struct fuse_open_in     open;

} FUSE_OPEN_IN, *PFUSE_OPEN_IN;

typedef struct
{
    struct fuse_out_header   hdr;
    struct fuse_open_out     open;

} FUSE_OPEN_OUT, *PFUSE_OPEN_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    struct fuse_read_in     read;

} FUSE_READ_IN, *PFUSE_READ_IN;

typedef struct
{
    struct fuse_out_header  hdr;
    char                    buf[];

} FUSE_READ_OUT, *PFUSE_READ_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    struct fuse_release_in  release;

} FUSE_RELEASE_IN, *PFUSE_RELEASE_IN;

typedef struct
{
    struct fuse_out_header  hdr;

} FUSE_RELEASE_OUT, *PFUSE_RELEASE_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    struct fuse_write_in    write;
    char                    buf[];

} FUSE_WRITE_IN, *PFUSE_WRITE_IN;

typedef struct
{
    struct fuse_out_header  hdr;
    struct fuse_write_out   write;
    char                    buf[];

} FUSE_WRITE_OUT, *PFUSE_WRITE_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    struct fuse_flush_in    flush;

} FUSE_FLUSH_IN;

typedef struct
{
    struct fuse_out_header  hdr;

} FUSE_FLUSH_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    char                    name[MAX_PATH];

} FUSE_UNLINK_IN;

typedef struct
{
    struct fuse_out_header  hdr;

} FUSE_UNLINK_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    struct fuse_rename2_in  rename;
    char                    names[];

} FUSE_RENAME2_IN;

typedef struct
{
    struct fuse_out_header  hdr;

} FUSE_RENAME_OUT;

typedef struct
{
    struct fuse_in_header   hdr;
    struct fuse_mkdir_in    mkdir;
    char                    name[MAX_PATH];

} FUSE_MKDIR_IN;

typedef struct
{
    struct fuse_out_header  hdr;
    struct fuse_entry_out   entry;

} FUSE_MKDIR_OUT;

typedef struct
{
    struct fuse_in_header       hdr;
    struct fuse_fallocate_in    falloc;

} FUSE_FALLOCATE_IN;

typedef struct
{
    struct fuse_out_header  hdr;

} FUSE_FALLOCATE_OUT;
