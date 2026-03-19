#ifndef CM_BACKEND_H
#define CM_BACKEND_H

#include "cm/core.h"
#include "cm/memory.h"
#include "cm/map.h"
#include "cm/string.h"
#include "cm/thread.h"
#include "cm/http.h"

/* ============================================================================
 * CM Backend Performance Optimizations
 * High-performance server-side optimizations for CM language
 * ========================================================================== */

#define CM_BACKEND_VERSION "1.0.0"

/* ============================================================================
 * Connection Pooling
 * ========================================================================== */

typedef struct {
    void** connections;
    size_t size;
    size_t capacity;
    size_t max_size;
    CMMutex lock;
    int (*create_conn)(void** conn);
    void (*destroy_conn)(void* conn);
    int (*validate_conn)(void* conn);
} cm_connection_pool_t;

cm_connection_pool_t* cm_pool_create(size_t initial, size_t max,
                                      int (*create)(void**),
                                      void (*destroy)(void*),
                                      int (*validate)(void*));
void cm_pool_destroy(cm_connection_pool_t* pool);
void* cm_pool_acquire(cm_connection_pool_t* pool);
int cm_pool_release(cm_connection_pool_t* pool, void* conn);

/* ============================================================================
 * Zero-Copy Buffer Management
 * ========================================================================== */

typedef struct {
    char* data;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    int ref_count;
} cm_zerocopy_buffer_t;

cm_zerocopy_buffer_t* cm_buffer_create(size_t capacity);
cm_zerocopy_buffer_t* cm_buffer_ref(cm_zerocopy_buffer_t* buf);
void cm_buffer_unref(cm_zerocopy_buffer_t* buf);
size_t cm_buffer_write(cm_zerocopy_buffer_t* buf, const void* data, size_t len);
size_t cm_buffer_read(cm_zerocopy_buffer_t* buf, void* dest, size_t len);
void cm_buffer_reset(cm_zerocopy_buffer_t* buf);

/* ============================================================================
 * String Interning (for reduced memory)
 * ========================================================================== */

typedef struct {
    cm_map_t* table;  /* hash -> string */
    CMMutex lock;
} cm_string_pool_t;

cm_string_pool_t* cm_string_pool_create(void);
void cm_string_pool_destroy(cm_string_pool_t* pool);
const char* cm_string_pool_intern(cm_string_pool_t* pool, const char* str);
void cm_string_pool_collect_garbage(cm_string_pool_t* pool);

/* ============================================================================
 * Async I/O (io_uring style on Linux, IOCP on Windows)
 * ========================================================================== */

typedef enum {
    CM_AIO_READ,
    CM_AIO_WRITE,
    CM_AIO_ACCEPT,
    CM_AIO_CONNECT,
    CM_AIO_CLOSE
} cm_aio_op_t;

typedef struct {
    cm_aio_op_t op;
    int fd;
    void* buffer;
    size_t len;
    void* user_data;
    int result;
    size_t bytes_transferred;
} cm_aio_request_t;

typedef struct cm_aio_queue cm_aio_queue_t;

cm_aio_queue_t* cm_aio_queue_create(size_t queue_depth);
void cm_aio_queue_destroy(cm_aio_queue_t* q);
int cm_aio_submit(cm_aio_queue_t* q, cm_aio_request_t* reqs, size_t count);
int cm_aio_wait(cm_aio_queue_t* q, cm_aio_request_t** completed, size_t* count, int timeout_ms);

/* ============================================================================
 * SIMD-Accelerated JSON Parsing
 * ========================================================================== */

struct CMJsonNode* cm_json_parse_fast(const char* json, size_t len);
cm_string_t* cm_json_stringify_fast(struct CMJsonNode* node);

/* Check for SIMD support */
int cm_cpu_has_sse2(void);
int cm_cpu_has_avx2(void);
int cm_cpu_has_neon(void);  /* ARM */

/* ============================================================================
 * Work-Stealing Thread Pool
 * ========================================================================== */

typedef struct cm_worker_pool cm_worker_pool_t;

typedef void (*cm_worker_task_t)(void* arg);

cm_worker_pool_t* cm_worker_pool_create(size_t num_workers);
void cm_worker_pool_destroy(cm_worker_pool_t* pool);
int cm_worker_pool_submit(cm_worker_pool_t* pool, cm_worker_task_t task, void* arg);
void cm_worker_pool_wait_all(cm_worker_pool_t* pool);

/* ============================================================================
 * Generational Garbage Collector (for low latency)
 * ========================================================================== */

void cm_gc_enable_generational(void);
void cm_gc_collect_minor(void);  /* Young generation only */
void cm_gc_collect_major(void);  /* Full GC */
void cm_gc_set_max_pause_ms(int ms);

/* ============================================================================
 * HTTP Server Optimizations
 * ========================================================================== */

typedef struct {
    int use_keepalive;
    int keepalive_timeout_sec;
    int max_requests_per_conn;
    int use_compression;  /* gzip/deflate */
    int compression_threshold;  /* min bytes to compress */
} cm_http_server_config_t;

typedef struct cm_http_server cm_http_server_t;

cm_http_server_t* cm_http_server_create(cm_http_server_config_t* config);
void cm_http_server_destroy(cm_http_server_t* server);
int cm_http_server_listen(cm_http_server_t* server, int port);
void cm_http_server_run(cm_http_server_t* server);  /* Event loop */

/* Route with caching */
void cm_http_route_cached(cm_http_server_t* server, const char* path,
                          int cache_seconds,
                          void (*handler)(CMHttpRequest*, CMHttpResponse*));

/* ============================================================================
 * Database Connection (with pooling)
 * ========================================================================== */

typedef struct cm_db_conn cm_db_conn_t;
typedef struct cm_db_result cm_db_result_t;

cm_db_conn_t* cm_db_connect(const char* connection_string);
void cm_db_close(cm_db_conn_t* conn);
cm_db_result_t* cm_db_query(cm_db_conn_t* conn, const char* sql);
cm_db_result_t* cm_db_query_async(cm_db_conn_t* conn, const char* sql);
cm_db_result_t* cm_db_query_params(cm_db_conn_t* conn, const char* sql, 
                                   const char** params, size_t param_count);

int cm_db_result_fetch_row(cm_db_result_t* result, cm_map_t** row);
void cm_db_result_free(cm_db_result_t* result);

/* ============================================================================
 * Caching Layer
 * ========================================================================== */

typedef struct cm_cache cm_cache_t;

cm_cache_t* cm_cache_create(size_t max_entries, size_t max_memory_bytes);
void cm_cache_destroy(cm_cache_t* cache);
void cm_cache_set(cm_cache_t* cache, const char* key, const void* data, size_t len, int ttl_seconds);
void* cm_cache_get(cm_cache_t* cache, const char* key, size_t* len);
void cm_cache_invalidate(cm_cache_t* cache, const char* pattern);  /* glob pattern */

/* ============================================================================
 * Metrics & Monitoring
 * ========================================================================== */

typedef struct {
    uint64_t requests_total;
    uint64_t requests_active;
    uint64_t bytes_in;
    uint64_t bytes_out;
    double avg_response_time_ms;
    double p99_response_time_ms;
    size_t gc_collections;
    size_t memory_used_bytes;
    size_t memory_peak_bytes;
} cm_backend_metrics_t;

void cm_backend_get_metrics(cm_backend_metrics_t* metrics);
void cm_backend_reset_metrics(void);
cm_string_t* cm_backend_metrics_json(void);

/* ============================================================================
 * Rate Limiting
 * ========================================================================== */

typedef struct cm_rate_limiter cm_rate_limiter_t;

cm_rate_limiter_t* cm_rate_limiter_create(int requests_per_second, int burst_size);
void cm_rate_limiter_destroy(cm_rate_limiter_t* limiter);
int cm_rate_limiter_check(cm_rate_limiter_t* limiter, const char* key);
void cm_rate_limiter_reset(cm_rate_limiter_t* limiter, const char* key);

#endif /* CM_BACKEND_H */
