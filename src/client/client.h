/* Copyright (c) 2009 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef RAMCLOUD_CLIENT_CLIENT_H
#define RAMCLOUD_CLIENT_CLIENT_H

#include <inttypes.h>
#include <stdbool.h>
#include <shared/net.h>

struct rc_client {
    struct rc_net net;
};

struct rc_index_entry {
    // keep this identical to shared/object.h's chunk_entry for now 
    uint64_t len;
    uint32_t index_id;
    uint32_t index_type;
    char data[0];                       // Variable length, but contiguous
};

#ifdef __cplusplus
extern "C" {
#endif
int rc_connect(struct rc_client *client);
void rc_disconnect(struct rc_client *client);
int rc_ping(struct rc_client *client);
int rc_write(struct rc_client *client, uint64_t table, uint64_t key,
             const char *buf, uint64_t len,
             const char *index_entries_buf, uint64_t index_entries_len);
int rc_insert(struct rc_client *client, uint64_t table, const char *buf,
              uint64_t len, uint64_t *key,
              const char *index_entries_buf, uint64_t index_entries_len);
int rc_read(struct rc_client *client, uint64_t table,
            uint64_t key, char *buf, uint64_t *len,
            char *index_entries_buf, uint64_t *index_entries_len);
int rc_create_table(struct rc_client *client, const char *name);
int rc_open_table(struct rc_client *client, const char *name,
                  uint64_t *table_id);
int rc_drop_table(struct rc_client *client, const char *name);
int rc_create_index(struct rc_client *client, uint64_t table_id,
                    enum RCRPC_INDEX_TYPE type, bool unique,
                    bool range_queryable, uint16_t *index_id);
int rc_drop_index(struct rc_client *client, uint64_t table_id,
                  uint16_t index_id);

/* These aren't strictly necessary, but they make life easier for
 * foreign languages because they don't have to know how to allocate a
 * structure of the correct size */
struct rc_client *rc_new();
void rc_free(struct rc_client *client);
const char* rc_last_error();
#ifdef __cplusplus
}
#endif

#endif
