/*
 * This file is concatenated after jx-runtime.c and compiled as the same
 * translation unit. That intentionally lets live cutover reuse the exact
 * private .64B verifier, Bag checkpoint, prelinker, channel bus, and hot-root
 * machinery that already gates the boot Book.
 */
#undef osaura_jx_runtime_task

extern const void *osaura_jx_runtime_candidate_bytes(void);
extern uint64_t osaura_jx_runtime_candidate_size(void);
extern void osaura_jx_runtime_candidate_consumed(void);

static jx64_book_view g_live_rollback_book;
static jx_bag_layout g_live_rollback_layout;
static jx_record_bag g_live_rollback_bag;
static jx_generation_root g_live_rollback_roots[JX_GENERATION_MAX];
static jx_program_probe g_live_rollback_programs[JX_GENERATION_MAX];
static size_t g_live_rollback_root_count;
static uint64_t g_live_rollback_active_generation;
static uint64_t g_live_rollback_previous_generation;
static uint8_t g_live_announced;

static void live_copy_bytes(void *target, const void *source, size_t bytes) {
    uint8_t *out = (uint8_t *)target;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0; i < bytes; ++i) out[i] = in[i];
}

static int live_layout_equal(const jx_bag_layout *a, const jx_bag_layout *b) {
    if (!a || !b) return 0;
    return bytes_equal((const uint8_t *)a, (const uint8_t *)b, sizeof *a);
}

static const jx_generation_root *live_saved_root(uint64_t generation) {
    for (size_t i = 0; i < g_live_rollback_root_count; ++i)
        if (g_live_rollback_roots[i].generation == generation)
            return &g_live_rollback_roots[i];
    return NULL;
}

static void live_snapshot_runtime(void) {
    g_live_rollback_book = g_book;
    g_live_rollback_layout = g_layout;
    g_live_rollback_bag = g_bag;
    live_copy_bytes(g_live_rollback_roots, g_roots, sizeof g_roots);
    live_copy_bytes(g_live_rollback_programs, g_programs, sizeof g_programs);
    g_live_rollback_root_count = g_root_count;
    g_live_rollback_active_generation = g_active_root ? g_active_root->generation : 0u;
    g_live_rollback_previous_generation = g_previous_generation;
}

static void live_restore_runtime(uint32_t old_endpoint) {
    g_book = g_live_rollback_book;
    g_layout = g_live_rollback_layout;
    g_bag = g_live_rollback_bag;
    live_copy_bytes(g_roots, g_live_rollback_roots, sizeof g_roots);
    live_copy_bytes(g_programs, g_live_rollback_programs, sizeof g_programs);
    g_root_count = g_live_rollback_root_count;
    g_active_root = find_root(g_live_rollback_active_generation);
    g_previous_generation = g_live_rollback_previous_generation;

    /* A failed candidate never gets to retain queued work or program authority. */
    g_bus.queue_head = 0u;
    g_bus.queue_count = 0u;
    g_bus.active_program_endpoint = old_endpoint;
    g_bus.paused = 0u;
}

static int live_ensure_program_endpoint(uint32_t endpoint_id) {
    if (find_endpoint(&g_bus, endpoint_id)) return 1;

    for (size_t i = 0; i < JX_GENERATION_MAX; ++i) {
        if (g_programs[i].endpoint_id != 0u) continue;
        g_programs[i].endpoint_id = endpoint_id;
        g_programs[i].deliveries = 0u;
        if (channel_bus_add_endpoint(&g_bus,
                                     endpoint_id,
                                     program_receive,
                                     &g_programs[i]) != 0)
            return 0;
        if (channel_bus_bind(&g_bus,
                             endpoint_id,
                             JX_RUNTIME_CHANNEL,
                             JX_CHANNEL_DIR_INOUT) != 0)
            return 0;
        return 1;
    }
    return 0;
}

static int live_activate_candidate(void) {
    if (!osaura_jx_runtime_candidate_queued() || !g_active_root || !g_bus_ready)
        return 0;

    const void *candidate_bytes = osaura_jx_runtime_candidate_bytes();
    uint64_t candidate_size = osaura_jx_runtime_candidate_size();
    if (!candidate_bytes || !candidate_size || candidate_size > JX64_MAX_BOOK_BYTES ||
        candidate_size > (uint64_t)(~(size_t)0))
        return 0;

    uint64_t old_generation = g_active_root->generation;
    uint32_t old_endpoint = g_active_root->endpoint_id;
    if (!old_generation || old_generation >= 0xffu) return 0;

    /*
     * Quiescent boundary: no channel deliveries occur while the candidate is
     * hashed, structurally verified, schema checked, and prelinked.
     * Scheduler preemption remains enabled, so shell/idle responsiveness is
     * unaffected by the potentially expensive admission work.
     */
    channel_bus_pause(&g_bus);
    bag_checkpoint();

    jx64_book_view next_book;
    int rc = load_jx64((const uint8_t *)candidate_bytes,
                       (size_t)candidate_size,
                       &next_book);
    if (rc != 0) {
        (void)channel_bus_resume(&g_bus);
        return 0;
    }

    jx_bag_layout next_layout;
    rc = parse_bag_layout(next_book.bag_schema,
                          next_book.bag_schema_bytes,
                          &next_layout);
    if (rc != 0 || !live_layout_equal(&g_layout, &next_layout)) {
        (void)channel_bus_resume(&g_bus);
        return 0;
    }

    live_snapshot_runtime();
    g_book = next_book;
    g_layout = next_layout;

    rc = build_generation_roots();
    if (rc != 0) {
        live_restore_runtime(old_endpoint);
        return 0;
    }

    jx_generation_root *continuity = find_root(old_generation);
    jx_generation_root *next = find_root(old_generation + 1u);
    const jx_generation_root *saved = live_saved_root(old_generation);
    if (!continuity || !next || !saved ||
        continuity->endpoint_id != old_endpoint ||
        !bytes_equal((const uint8_t *)continuity,
                     (const uint8_t *)saved,
                     sizeof *continuity)) {
        live_restore_runtime(old_endpoint);
        return 0;
    }

    /* Root replacement happens before authority changes, while still paused. */
    g_active_root = continuity;
    g_previous_generation = g_live_rollback_previous_generation;

    if (!live_ensure_program_endpoint(next->endpoint_id)) {
        live_restore_runtime(old_endpoint);
        return 0;
    }

    if (!generation_swap(next->generation)) {
        live_restore_runtime(old_endpoint);
        return 0;
    }

    osaura_jx_runtime_candidate_consumed();
    return g_active_root == next &&
           g_bus.active_program_endpoint == next->endpoint_id;
}

static void live_announce_when_admitted(void) {
    if (g_live_announced || g_errors || !g_book_loaded || !g_tables_ready ||
        !g_bus_ready || !g_active || !g_active_root ||
        osaura_jx_runtime_live_book_activations() == 0u ||
        g_active_root->generation != 3u ||
        !g_bag.hot[g_layout.bus_ticks] ||
        !g_bag.hot[g_layout.bus_collects] ||
        !g_bag.hot[g_layout.channel_deliveries] ||
        !g_bag.hot[g_layout.prepared_calls] ||
        !g_bag.hot[g_layout.hot_dispatches] ||
        !g_bag.hot[g_layout.reaction_runs] ||
        g_bag.hot[g_layout.generation_swaps] < 2u ||
        !g_bag.checkpoints)
        return;

    g_live_announced = 1u;
    serial_text("\nJX 64B: VERIFIED\n");
    serial_text("JX 64B CODE/APPLIED-BUS: LOADED\n");
    serial_text("JX BAG SCHEMA: LINKED\n");
    serial_text("JX BAG: ACTIVE\n");
    serial_text("JX BAG CHECKPOINT: ACTIVE\n");
    serial_text("JX PREPARED CALLS: ACTIVE\n");
    serial_text("JX HOT REGISTERS: ACTIVE\n");
    serial_text("JX REACTIONS: ACTIVE\n");
    serial_text("JX GENERATION 1->2: ACTIVE\n");
    serial_text("JX LIVE BOOK: VERIFIED\n");
    serial_text("JX GENERATION 2->3: ACTIVE\n");
    serial_text("JX LIVE BOOK CUTOVER: ACTIVE\n");
    serial_text("JX CHANNEL BUS: ACTIVE\n");
    serial_text("JX CHANNEL SWITCH: ACTIVE\n");
    serial_text("JX RUNTIME: ACTIVE\n");
    serial_text("JX APPLIED ABI: 1\n");
    serial_text("JX PAGE: 7F0001 7F0002\n");
    serial_text("JX BUS.TICK: ACTIVE\n");
    serial_text("JX BUS.COLLECT: ACTIVE\n");
}

__attribute__((noreturn)) void osaura_jx_runtime_task(void) {
    if (!g_book_loaded || !g_tables_ready || !g_active_root ||
        !osaura_jx_runtime_candidate_queued()) {
        runtime_error();
        serial_text("\nJX RUNTIME: BOOK/TABLES/CANDIDATE MISSING\n");
        for (;;) __asm__ volatile("hlt");
    }

    bag_init();
    bag_set(g_layout.active_generation, g_active_root->generation);

    if (!runtime_bus_init()) {
        runtime_error();
        serial_text("\nJX RUNTIME: CHANNEL BUS FAILED\n");
        for (;;) __asm__ volatile("hlt");
    }
    g_bus_ready = 1u;

    if (!prepared_smoke()) {
        runtime_error();
        serial_text("\nJX RUNTIME: PREPARED CALL FAILED\n");
        for (;;) __asm__ volatile("hlt");
    }
    if (!hot_dispatch(1u, 0u, 0u) || !hot_dispatch(1u, 1u, 0u)) {
        runtime_error();
        serial_text("\nJX RUNTIME: HOT GENERATION 1 FAILED\n");
        for (;;) __asm__ volatile("hlt");
    }
    if (!generation_swap(2u)) {
        runtime_error();
        serial_text("\nJX RUNTIME: GENERATION 1->2 FAILED\n");
        for (;;) __asm__ volatile("hlt");
    }
    if (!hot_dispatch(1u, 0u, 0u) || !hot_dispatch(1u, 1u, 0u)) {
        runtime_error();
        serial_text("\nJX RUNTIME: HOT GENERATION 2 FAILED\n");
        for (;;) __asm__ volatile("hlt");
    }

    g_active = 1u;

    if (!live_activate_candidate()) {
        runtime_error();
        serial_text("\nJX RUNTIME: LIVE BOOK CUTOVER FAILED\n");
        for (;;) __asm__ volatile("hlt");
    }
    if (!hot_dispatch(1u, 0u, 0u) || !hot_dispatch(1u, 1u, 0u)) {
        runtime_error();
        serial_text("\nJX RUNTIME: HOT GENERATION 3 FAILED\n");
        for (;;) __asm__ volatile("hlt");
    }

    for (;;) {
        (void)hot_dispatch(1u, 0u, 0u);
        (void)hot_dispatch(1u, 1u, 0u);
        (void)execute_applied_entry(OSAURA_JX_RUNTIME_TICK_OFFSET);
        (void)execute_applied_entry(OSAURA_JX_RUNTIME_COLLECT_OFFSET);
        live_announce_when_admitted();
        __asm__ volatile("hlt");
    }
}
