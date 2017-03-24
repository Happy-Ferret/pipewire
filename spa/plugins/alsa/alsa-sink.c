/* Spa ALSA Sink
 * Copyright (C) 2016 Wim Taymans <wim.taymans@gmail.com>
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

#include <stddef.h>

#include <asoundlib.h>

#include <spa/node.h>
#include <spa/audio/format.h>
#include <lib/props.h>

#include "alsa-utils.h"

#define CHECK_PORT(this,d,p)    ((d) == SPA_DIRECTION_INPUT && (p) == 0)

typedef struct _SpaALSAState SpaALSASink;

static const char default_device[] = "default";
static const uint32_t default_period_size = 128;
static const uint32_t default_periods = 2;
static const bool default_period_event = 0;

static void
reset_alsa_sink_props (SpaALSAProps *props)
{
  strncpy (props->device, default_device, 64);
  props->period_size = default_period_size;
  props->periods = default_periods;
  props->period_event = default_period_event;
}

static void
update_state (SpaALSASink *this, SpaNodeState state)
{
  spa_log_info (this->log, "update state %d", state);
  this->node.state = state;
}

#define PROP(f,key,type,...)                                                    \
          SPA_POD_PROP (f,key,SPA_POD_PROP_FLAG_READWRITE,type,1,__VA_ARGS__)
#define PROP_MM(f,key,type,...)                                                 \
          SPA_POD_PROP (f,key,SPA_POD_PROP_FLAG_READWRITE |                     \
                              SPA_POD_PROP_RANGE_MIN_MAX,type,3,__VA_ARGS__)
#define PROP_U_MM(f,key,type,...)                                               \
          SPA_POD_PROP (f,key,SPA_POD_PROP_FLAG_READWRITE |                     \
                              SPA_POD_PROP_FLAG_UNSET |                         \
                              SPA_POD_PROP_RANGE_MIN_MAX,type,3,__VA_ARGS__)
#define PROP_U_EN(f,key,type,n,...)                                             \
          SPA_POD_PROP (f,key,SPA_POD_PROP_FLAG_READWRITE |                     \
                              SPA_POD_PROP_FLAG_UNSET |                         \
                              SPA_POD_PROP_RANGE_ENUM,type,n,__VA_ARGS__)


static SpaResult
spa_alsa_sink_node_get_props (SpaNode       *node,
                              SpaProps     **props)
{
  SpaALSASink *this;
  SpaPODBuilder b = { NULL,  };
  SpaPODFrame f[2];

  if (node == NULL || props == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  spa_pod_builder_init (&b, this->props_buffer, sizeof (this->props_buffer));

  spa_pod_builder_props (&b, &f[0], this->type.props,
        PROP    (&f[1], this->type.prop_device,      -SPA_POD_TYPE_STRING, this->props.device, sizeof (this->props.device)),
        PROP    (&f[1], this->type.prop_device_name, -SPA_POD_TYPE_STRING, this->props.device_name, sizeof (this->props.device_name)),
        PROP    (&f[1], this->type.prop_card_name,   -SPA_POD_TYPE_STRING, this->props.card_name, sizeof (this->props.card_name)),
        PROP_MM (&f[1], this->type.prop_period_size,  SPA_POD_TYPE_INT,    this->props.period_size, 1, INT32_MAX),
        PROP_MM (&f[1], this->type.prop_periods,      SPA_POD_TYPE_INT,    this->props.periods, 1, INT32_MAX),
        PROP    (&f[1], this->type.prop_period_event, SPA_POD_TYPE_BOOL,   this->props.period_event));

  *props = SPA_POD_BUILDER_DEREF (&b, f[0].ref, SpaProps);

  return SPA_RESULT_OK;
}

static SpaResult
spa_alsa_sink_node_set_props (SpaNode         *node,
                              const SpaProps  *props)
{
  SpaALSASink *this;

  if (node == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  if (props == NULL) {
    reset_alsa_sink_props (&this->props);
    return SPA_RESULT_OK;
  } else {
    spa_props_query (props,
        this->type.prop_device,      -SPA_POD_TYPE_STRING, this->props.device, sizeof (this->props.device),
        this->type.prop_period_size,  SPA_POD_TYPE_INT,    &this->props.period_size,
        this->type.prop_periods,      SPA_POD_TYPE_INT,    &this->props.periods,
        this->type.prop_period_event, SPA_POD_TYPE_BOOL,   &this->props.period_event,
        0);
  }
  return SPA_RESULT_OK;
}

static SpaResult
do_send_event (SpaLoop        *loop,
               bool            async,
               uint32_t        seq,
               size_t          size,
               void           *data,
               void           *user_data)
{
  SpaALSASink *this = user_data;

  this->event_cb (&this->node, data, this->user_data);

  return SPA_RESULT_OK;
}

static SpaResult
do_command (SpaLoop        *loop,
            bool            async,
            uint32_t        seq,
            size_t          size,
            void           *data,
            void           *user_data)
{
  SpaALSASink *this = user_data;
  SpaResult res;
  SpaCommand *cmd = data;

  if (SPA_COMMAND_TYPE (cmd) == this->type.command_node.Start ||
      SPA_COMMAND_TYPE (cmd) == this->type.command_node.Pause) {
    res = spa_node_port_send_command (&this->node,
                                      SPA_DIRECTION_INPUT,
                                      0,
                                      cmd);
  }
  else
    res = SPA_RESULT_NOT_IMPLEMENTED;

  if (async) {
    SpaEventNodeAsyncComplete ac = SPA_EVENT_NODE_ASYNC_COMPLETE_INIT (this->type.event_node.AsyncComplete,
                                                                       seq, res);
    spa_loop_invoke (this->main_loop,
                     do_send_event,
                     SPA_ID_INVALID,
                     sizeof (ac),
                     &ac,
                     this);
  }
  return res;
}

static SpaResult
spa_alsa_sink_node_send_command (SpaNode    *node,
                                 SpaCommand *command)
{
  SpaALSASink *this;

  if (node == NULL || command == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  if (SPA_COMMAND_TYPE (command) == this->type.command_node.Start ||
      SPA_COMMAND_TYPE (command) == this->type.command_node.Pause) {
    if (!this->have_format)
      return SPA_RESULT_NO_FORMAT;

    if (this->n_buffers == 0)
      return SPA_RESULT_NO_BUFFERS;

    return spa_loop_invoke (this->data_loop,
                            do_command,
                            ++this->seq,
                            SPA_POD_SIZE (command),
                            command,
                            this);

  }
  else
    return SPA_RESULT_NOT_IMPLEMENTED;
}

static SpaResult
spa_alsa_sink_node_set_event_callback (SpaNode              *node,
                                       SpaEventNodeCallback  event,
                                       void                 *user_data)
{
  SpaALSASink *this;

  if (node == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  this->event_cb = event;
  this->user_data = user_data;

  return SPA_RESULT_OK;
}

static SpaResult
spa_alsa_sink_node_get_n_ports (SpaNode       *node,
                                uint32_t      *n_input_ports,
                                uint32_t      *max_input_ports,
                                uint32_t      *n_output_ports,
                                uint32_t      *max_output_ports)
{
  if (node == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  if (n_input_ports)
    *n_input_ports = 1;
  if (max_input_ports)
    *max_input_ports = 1;
  if (n_output_ports)
    *n_output_ports = 0;
  if (max_output_ports)
    *max_output_ports = 0;

  return SPA_RESULT_OK;
}

static SpaResult
spa_alsa_sink_node_get_port_ids (SpaNode       *node,
                                 uint32_t       n_input_ports,
                                 uint32_t      *input_ids,
                                 uint32_t       n_output_ports,
                                 uint32_t      *output_ids)
{
  if (node == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  if (n_input_ports > 0 && input_ids != NULL)
    input_ids[0] = 0;

  return SPA_RESULT_OK;
}


static SpaResult
spa_alsa_sink_node_add_port (SpaNode        *node,
                             SpaDirection    direction,
                             uint32_t        port_id)
{
  return SPA_RESULT_NOT_IMPLEMENTED;
}

static SpaResult
spa_alsa_sink_node_remove_port (SpaNode        *node,
                                SpaDirection    direction,
                                uint32_t        port_id)
{
  return SPA_RESULT_NOT_IMPLEMENTED;
}

static SpaResult
spa_alsa_sink_node_port_enum_formats (SpaNode         *node,
                                      SpaDirection     direction,
                                      uint32_t         port_id,
                                      SpaFormat      **format,
                                      const SpaFormat *filter,
                                      uint32_t         index)
{
  SpaALSASink *this;
  SpaResult res;
  SpaFormat *fmt;
  uint8_t buffer[1024];
  SpaPODBuilder b = { NULL, };
  SpaPODFrame f[2];

  if (node == NULL || format == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  if (!CHECK_PORT (this, direction, port_id))
    return SPA_RESULT_INVALID_PORT;

next:
  spa_pod_builder_init (&b, buffer, sizeof (buffer));

  switch (index++) {
    case 0:
      spa_pod_builder_format (&b, &f[0], this->type.format,
        this->type.media_type.audio, this->type.media_subtype.raw,
        PROP_U_EN (&f[1], this->type.format_audio.format,   SPA_POD_TYPE_ID,  3, this->type.audio_format.S16,
                                                                              this->type.audio_format.S16,
                                                                              this->type.audio_format.S32),
        PROP_U_MM (&f[1], this->type.format_audio.rate,     SPA_POD_TYPE_INT, 44100, 1, INT32_MAX),
        PROP_U_MM (&f[1], this->type.format_audio.channels, SPA_POD_TYPE_INT, 2,     1, INT32_MAX));
      break;
    case 1:
      spa_pod_builder_format (&b, &f[0], this->type.format,
        this->type.media_type.audio, this->type.media_subtype_audio.aac,
        SPA_POD_TYPE_NONE);
      break;
    default:
      return SPA_RESULT_ENUM_END;
  }
  fmt = SPA_POD_BUILDER_DEREF (&b, f[0].ref, SpaFormat);

  spa_pod_builder_init (&b, this->format_buffer, sizeof (this->format_buffer));

  if ((res = spa_format_filter (fmt, filter, &b)) != SPA_RESULT_OK)
    goto next;

  *format = SPA_POD_BUILDER_DEREF (&b, 0, SpaFormat);

  return SPA_RESULT_OK;
}

static SpaResult
spa_alsa_clear_buffers (SpaALSASink *this)
{
  if (this->n_buffers > 0) {
    spa_list_init (&this->ready);
    this->n_buffers = 0;
    this->ringbuffer = NULL;
  }
  return SPA_RESULT_OK;
}

static SpaResult
spa_alsa_sink_node_port_set_format (SpaNode            *node,
                                    SpaDirection        direction,
                                    uint32_t            port_id,
                                    SpaPortFormatFlags  flags,
                                    const SpaFormat    *format)
{
  SpaALSASink *this;
  SpaPODBuilder b = { NULL };
  SpaPODFrame f[2];

  if (node == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  if (!CHECK_PORT (this, direction, port_id))
    return SPA_RESULT_INVALID_PORT;

  if (format == NULL) {
    spa_log_info (this->log, "clear format");
    spa_alsa_pause (this, false);
    spa_alsa_clear_buffers (this);
    spa_alsa_close (this);
    this->have_format = false;
  } else {
    SpaAudioInfo info = { SPA_FORMAT_MEDIA_TYPE (format),
                          SPA_FORMAT_MEDIA_SUBTYPE (format), };

    if (info.media_type != this->type.media_type.audio ||
        info.media_subtype != this->type.media_subtype.raw)
      return SPA_RESULT_INVALID_MEDIA_TYPE;

    if (!spa_format_audio_raw_parse (format, &info.info.raw, &this->type.format_audio))
      return SPA_RESULT_INVALID_MEDIA_TYPE;

    if (spa_alsa_set_format (this, &info, flags) < 0)
      return SPA_RESULT_ERROR;

    this->current_format = info;
    this->have_format = true;
  }

  if (this->have_format) {
    this->info.flags = SPA_PORT_INFO_FLAG_CAN_USE_BUFFERS |
                       SPA_PORT_INFO_FLAG_LIVE;
    this->info.maxbuffering = this->buffer_frames * this->frame_size;
    this->info.latency = (this->period_frames * SPA_NSEC_PER_SEC) / this->rate;
    this->info.n_params = 3;
    this->info.params = this->params;

    spa_pod_builder_init (&b, this->params_buffer, sizeof (this->params_buffer));
    spa_pod_builder_object (&b, &f[0], 0, this->type.alloc_param_buffers.Buffers,
        PROP    (&f[1], this->type.alloc_param_buffers.size,    SPA_POD_TYPE_INT, this->period_frames * this->frame_size),
        PROP    (&f[1], this->type.alloc_param_buffers.stride,  SPA_POD_TYPE_INT, 0),
        PROP_MM (&f[1], this->type.alloc_param_buffers.buffers, SPA_POD_TYPE_INT, 32, 1, 32),
        PROP    (&f[1], this->type.alloc_param_buffers.align,   SPA_POD_TYPE_INT, 16));
    this->params[0] = SPA_POD_BUILDER_DEREF (&b, f[0].ref, SpaAllocParam);

    spa_pod_builder_object (&b, &f[0], 0, this->type.alloc_param_meta_enable.MetaEnable,
        PROP    (&f[1], this->type.alloc_param_meta_enable.type, SPA_POD_TYPE_INT, SPA_META_TYPE_HEADER));
    this->params[1] = SPA_POD_BUILDER_DEREF (&b, f[0].ref, SpaAllocParam);

    spa_pod_builder_object (&b, &f[0], 0, this->type.alloc_param_meta_enable.MetaEnable,
        PROP    (&f[1], this->type.alloc_param_meta_enable.type, SPA_POD_TYPE_INT, SPA_META_TYPE_RINGBUFFER),
        PROP    (&f[1], this->type.alloc_param_meta_enable.ringbufferSize,   SPA_POD_TYPE_INT, this->period_frames * this->frame_size * 32),
        PROP    (&f[1], this->type.alloc_param_meta_enable.ringbufferStride, SPA_POD_TYPE_INT, 0),
        PROP    (&f[1], this->type.alloc_param_meta_enable.ringbufferBlocks, SPA_POD_TYPE_INT, 1),
        PROP    (&f[1], this->type.alloc_param_meta_enable.ringbufferAlign,  SPA_POD_TYPE_INT, 16));
    this->params[2] = SPA_POD_BUILDER_DEREF (&b, f[0].ref, SpaAllocParam);
    this->info.extra = NULL;

    update_state (this, SPA_NODE_STATE_READY);
  }
  else
    update_state (this, SPA_NODE_STATE_CONFIGURE);

  return SPA_RESULT_OK;
}

static SpaResult
spa_alsa_sink_node_port_get_format (SpaNode          *node,
                                    SpaDirection      direction,
                                    uint32_t          port_id,
                                    const SpaFormat **format)
{
  SpaALSASink *this;

  if (node == NULL || format == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  if (!CHECK_PORT (this, direction, port_id))
    return SPA_RESULT_INVALID_PORT;

  if (!this->have_format)
    return SPA_RESULT_NO_FORMAT;

  *format = NULL;

  return SPA_RESULT_OK;
}

static SpaResult
spa_alsa_sink_node_port_get_info (SpaNode            *node,
                                  SpaDirection        direction,
                                  uint32_t            port_id,
                                  const SpaPortInfo **info)
{
  SpaALSASink *this;

  if (node == NULL || info == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  if (!CHECK_PORT (this, direction, port_id))
    return SPA_RESULT_INVALID_PORT;

  *info = &this->info;

  return SPA_RESULT_OK;
}

static SpaResult
spa_alsa_sink_node_port_get_props (SpaNode       *node,
                                   SpaDirection   direction,
                                   uint32_t       port_id,
                                   SpaProps     **props)
{
  return SPA_RESULT_NOT_IMPLEMENTED;
}

static SpaResult
spa_alsa_sink_node_port_set_props (SpaNode         *node,
                                   SpaDirection     direction,
                                   uint32_t         port_id,
                                   const SpaProps  *props)
{
  return SPA_RESULT_NOT_IMPLEMENTED;
}

static SpaResult
spa_alsa_sink_node_port_use_buffers (SpaNode         *node,
                                     SpaDirection     direction,
                                     uint32_t         port_id,
                                     SpaBuffer      **buffers,
                                     uint32_t         n_buffers)
{
  SpaALSASink *this;
  int i;

  if (node == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  if (!CHECK_PORT (this, direction, port_id))
    return SPA_RESULT_INVALID_PORT;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  spa_log_info (this->log, "use buffers %d", n_buffers);

  if (!this->have_format)
    return SPA_RESULT_NO_FORMAT;

  if (n_buffers == 0) {
    spa_alsa_pause (this, false);
    spa_alsa_clear_buffers (this);
    update_state (this, SPA_NODE_STATE_READY);
    return SPA_RESULT_OK;
  }

  for (i = 0; i < n_buffers; i++) {
    SpaALSABuffer *b = &this->buffers[i];
    b->outbuf = buffers[i];
    b->outstanding = true;

    b->h = spa_buffer_find_meta (b->outbuf, SPA_META_TYPE_HEADER);
    b->rb = spa_buffer_find_meta (b->outbuf, SPA_META_TYPE_RINGBUFFER);
    if (b->rb)
      this->ringbuffer = b;

    switch (buffers[i]->datas[0].type) {
      case SPA_DATA_TYPE_MEMFD:
      case SPA_DATA_TYPE_DMABUF:
      case SPA_DATA_TYPE_MEMPTR:
        if (buffers[i]->datas[0].data == NULL) {
          spa_log_error (this->log, "alsa-source: need mapped memory");
          continue;
        }
        break;
      default:
        break;
    }
  }
  this->n_buffers = n_buffers;

  update_state (this, SPA_NODE_STATE_PAUSED);

  return SPA_RESULT_OK;
}

static SpaResult
spa_alsa_sink_node_port_alloc_buffers (SpaNode         *node,
                                       SpaDirection     direction,
                                       uint32_t         port_id,
                                       SpaAllocParam  **params,
                                       uint32_t         n_params,
                                       SpaBuffer      **buffers,
                                       uint32_t        *n_buffers)
{
  SpaALSASink *this;

  if (node == NULL || buffers == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  if (!CHECK_PORT (this, direction, port_id))
    return SPA_RESULT_INVALID_PORT;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  if (!this->have_format)
    return SPA_RESULT_NO_FORMAT;

  return SPA_RESULT_NOT_IMPLEMENTED;
}

static SpaResult
spa_alsa_sink_node_port_set_input (SpaNode      *node,
                                   uint32_t      port_id,
                                   SpaPortInput *input)
{
  SpaALSASink *this;

  if (node == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  if (!CHECK_PORT (this, SPA_DIRECTION_INPUT, port_id))
    return SPA_RESULT_INVALID_PORT;

  this->io = input;

  return SPA_RESULT_OK;
}

static SpaResult
spa_alsa_sink_node_port_set_output (SpaNode       *node,
                                    uint32_t       port_id,
                                    SpaPortOutput *output)
{
  return SPA_RESULT_NOT_IMPLEMENTED;
}

static SpaResult
spa_alsa_sink_node_port_reuse_buffer (SpaNode         *node,
                                      uint32_t         port_id,
                                      uint32_t         buffer_id)
{
  return SPA_RESULT_NOT_IMPLEMENTED;
}

static SpaResult
spa_alsa_sink_node_port_send_command (SpaNode          *node,
                                      SpaDirection      direction,
                                      uint32_t          port_id,
                                      SpaCommand       *command)
{
  SpaALSASink *this;
  SpaResult res;

  if (node == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  if (port_id != 0)
    return SPA_RESULT_INVALID_PORT;

  if (SPA_COMMAND_TYPE (command) == this->type.command_node.Pause) {
    if (SPA_RESULT_IS_OK (res = spa_alsa_pause (this, false))) {
      update_state (this, SPA_NODE_STATE_PAUSED);
    }
  }
  else if (SPA_COMMAND_TYPE (command) == this->type.command_node.Start) {
    if (SPA_RESULT_IS_OK (res = spa_alsa_start (this, false))) {
      update_state (this, SPA_NODE_STATE_STREAMING);
    }
  }
  else
    res = SPA_RESULT_NOT_IMPLEMENTED;

  return res;
}

static SpaResult
spa_alsa_sink_node_process_input (SpaNode *node)
{
  SpaALSASink *this;
  SpaPortInput *input;
  SpaALSABuffer *b;

  if (node == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = SPA_CONTAINER_OF (node, SpaALSASink, node);

  if ((input = this->io) == NULL)
    return SPA_RESULT_OK;

  if (!this->have_format) {
    input->status = SPA_RESULT_NO_FORMAT;
    return SPA_RESULT_ERROR;
  }

  if (input->buffer_id >= this->n_buffers) {
    input->status = SPA_RESULT_INVALID_BUFFER_ID;
    return SPA_RESULT_ERROR;
  }

  b = &this->buffers[input->buffer_id];
  if (!b->outstanding) {
    input->buffer_id = SPA_ID_INVALID;
    input->status = SPA_RESULT_INVALID_BUFFER_ID;
    return SPA_RESULT_ERROR;
  }
  if (this->ringbuffer) {
    this->ringbuffer->outstanding = true;
    this->ringbuffer = b;
  } else {
    spa_list_insert (this->ready.prev, &b->link);
  }
  //spa_log_debug (this->log, "alsa-source: got buffer %u", input->buffer_id);
  b->outstanding = false;
  input->buffer_id = SPA_ID_INVALID;
  input->status = SPA_RESULT_OK;

  return SPA_RESULT_OK;
}

static SpaResult
spa_alsa_sink_node_process_output (SpaNode *node)
{
  return SPA_RESULT_INVALID_PORT;
}


static const SpaNode alsasink_node = {
  sizeof (SpaNode),
  NULL,
  SPA_NODE_STATE_INIT,
  spa_alsa_sink_node_get_props,
  spa_alsa_sink_node_set_props,
  spa_alsa_sink_node_send_command,
  spa_alsa_sink_node_set_event_callback,
  spa_alsa_sink_node_get_n_ports,
  spa_alsa_sink_node_get_port_ids,
  spa_alsa_sink_node_add_port,
  spa_alsa_sink_node_remove_port,
  spa_alsa_sink_node_port_enum_formats,
  spa_alsa_sink_node_port_set_format,
  spa_alsa_sink_node_port_get_format,
  spa_alsa_sink_node_port_get_info,
  spa_alsa_sink_node_port_get_props,
  spa_alsa_sink_node_port_set_props,
  spa_alsa_sink_node_port_use_buffers,
  spa_alsa_sink_node_port_alloc_buffers,
  spa_alsa_sink_node_port_set_input,
  spa_alsa_sink_node_port_set_output,
  spa_alsa_sink_node_port_reuse_buffer,
  spa_alsa_sink_node_port_send_command,
  spa_alsa_sink_node_process_input,
  spa_alsa_sink_node_process_output,
};

static SpaResult
spa_alsa_sink_get_interface (SpaHandle               *handle,
                             uint32_t                 interface_id,
                             void                   **interface)
{
  SpaALSASink *this;

  if (handle == NULL || interface == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  this = (SpaALSASink *) handle;

  if (interface_id == this->type.node)
    *interface = &this->node;
  else
    return SPA_RESULT_UNKNOWN_INTERFACE;

  return SPA_RESULT_OK;
}

static SpaResult
alsa_sink_clear (SpaHandle *handle)
{
  return SPA_RESULT_OK;
}

static SpaResult
alsa_sink_init (const SpaHandleFactory  *factory,
                SpaHandle               *handle,
                const SpaDict           *info,
                const SpaSupport        *support,
                uint32_t                 n_support)
{
  SpaALSASink *this;
  uint32_t i;

  if (factory == NULL || handle == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  handle->get_interface = spa_alsa_sink_get_interface;
  handle->clear = alsa_sink_clear;

  this = (SpaALSASink *) handle;

  for (i = 0; i < n_support; i++) {
    if (strcmp (support[i].type, SPA_TYPE__TypeMap) == 0)
      this->map = support[i].data;
    else if (strcmp (support[i].type, SPA_TYPE__Log) == 0)
      this->log = support[i].data;
    else if (strcmp (support[i].type, SPA_TYPE_LOOP__DataLoop) == 0)
      this->data_loop = support[i].data;
    else if (strcmp (support[i].type, SPA_TYPE_LOOP__MainLoop) == 0)
      this->main_loop = support[i].data;
  }
  if (this->map == NULL) {
    spa_log_error (this->log, "an id-map is needed");
    return SPA_RESULT_ERROR;
  }
  init_type (&this->type, this->map);

  this->node = alsasink_node;
  this->stream = SND_PCM_STREAM_PLAYBACK;
  reset_alsa_sink_props (&this->props);

  spa_list_init (&this->ready);

  for (i = 0; info && i < info->n_items; i++) {
    if (!strcmp (info->items[i].key, "alsa.card")) {
      snprintf (this->props.device, 63, "hw:%s", info->items[i].value);
    }
  }

  update_state (this, SPA_NODE_STATE_CONFIGURE);

  return SPA_RESULT_OK;
}

static const SpaInterfaceInfo alsa_sink_interfaces[] =
{
  { SPA_TYPE__Node, },
};

static SpaResult
alsa_sink_enum_interface_info (const SpaHandleFactory  *factory,
                               const SpaInterfaceInfo **info,
                               uint32_t                 index)
{
  if (factory == NULL || info == NULL)
    return SPA_RESULT_INVALID_ARGUMENTS;

  switch (index) {
    case 0:
      *info = &alsa_sink_interfaces[index];
      break;
    default:
      return SPA_RESULT_ENUM_END;
  }
  return SPA_RESULT_OK;
}

const SpaHandleFactory spa_alsa_sink_factory =
{ "alsa-sink",
  NULL,
  sizeof (SpaALSASink),
  alsa_sink_init,
  alsa_sink_enum_interface_info,
};
