#include "cm/backend.h"
#include "cm/http.h"
#include "cm/json.h"
#include "cm/error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

/* ============================================================================
 * Connection Pool Implementation
 * ========================================================================== */

cm_connection_pool_t* cm_pool_create(size_t initial, size_t max,
                                      int (*create)(void**),
                                      void (*destroy)(void*),
                                      int (*validate)(void*)) {
    cm_connection_pool_t* pool = (cm_connection_pool_t*)cm_alloc(sizeof(cm_connection_pool_t), "conn_pool");
    if (!pool) return NULL;
    
    memset(pool, 0, sizeof(*pool));
    pool->connections = (void**)cm_alloc(sizeof(void*) * max, "conn_pool_array");
    if (!pool->connections) {
        cm_free(pool);
        return NULL;
    }
    
    pool->capacity = max;
    pool->max_size = max;
    pool->create_conn = create;
    pool->destroy_conn = destroy;
    pool->validate_conn = validate;
    pool->lock = cm_mutex_init();
    
    /* Pre-create initial connections */
    for (size_t i = 0; i < initial; i++) {
        void* conn = NULL;
        if (create(&conn) == 0) {
            pool->connections[pool->size++] = conn;
        }
    }
    
    return pool;
}

void cm_pool_destroy(cm_connection_pool_t* pool) {
    if (!pool) return;
    
    cm_mutex_lock(pool->lock);
    for (size_t i = 0; i < pool->size; i++) {
        if (pool->connections[i] && pool->destroy_conn) {
            pool->destroy_conn(pool->connections[i]);
        }
    }
    cm_mutex_unlock(pool->lock);
    
    cm_free(pool->connections);
    cm_mutex_destroy(pool->lock);
    cm_free(pool);
}

void* cm_pool_acquire(cm_connection_pool_t* pool) {
    if (!pool) return NULL;
    
    cm_mutex_lock(pool->lock);
    
    /* Try to get existing connection */
    for (size_t i = 0; i < pool->size; i++) {
        void* conn = pool->connections[i];
        if (conn && pool->validate_conn && pool->validate_conn(conn) == 0) {
            pool->connections[i] = NULL;  /* Mark as in-use */
            cm_mutex_unlock(pool->lock);
            return conn;
        }
    }
    
    /* Create new if under limit */
    if (pool->size < pool->max_size && pool->create_conn) {
        void* conn = NULL;
        if (pool->create_conn(&conn) == 0) {
            pool->size++;
            cm_mutex_unlock(pool->lock);
            return conn;
        }
    }
    
    cm_mutex_unlock(pool->lock);
    return NULL;
}

int cm_pool_release(cm_connection_pool_t* pool, void* conn) {
    if (!pool || !conn) return -1;
    
    cm_mutex_lock(pool->lock);
    
    /* Find empty slot */
    for (size_t i = 0; i < pool->size; i++) {
        if (pool->connections[i] == NULL) {
            pool->connections[i] = conn;
            cm_mutex_unlock(pool->lock);
            return 0;
        }
    }
    
    /* Extend array if needed */
    if (pool->size < pool->capacity) {
        pool->connections[pool->size++] = conn;
        cm_mutex_unlock(pool->lock);
        return 0;
    }
    
    cm_mutex_unlock(pool->lock);
    
    /* No room, destroy */
    if (pool->destroy_conn) {
        pool->destroy_conn(conn);
    }
    return -1;
}

/* ============================================================================
 * Zero-Copy Buffer Implementation
 * ========================================================================== */

cm_zerocopy_buffer_t* cm_buffer_create(size_t capacity) {
    cm_zerocopy_buffer_t* buf = (cm_zerocopy_buffer_t*)cm_alloc(sizeof(cm_zerocopy_buffer_t), "zcbuffer");
    if (!buf) return NULL;
    
    buf->data = (char*)cm_alloc(capacity, "zcbuffer_data");
    if (!buf->data) {
        cm_free(buf);
        return NULL;
    }
    
    buf->capacity = capacity;
    buf->read_pos = 0;
    buf->write_pos = 0;
    buf->ref_count = 1;
    
    return buf;
}

cm_zerocopy_buffer_t* cm_buffer_ref(cm_zerocopy_buffer_t* buf) {
    if (!buf) return NULL;
    /* Note: In real implementation, would use atomic increment */
    buf->ref_count++;
    return buf;
}

void cm_buffer_unref(cm_zerocopy_buffer_t* buf) {
    if (!buf) return;
    /* Note: In real implementation, would use atomic decrement */
    buf->ref_count--;
    if (buf->ref_count <= 0) {
        if (buf->data) cm_free(buf->data);
        cm_free(buf);
    }
}

size_t cm_buffer_write(cm_zerocopy_buffer_t* buf, const void* data, size_t len) {
    if (!buf || !data || len == 0) return 0;
    
    size_t available = buf->capacity - buf->write_pos;
    size_t to_write = (len < available) ? len : available;
    
    memcpy(buf->data + buf->write_pos, data, to_write);
    buf->write_pos += to_write;
    
    return to_write;
}

size_t cm_buffer_read(cm_zerocopy_buffer_t* buf, void* dest, size_t len) {
    if (!buf || !dest || len == 0) return 0;
    
    size_t available = buf->write_pos - buf->read_pos;
    size_t to_read = (len < available) ? len : available;
    
    memcpy(dest, buf->data + buf->read_pos, to_read);
    buf->read_pos += to_read;
    
    return to_read;
}

void cm_buffer_reset(cm_zerocopy_buffer_t* buf) {
    if (!buf) return;
    buf->read_pos = 0;
    buf->write_pos = 0;
}

/* ============================================================================
 * String Interning Pool
 * ========================================================================== */

cm_string_pool_t* cm_string_pool_create(void) {
    cm_string_pool_t* pool = (cm_string_pool_t*)cm_alloc(sizeof(cm_string_pool_t), "string_pool");
    if (!pool) return NULL;
    
    pool->table = cm_map_new();
    pool->lock = cm_mutex_init();
    
    return pool;
}

void cm_string_pool_destroy(cm_string_pool_t* pool) {
    if (!pool) return;
    
    cm_mutex_lock(pool->lock);
    /* Free all interned strings */
    cm_map_free(pool->table);
    cm_mutex_unlock(pool->lock);
    
    cm_mutex_destroy(pool->lock);
    cm_free(pool);
}

const char* cm_string_pool_intern(cm_string_pool_t* pool, const char* str) {
    if (!pool || !str) return NULL;
    
    cm_mutex_lock(pool->lock);
    
    /* Check if already interned */
    cm_string_t** existing = (cm_string_t**)cm_map_get(pool->table, str);
    if (existing) {
        cm_mutex_unlock(pool->lock);
        return (*existing)->data;
    }
    
    /* Create new interned string */
    cm_string_t* interned = cm_string_new(str);
    cm_map_set(pool->table, str, &interned, sizeof(cm_string_t*));
    
    cm_mutex_unlock(pool->lock);
    return interned->data;
}

/* ============================================================================
 * Worker Thread Pool (Basic Implementation)
 * ========================================================================== */

struct cm_worker_pool {
    CMThread* workers;
    size_t num_workers;
    CMMutex queue_lock;
    /* Simple task queue - in production, use lock-free queue */
    void** tasks;
    size_t task_count;
    size_t task_capacity;
    int shutdown;
};

static void* cm_worker_thread(void* arg) {
    cm_worker_pool_t* pool = (cm_worker_pool_t*)arg;
    
    while (!pool->shutdown) {
        cm_mutex_lock(pool->queue_lock);
        
        if (pool->task_count > 0) {
            /* Get task */
            void* task = pool->tasks[0];
            /* Shift queue */
            for (size_t i = 0; i < pool->task_count - 1; i++) {
                pool->tasks[i] = pool->tasks[i + 1];
            }
            pool->task_count--;
            cm_mutex_unlock(pool->queue_lock);
            
            /* Execute task */
            cm_worker_task_t* task_fn = (cm_worker_task_t*)task;
            if (task_fn) {
                (*task_fn)(NULL);  /* Task arg would come from task struct */
            }
        } else {
            cm_mutex_unlock(pool->queue_lock);
            /* Small sleep to prevent busy-waiting */
#ifdef _WIN32
            Sleep(1);
#else
            usleep(1000);
#endif
        }
    }
    
    return NULL;
}

cm_worker_pool_t* cm_worker_pool_create(size_t num_workers) {
    cm_worker_pool_t* pool = (cm_worker_pool_t*)cm_alloc(sizeof(cm_worker_pool_t), "worker_pool");
    if (!pool) return NULL;
    
    memset(pool, 0, sizeof(*pool));
    pool->num_workers = num_workers;
    pool->workers = (CMThread*)cm_alloc(sizeof(CMThread) * num_workers, "worker_threads");
    pool->tasks = (void**)cm_alloc(sizeof(void*) * 1024, "task_queue");
    pool->task_capacity = 1024;
    pool->queue_lock = cm_mutex_init();
    
    /* Start worker threads */
    for (size_t i = 0; i < num_workers; i++) {
        pool->workers[i] = cm_thread_create(cm_worker_thread, pool);
    }
    
    return pool;
}

void cm_worker_pool_destroy(cm_worker_pool_t* pool) {
    if (!pool) return;
    
    /* Signal shutdown */
    pool->shutdown = 1;
    
    /* Wait for workers */
    for (size_t i = 0; i < pool->num_workers; i++) {
        cm_thread_join(pool->workers[i]);
    }
    
    cm_free(pool->workers);
    cm_free(pool->tasks);
    cm_mutex_destroy(pool->queue_lock);
    cm_free(pool);
}

int cm_worker_pool_submit(cm_worker_pool_t* pool, cm_worker_task_t task, void* arg) {
    if (!pool || !task) return -1;
    (void)arg; /* Not used in simplified implementation */
    
    cm_mutex_lock(pool->queue_lock);
    
    if (pool->task_count >= pool->task_capacity) {
        cm_mutex_unlock(pool->queue_lock);
        return -1;  /* Queue full */
    }
    
    /* Store task using union to avoid strict aliasing issues */
    union { cm_worker_task_t fn; void* ptr; } converter;
    converter.fn = task;
    pool->tasks[pool->task_count++] = converter.ptr;
    
    cm_mutex_unlock(pool->queue_lock);
    return 0;
}

void cm_worker_pool_wait_all(cm_worker_pool_t* pool) {
    if (!pool) return;
    
    /* Wait for queue to empty */
    while (1) {
        cm_mutex_lock(pool->queue_lock);
        int empty = (pool->task_count == 0);
        cm_mutex_unlock(pool->queue_lock);
        
        if (empty) break;
        
#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif
    }
}

/* ============================================================================
 * Metrics Implementation
 * ========================================================================== */

static cm_backend_metrics_t g_metrics = {0};
static CMMutex g_metrics_lock;
static int g_metrics_initialized = 0;

static void cm_metrics_init(void) {
    if (!g_metrics_initialized) {
        g_metrics_lock = cm_mutex_init();
        g_metrics_initialized = 1;
    }
}

void cm_backend_get_metrics(cm_backend_metrics_t* metrics) {
    if (!metrics) return;
    cm_metrics_init();
    
    cm_mutex_lock(g_metrics_lock);
    memcpy(metrics, &g_metrics, sizeof(cm_backend_metrics_t));
    cm_mutex_unlock(g_metrics_lock);
}

void cm_backend_reset_metrics(void) {
    cm_metrics_init();
    
    cm_mutex_lock(g_metrics_lock);
    memset(&g_metrics, 0, sizeof(g_metrics));
    cm_mutex_unlock(g_metrics_lock);
}

void cm_backend_record_request(double response_time_ms) {
    cm_metrics_init();
    
    cm_mutex_lock(g_metrics_lock);
    g_metrics.requests_total++;
    
    /* Update average */
    double old_avg = g_metrics.avg_response_time_ms;
    g_metrics.avg_response_time_ms = old_avg + (response_time_ms - old_avg) / g_metrics.requests_total;
    
    /* Track P99 (simplified - real impl would use histogram) */
    if (response_time_ms > g_metrics.p99_response_time_ms * 0.99) {
        g_metrics.p99_response_time_ms = response_time_ms;
    }
    
    cm_mutex_unlock(g_metrics_lock);
}

cm_string_t* cm_backend_metrics_json(void) {
    cm_backend_metrics_t m;
    cm_backend_get_metrics(&m);
    
    return cm_string_format(
        "{"
        "\"requests_total\": %llu,"
        "\"requests_active\": %llu,"
        "\"bytes_in\": %llu,"
        "\"bytes_out\": %llu,"
        "\"avg_response_time_ms\": %.2f,"
        "\"p99_response_time_ms\": %.2f,"
        "\"gc_collections\": %zu,"
        "\"memory_used_bytes\": %zu,"
        "\"memory_peak_bytes\": %zu"
        "}",
        m.requests_total,
        m.requests_active,
        m.bytes_in,
        m.bytes_out,
        m.avg_response_time_ms,
        m.p99_response_time_ms,
        m.gc_collections,
        m.memory_used_bytes,
        m.memory_peak_bytes
    );
}

/* ============================================================================
 * SIMD Detection
 * ========================================================================== */

int cm_cpu_has_sse2(void) {
#if defined(__x86_64__) || defined(_M_X64)
    /* x86-64 always has SSE2 */
    return 1;
#else
    /* Would need CPUID check for x86 */
    return 0;
#endif
}

int cm_cpu_has_avx2(void) {
    /* Would need CPUID check */
    return 0;
}

int cm_cpu_has_neon(void) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return 1;
#else
    return 0;
#endif
}

/* ============================================================================
 * Generational GC (stubs - would integrate with existing GC)
 * ========================================================================== */

void cm_gc_enable_generational(void) {
    /* Would configure GC for generational mode */
}

void cm_gc_collect_minor(void) {
    /* Collect young generation only */
    cm_gc_collect();
}

void cm_gc_collect_major(void) {
    /* Full GC */
    cm_gc_collect();
}

void cm_gc_set_max_pause_ms(int ms) {
    /* Configure max pause time target */
    (void)ms;
}
