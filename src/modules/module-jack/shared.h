/* PipeWire
 * Copyright (C) 2015 Wim Taymans <wim.taymans@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <math.h>

extern int segment_num;

static inline int jack_shm_alloc(size_t size, jack_shm_info_t *info, int num)
{
	char name[64];

	snprintf(name, sizeof(name), "/jack_shared%d", num);

	if (jack_shmalloc(name, size, info)) {
		pw_log_error("Cannot create shared memory segment of size = %zd (%s)", size, strerror(errno));
		return -1;
	}

	if (jack_attach_shm(info)) {
		jack_error("Cannot attach shared memory segment name = %s err = %s", name, strerror(errno));
		jack_destroy_shm(info);
		return -1;
	}
	info->size = size;
	return 0;
}

typedef uint16_t jack_int_t;  // Internal type for ports and refnum

typedef enum {
	NotTriggered,
	Triggered,
	Running,
	Finished,
} jack_client_state_t;

PRE_PACKED_STRUCTURE
struct jack_client_timing {
	jack_time_t signaled_at;
	jack_time_t awake_at;
	jack_time_t finished_at;
	jack_client_state_t status;
} POST_PACKED_STRUCTURE;

#define JACK_CLIENT_TIMING_INIT (struct jack_client_timing) { 0, 0, 0, NotTriggered }

PRE_PACKED_STRUCTURE
struct jack_port {
	int type_id;
	enum JackPortFlags flags;
	char name[REAL_JACK_PORT_NAME_SIZE];
	char alias1[REAL_JACK_PORT_NAME_SIZE];
	char alias2[REAL_JACK_PORT_NAME_SIZE];
	int ref_num;

	jack_nframes_t latency;
	jack_nframes_t total_latency;
	jack_latency_range_t playback_latency;
	jack_latency_range_t capture_latency;
	uint8_t monitor_requests;

	bool in_use;
	jack_port_id_t tied;
	jack_default_audio_sample_t buffer[BUFFER_SIZE_MAX + 8];
} POST_PACKED_STRUCTURE;

static inline void jack_port_init(struct jack_port *port, int ref_num,
				  const char* port_name, int type_id, enum JackPortFlags flags)
{
	port->type_id = type_id;
	port->flags = flags;
	strcpy(port->name, port_name);
	port->alias1[0] = '\0';
	port->alias2[0] = '\0';
	port->ref_num = ref_num;
	port->latency = 0;
	port->total_latency = 0;
	port->playback_latency.min = port->playback_latency.max = 0;
	port->capture_latency.min = port->capture_latency.max = 0;
	port->monitor_requests = 0;
	port->in_use = true;
	port->tied = NO_PORT;
}

#define MAKE_FIXED_ARRAY(size)			\
PRE_PACKED_STRUCTURE				\
struct {					\
	jack_int_t table[size];			\
        uint32_t counter;			\
} POST_PACKED_STRUCTURE

#define INIT_FIXED_ARRAY(arr) ({			\
	int i;						\
	for (i = 0; i < SPA_N_ELEMENTS(arr.table); i++)	\
		arr.table[i] = EMPTY;			\
	arr.counter = 0;				\
})

#define ADD_FIXED_ARRAY(arr,item) ({				\
	int i,ret = -1;						\
	for (i = 0; i < SPA_N_ELEMENTS(arr.table); i++)	{	\
		if (arr.table[i] == EMPTY) {			\
			arr.table[i] = item;			\
			arr.counter++;				\
			ret = 0;				\
			break;					\
		}						\
	}							\
	ret;							\
})

#define MAKE_FIXED_ARRAY1(size)			\
PRE_PACKED_STRUCTURE				\
struct {					\
	MAKE_FIXED_ARRAY(size) array;		\
        bool used;				\
} POST_PACKED_STRUCTURE

#define INIT_FIXED_ARRAY1(arr) ({		\
	INIT_FIXED_ARRAY(arr.array);		\
	arr.used = false;			\
})

#define ADD_FIXED_ARRAY1(arr,item) ADD_FIXED_ARRAY(arr.array,item)

#define MAKE_FIXED_MATRIX(size)			\
PRE_PACKED_STRUCTURE				\
struct {					\
	jack_int_t table[size][size];		\
} POST_PACKED_STRUCTURE

#define INIT_FIXED_MATRIX(mat,idx) ({				\
	int i;							\
	for (i = 0; i < SPA_N_ELEMENTS(mat.table[0]); i++){	\
		mat.table[idx][i] = 0;				\
		mat.table[i][idx] = 0;				\
	}							\
})

PRE_PACKED_STRUCTURE
struct jack_activation_count {
	int32_t value;
	int32_t count;
} POST_PACKED_STRUCTURE;

static inline void jack_activation_count_set_value(struct jack_activation_count *cnt, int32_t val) {
	cnt->value = val;
}

#define MAKE_LOOP_FEEDBACK(size)		\
PRE_PACKED_STRUCTURE				\
struct {					\
	int table[size][3];			\
} POST_PACKED_STRUCTURE

#define INIT_LOOP_FEEDBACK(arr,size) ({				\
	int i;							\
	for (i = 0; i < size; i++) {				\
		arr.table[i][0] = EMPTY;			\
		arr.table[i][1] = EMPTY;			\
		arr.table[i][2] = 0;				\
	}							\
})

PRE_PACKED_STRUCTURE
struct jack_connection_manager {
	MAKE_FIXED_ARRAY(CONNECTION_NUM_FOR_PORT) connection[PORT_NUM_MAX];
	MAKE_FIXED_ARRAY1(PORT_NUM_FOR_CLIENT) input_port[CLIENT_NUM];
	MAKE_FIXED_ARRAY(PORT_NUM_FOR_CLIENT) output_port[CLIENT_NUM];
	MAKE_FIXED_MATRIX(CLIENT_NUM) connection_ref;
	struct jack_activation_count input_counter[CLIENT_NUM];
	MAKE_LOOP_FEEDBACK(CONNECTION_NUM_FOR_PORT) loop_feedback;
} POST_PACKED_STRUCTURE;

static inline void
jack_connection_manager_init_ref_num(struct jack_connection_manager *conn, int ref_num)
{
	INIT_FIXED_ARRAY1(conn->input_port[ref_num]);
	INIT_FIXED_ARRAY(conn->output_port[ref_num]);
	INIT_FIXED_MATRIX(conn->connection_ref, ref_num);
	jack_activation_count_set_value (&conn->input_counter[ref_num], 0);
}

static inline void
jack_connection_manager_init(struct jack_connection_manager *conn)
{
	int i;
	for (i = 0; i < PORT_NUM_MAX; i++)
		INIT_FIXED_ARRAY(conn->connection[i]);

	INIT_LOOP_FEEDBACK(conn->loop_feedback, CONNECTION_NUM_FOR_PORT);

	for (i = 0; i < CLIENT_NUM; i++)
		jack_connection_manager_init_ref_num(conn, i);
}

static inline int
jack_connection_manager_add_port(struct jack_connection_manager *conn, bool input,
				 int ref_num, jack_port_id_t port_id)
{
	if (input)
		return ADD_FIXED_ARRAY1(conn->input_port[ref_num], port_id);
	else
		return ADD_FIXED_ARRAY(conn->output_port[ref_num], port_id);
}

PRE_PACKED_STRUCTURE
struct jack_atomic_counter {
	union {
		struct {
			uint16_t short_val1;  // Cur
			uint16_t short_val2;  // Next
		} scounter;
		uint32_t long_val;
	} info;
} POST_PACKED_STRUCTURE;

#define Counter(e) (e).info.long_val
#define CurIndex(e) (e).info.scounter.short_val1
#define NextIndex(e) (e).info.scounter.short_val2

#define CurArrayIndex(e) (CurIndex(e) & 0x0001)
#define NextArrayIndex(e) ((CurIndex(e) + 1) & 0x0001)

#define MAKE_ATOMIC_STATE(type)				\
PRE_PACKED_STRUCTURE					\
struct {						\
	type state[2];					\
	volatile struct jack_atomic_counter counter;	\
        int32_t call_write_counter;			\
} POST_PACKED_STRUCTURE

PRE_PACKED_STRUCTURE
struct jack_atomic_array_counter {
	union {
		struct {
			unsigned char byte_val[4];
		} scounter;
		uint32_t long_val;
	} info;
} POST_PACKED_STRUCTURE;

#define MAKE_ATOMIC_ARRAY_STATE(type)				\
PRE_PACKED_STRUCTURE						\
struct {							\
	type state[3];						\
	volatile struct jack_atomic_array_counter counter;	\
} POST_PACKED_STRUCTURE

PRE_PACKED_STRUCTURE
struct jack_graph_manager {
	jack_shm_info_t info;
	MAKE_ATOMIC_STATE(struct jack_connection_manager) state;
	unsigned int port_max;
        struct jack_client_timing client_timing[CLIENT_NUM];
        struct jack_port port_array[0];
} POST_PACKED_STRUCTURE;

static inline struct jack_graph_manager *
jack_graph_manager_alloc(int port_max)
{
	struct jack_graph_manager *mgr;
        jack_shm_info_t info;
	size_t i, size;

	size = sizeof(struct jack_graph_manager) + port_max * sizeof(struct jack_port);

        if (jack_shm_alloc(size, &info, segment_num++) < 0)
                return NULL;

        mgr = (struct jack_graph_manager *)jack_shm_addr(&info);
        mgr->info = info;

	jack_connection_manager_init(&mgr->state.state[0]);
	jack_connection_manager_init(&mgr->state.state[1]);
	mgr->port_max = port_max;

	for (i = 0; i < port_max; i++) {
		mgr->port_array[i].in_use = false;
		mgr->port_array[i].ref_num = -1;
	}
	return mgr;
}

static inline jack_port_id_t
jack_graph_manager_allocate_port(struct jack_graph_manager *mgr,
				 int ref_num, const char* port_name, int type_id,
				 enum JackPortFlags flags)
{
	int i;
	for (i = 1; i < mgr->port_max; i++) {
		if (!mgr->port_array[i].in_use) {
			jack_port_init(&mgr->port_array[i], ref_num, port_name, type_id, flags);
			return i;
		}
	}
	return NO_PORT;
}


static inline struct jack_connection_manager *
jack_graph_manager_next_start(struct jack_graph_manager *manager)
{
	uint32_t next_index;

	if (manager->state.call_write_counter++ == 0) {
		struct jack_atomic_counter old_val;
		struct jack_atomic_counter new_val;
		uint32_t cur_index;
		bool need_copy;
		do {
			old_val = manager->state.counter;
			new_val = old_val;
			cur_index = CurArrayIndex(new_val);
			next_index = NextArrayIndex(new_val);
			need_copy = (CurIndex(new_val) == NextIndex(new_val));
			NextIndex(new_val) = CurIndex(new_val); // Invalidate next index
		}
		while (!__atomic_compare_exchange_n((uint32_t*)&manager->state.counter,
						    (uint32_t*)&Counter(old_val),
						    Counter(new_val),
						    false,
						    __ATOMIC_SEQ_CST,
						    __ATOMIC_SEQ_CST));

		if (need_copy)
			memcpy(&manager->state.state[next_index],
			       &manager->state.state[cur_index],
			       sizeof(struct jack_connection_manager));
	}
	else {
		next_index = NextArrayIndex(manager->state.counter);
	}
	return &manager->state.state[next_index];
}

static inline void
jack_graph_manager_next_stop(struct jack_graph_manager *manager)
{
	if (--manager->state.call_write_counter == 0) {
		struct jack_atomic_counter old_val;
		struct jack_atomic_counter new_val;
		do {
			old_val = manager->state.counter;
			new_val = old_val;
			NextIndex(new_val)++; // Set next index
		}
		while (!__atomic_compare_exchange_n((uint32_t*)&manager->state.counter,
						    (uint32_t*)&Counter(old_val),
						    Counter(new_val),
						    false,
						    __ATOMIC_SEQ_CST,
						    __ATOMIC_SEQ_CST));
	}
}

typedef enum {
    TransportCommandNone = 0,
    TransportCommandStart = 1,
    TransportCommandStop = 2,
} transport_command_t;

PRE_PACKED_STRUCTURE
struct jack_transport_engine {
	MAKE_ATOMIC_ARRAY_STATE(jack_position_t) state;
	jack_transport_state_t transport_state;
        volatile transport_command_t transport_cmd;
        transport_command_t previous_cmd;               /* previous transport_cmd */
        jack_time_t sync_timeout;
        int sync_time_left;
        int time_base_master;
        bool pending_pos;
        bool network_sync;
        bool conditionnal;
        int32_t write_counter;
} POST_PACKED_STRUCTURE;

PRE_PACKED_STRUCTURE
struct jack_timer {
	jack_nframes_t frames;
	jack_time_t current_wakeup;
	jack_time_t current_callback;
	jack_time_t next_wakeup;
	float period_usecs;
	float filter_omega; /* set once, never altered */
	bool initialized;
} POST_PACKED_STRUCTURE;

PRE_PACKED_STRUCTURE
struct jack_frame_timer {
	MAKE_ATOMIC_STATE(struct jack_timer) state;
        bool first_wakeup;
} POST_PACKED_STRUCTURE;

#ifdef JACK_MONITOR
PRE_PACKED_STRUCTURE
struct jack_timing_measure_client {
	int ref_num;
	jack_time_t signaled_at;
	jack_time_t awake_at;
	jack_time_t finished_at;
	jack_client_state_t status;
} POST_PACKED_STRUCTURE;

PRE_PACKED_STRUCTURE
struct jack_timing_client_interval {
	int ref_num;
	char name[JACK_CLIENT_NAME_SIZE+1];
	int begin_interval;
	int end_interval;
} POST_PACKED_STRUCTURE;

PRE_PACKED_STRUCTURE
struct jack_timing_measure {
	unsigned int audio_cycle;
	jack_time_t period_usecs;
	jack_time_t cur_cycle_begin;
	jack_time_t prev_cycle_end;
	struct jack_timing_measure_client client_table[CLIENT_NUM];
} POST_PACKED_STRUCTURE;

PRE_PACKED_STRUCTURE
struct jack_engine_profiling {
	struct jack_timing_measure profile_table[TIME_POINTS];
	struct jack_timing_client_interval interval_table[MEASURED_CLIENTS];

	unsigned int audio_cycle;
	unsigned int measured_client;
} POST_PACKED_STRUCTURE;
#endif

PRE_PACKED_STRUCTURE
struct jack_engine_control {
	jack_shm_info_t info;
	jack_nframes_t buffer_size;
	jack_nframes_t sample_rate;
	bool sync_mode;
	bool temporary;
	jack_time_t period_usecs;
	jack_time_t timeout_usecs;
	float max_delayed_usecs;
	float xrun_delayed_usecs;
	bool timeout;
	bool real_time;
	bool saved_real_time;
	int server_priority;
	int client_priority;
	int max_client_priority;
	char server_name[JACK_SERVER_NAME_SIZE];
	struct jack_transport_engine transport;
	jack_timer_type_t clock_source;
	int driver_num;
	bool verbose;

	jack_time_t prev_cycle_time;
	jack_time_t cur_cycle_time;
	jack_time_t spare_usecs;
	jack_time_t max_usecs;
	jack_time_t rolling_client_usecs[JACK_ENGINE_ROLLING_COUNT];
	unsigned int rolling_client_usecs_cnt;
	int rolling_client_usecs_index;
	int rolling_interval;
	float CPU_load;

	uint64_t period;
	uint64_t computation;
	uint64_t constraint;

	struct jack_frame_timer frame_timer;

#ifdef JACK_MONITOR
	struct jack_engine_profiling profiler;
#endif
} POST_PACKED_STRUCTURE;

static inline void
jack_engine_control_reset_rolling_usecs(struct jack_engine_control *ctrl)
{
    memset(ctrl->rolling_client_usecs, 0, sizeof(ctrl->rolling_client_usecs));
    ctrl->rolling_client_usecs_index = 0;
    ctrl->rolling_client_usecs_cnt = 0;
    ctrl->spare_usecs = 0;
    ctrl->rolling_interval = floor((JACK_ENGINE_ROLLING_INTERVAL * 1000.f) / ctrl->period_usecs);
}

static inline struct jack_engine_control *
jack_engine_control_alloc(const char* name)
{
	struct jack_engine_control *ctrl;
        jack_shm_info_t info;
	size_t size;

	size = sizeof(struct jack_engine_control);
        if (jack_shm_alloc(size, &info, segment_num++) < 0)
                return NULL;

        ctrl = (struct jack_engine_control *)jack_shm_addr(&info);
        ctrl->info = info;

	ctrl->buffer_size = 512;
        ctrl->sample_rate = 48000;
	ctrl->sync_mode = false;
	ctrl->temporary = false;
	ctrl->period_usecs = 1000000.f / ctrl->sample_rate * ctrl->buffer_size;
	ctrl->timeout_usecs = 0;
	ctrl->max_delayed_usecs = 0.f;
	ctrl->xrun_delayed_usecs = 0.f;
	ctrl->timeout = false;
	ctrl->real_time = true;
	ctrl->saved_real_time = false;
	ctrl->server_priority = 20;
	ctrl->client_priority = 15;
	ctrl->max_client_priority = 19;
        strcpy(ctrl->server_name, name);
	ctrl->clock_source = 0;
	ctrl->driver_num = 0;
	ctrl->verbose = true;

	ctrl->prev_cycle_time = 0;
	ctrl->cur_cycle_time = 0;
	ctrl->spare_usecs = 0;
	ctrl->max_usecs = 0;
	jack_engine_control_reset_rolling_usecs(ctrl);
	ctrl->CPU_load = 0.f;

	ctrl->period = 0;
	ctrl->computation = 0;
	ctrl->constraint = 0;

	return ctrl;
}

PRE_PACKED_STRUCTURE
struct jack_client_control {
	jack_shm_info_t info;
	char name[JACK_CLIENT_NAME_SIZE+1];
	bool callback[jack_notify_max];
	volatile jack_transport_state_t transport_state;
	volatile bool transport_sync;
	volatile bool transport_timebase;
	int ref_num;
	int PID;
	bool active;

	int session_ID;
	char session_command[JACK_SESSION_COMMAND_SIZE];
	jack_session_flags_t session_flags;
} POST_PACKED_STRUCTURE;

static inline struct jack_client_control *
jack_client_control_alloc(const char* name, int pid, int ref_num, int uuid)
{
	struct jack_client_control *ctrl;
        jack_shm_info_t info;
	size_t size;

	size = sizeof(struct jack_client_control);
        if (jack_shm_alloc(size, &info, segment_num++) < 0)
                return NULL;

        ctrl = (struct jack_client_control *)jack_shm_addr(&info);
        ctrl->info = info;

	strcpy(ctrl->name, name);
        for (int i = 0; i < jack_notify_max; i++)
            ctrl->callback[i] = false;

        // Always activated
        ctrl->callback[jack_notify_AddClient] = true;
        ctrl->callback[jack_notify_RemoveClient] = true;
        ctrl->callback[jack_notify_ActivateClient] = true;
        ctrl->callback[jack_notify_LatencyCallback] = true;
        // So that driver synchro are correctly setup in "flush" or "normal" mode
        ctrl->callback[jack_notify_StartFreewheelCallback] = true;
        ctrl->callback[jack_notify_StopFreewheelCallback] = true;
        ctrl->ref_num = ref_num;
        ctrl->PID = pid;
        ctrl->transport_state = JackTransportStopped;
        ctrl->transport_sync = false;
        ctrl->transport_timebase = false;
        ctrl->active = false;
        ctrl->session_ID = uuid;

	return ctrl;
}
