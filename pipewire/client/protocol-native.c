/* PipeWire
 * Copyright (C) 2017 Wim Taymans <wim.taymans@gmail.com>
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

#include <errno.h>

#include "spa/pod-iter.h"
#include "pipewire/client/pipewire.h"

#include "pipewire/client/protocol-native.h"
#include "pipewire/client/interfaces.h"
#include "pipewire/client/connection.h"

struct builder {
  SpaPODBuilder b;
  struct pw_connection *connection;
};

typedef bool (*demarshal_func_t) (void *object, void *data, size_t size);

static uint32_t
write_pod (SpaPODBuilder *b, uint32_t ref, const void *data, uint32_t size)
{
  if (ref == -1)
    ref = b->offset;

  if (b->size <= b->offset) {
    b->size = SPA_ROUND_UP_N (b->offset + size, 4096);
    b->data = pw_connection_begin_write (((struct builder*)b)->connection, b->size);
  }
  memcpy (b->data + ref, data, size);
  return ref;
}

static void
core_update_map (struct pw_context *context)
{
  uint32_t diff, base, i;
  const char **types;

  base = context->n_types;
  diff = spa_type_map_get_size (context->type.map) - base;
  if (diff == 0)
    return;

  types = alloca (diff * sizeof (char *));
  for (i = 0; i < diff; i++, base++)
    types[i] = spa_type_map_get_type (context->type.map, base);

  pw_core_do_update_types (context->core_proxy,
                              context->n_types,
                              diff,
                              types);
  context->n_types += diff;
}

static void
core_marshal_client_update (void          *object,
                            const SpaDict *props)
{
  struct pw_proxy *proxy = object;
  struct pw_connection *connection = proxy->context->protocol_private;
  struct builder b = { { NULL, 0, 0, NULL, write_pod }, connection };
  SpaPODFrame f;
  int i, n_items;

  if (connection == NULL)
    return;

  core_update_map (proxy->context);

  n_items = props ? props->n_items : 0;

  spa_pod_builder_add (&b.b,
      SPA_POD_TYPE_STRUCT, &f,
        SPA_POD_TYPE_INT, n_items, 0);

  for (i = 0; i < n_items; i++) {
    spa_pod_builder_add (&b.b,
        SPA_POD_TYPE_STRING, props->items[i].key,
        SPA_POD_TYPE_STRING, props->items[i].value,
        0);
  }
  spa_pod_builder_add (&b.b, -SPA_POD_TYPE_STRUCT, &f, 0);

  pw_connection_end_write (connection, proxy->id, PW_CORE_METHOD_CLIENT_UPDATE, b.b.offset);
}

static void
core_marshal_sync (void     *object,
                   uint32_t  seq)
{
  struct pw_proxy *proxy = object;
  struct pw_connection *connection = proxy->context->protocol_private;
  struct builder b = { { NULL, 0, 0, NULL, write_pod }, connection };
  SpaPODFrame f;

  if (connection == NULL)
    return;

  core_update_map (proxy->context);

  spa_pod_builder_struct (&b.b, &f,
      SPA_POD_TYPE_INT, seq);

  pw_connection_end_write (connection, proxy->id, PW_CORE_METHOD_SYNC, b.b.offset);
}

static void
core_marshal_get_registry (void     *object,
                           uint32_t  new_id)
{
  struct pw_proxy *proxy = object;
  struct pw_connection *connection = proxy->context->protocol_private;
  struct builder b = { { NULL, 0, 0, NULL, write_pod }, connection };
  SpaPODFrame f;

  if (connection == NULL)
    return;

  core_update_map (proxy->context);

  spa_pod_builder_struct (&b.b, &f,
        SPA_POD_TYPE_INT, new_id);

  pw_connection_end_write (connection, proxy->id, PW_CORE_METHOD_GET_REGISTRY, b.b.offset);
}

static void
core_marshal_create_node (void          *object,
                          const char    *factory_name,
                          const char    *name,
                          const SpaDict *props,
                          uint32_t       new_id)
{
  struct pw_proxy *proxy = object;
  struct pw_connection *connection = proxy->context->protocol_private;
  struct builder b = { { NULL, 0, 0, NULL, write_pod }, connection };
  SpaPODFrame f;
  uint32_t i, n_items;

  if (connection == NULL)
    return;

  core_update_map (proxy->context);

  n_items = props ? props->n_items : 0;

  spa_pod_builder_add (&b.b,
      SPA_POD_TYPE_STRUCT, &f,
        SPA_POD_TYPE_STRING, factory_name,
        SPA_POD_TYPE_STRING, name,
        SPA_POD_TYPE_INT, n_items, 0);

  for (i = 0; i < n_items; i++) {
    spa_pod_builder_add (&b.b,
        SPA_POD_TYPE_STRING, props->items[i].key,
        SPA_POD_TYPE_STRING, props->items[i].value,
        0);
  }
  spa_pod_builder_add (&b.b,
      SPA_POD_TYPE_INT, new_id,
    -SPA_POD_TYPE_STRUCT, &f, 0);

  pw_connection_end_write (connection, proxy->id, PW_CORE_METHOD_CREATE_NODE, b.b.offset);
}

static void
core_marshal_create_client_node (void          *object,
                                 const char    *name,
                                 const SpaDict *props,
                                 uint32_t       new_id)
{
  struct pw_proxy *proxy = object;
  struct pw_connection *connection = proxy->context->protocol_private;
  struct builder b = { { NULL, 0, 0, NULL, write_pod }, connection };
  SpaPODFrame f;
  uint32_t i, n_items;

  if (connection == NULL)
    return;

  core_update_map (proxy->context);

  n_items = props ? props->n_items : 0;

  spa_pod_builder_add (&b.b,
      SPA_POD_TYPE_STRUCT, &f,
        SPA_POD_TYPE_STRING, name,
        SPA_POD_TYPE_INT, n_items, 0);

  for (i = 0; i < n_items; i++) {
    spa_pod_builder_add (&b.b,
        SPA_POD_TYPE_STRING, props->items[i].key,
        SPA_POD_TYPE_STRING, props->items[i].value,
        0);
  }
  spa_pod_builder_add (&b.b,
      SPA_POD_TYPE_INT, new_id,
    -SPA_POD_TYPE_STRUCT, &f, 0);

  pw_connection_end_write (connection, proxy->id, PW_CORE_METHOD_CREATE_CLIENT_NODE, b.b.offset);
}

static void
core_marshal_update_types (void          *object,
                           uint32_t       first_id,
                           uint32_t       n_types,
                           const char   **types)
{
  struct pw_proxy *proxy = object;
  struct pw_connection *connection = proxy->context->protocol_private;
  struct builder b = { { NULL, 0, 0, NULL, write_pod }, connection };
  SpaPODFrame f;
  uint32_t i;

  if (connection == NULL)
    return;

  spa_pod_builder_add (&b.b,
      SPA_POD_TYPE_STRUCT, &f,
        SPA_POD_TYPE_INT, first_id,
        SPA_POD_TYPE_INT, n_types, 0);

  for (i = 0; i < n_types; i++) {
    spa_pod_builder_add (&b.b,
        SPA_POD_TYPE_STRING, types[i], 0);
  }
  spa_pod_builder_add (&b.b,
    -SPA_POD_TYPE_STRUCT, &f, 0);

  pw_connection_end_write (connection, proxy->id, PW_CORE_METHOD_UPDATE_TYPES, b.b.offset);
}

static bool
core_demarshal_info (void   *object,
                     void   *data,
                     size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaDict props;
  struct pw_core_info info;
  SpaPODIter it;
  int i;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &info.id,
        SPA_POD_TYPE_LONG, &info.change_mask,
        SPA_POD_TYPE_STRING, &info.user_name,
        SPA_POD_TYPE_STRING, &info.host_name,
        SPA_POD_TYPE_STRING, &info.version,
        SPA_POD_TYPE_STRING, &info.name,
        SPA_POD_TYPE_INT, &info.cookie,
        SPA_POD_TYPE_INT, &props.n_items,
        0))
    return false;

  info.props = &props;
  props.items = alloca (props.n_items * sizeof (SpaDictItem));
  for (i = 0; i < props.n_items; i++) {
    if (!spa_pod_iter_get (&it,
          SPA_POD_TYPE_STRING, &props.items[i].key,
          SPA_POD_TYPE_STRING, &props.items[i].value,
          0))
      return false;
  }
  ((struct pw_core_events*)proxy->implementation)->info (proxy, &info);
  return true;
}

static bool
core_demarshal_done (void   *object,
                     void   *data,
                     size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  uint32_t seq;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &seq,
        0))
    return false;

  ((struct pw_core_events*)proxy->implementation)->done (proxy, seq);
  return true;
}

static bool
core_demarshal_error (void   *object,
                      void   *data,
                      size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  uint32_t id, res;
  const char *error;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &id,
        SPA_POD_TYPE_INT, &res,
        SPA_POD_TYPE_STRING, &error,
        0))
    return false;

  ((struct pw_core_events*)proxy->implementation)->error (proxy, id, res, error);
  return true;
}

static bool
core_demarshal_remove_id (void   *object,
                          void   *data,
                          size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  uint32_t id;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &id,
        0))
    return false;

  ((struct pw_core_events*)proxy->implementation)->remove_id (proxy, id);
  return true;
}

static bool
core_demarshal_update_types (void   *object,
                             void   *data,
                             size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  uint32_t first_id, n_types;
  const char **types;
  int i;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &first_id,
        SPA_POD_TYPE_INT, &n_types,
        0))
    return false;

  types = alloca (n_types * sizeof (char *));
  for (i = 0; i < n_types; i++) {
    if (!spa_pod_iter_get (&it, SPA_POD_TYPE_STRING, &types[i], 0))
      return false;
  }
  ((struct pw_core_events*)proxy->implementation)->update_types (proxy, first_id, n_types, types);
  return true;
}

static bool
module_demarshal_info (void   *object,
                       void   *data,
                       size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  SpaDict props;
  struct pw_module_info info;
  int i;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &info.id,
        SPA_POD_TYPE_LONG, &info.change_mask,
        SPA_POD_TYPE_STRING, &info.name,
        SPA_POD_TYPE_STRING, &info.filename,
        SPA_POD_TYPE_STRING, &info.args,
        SPA_POD_TYPE_INT, &props.n_items,
        0))
    return false;

  info.props = &props;
  props.items = alloca (props.n_items * sizeof (SpaDictItem));
  for (i = 0; i < props.n_items; i++) {
    if (!spa_pod_iter_get (&it,
          SPA_POD_TYPE_STRING, &props.items[i].key,
          SPA_POD_TYPE_STRING, &props.items[i].value,
          0))
      return false;
  }
  ((struct pw_module_events*)proxy->implementation)->info (proxy, &info);
  return true;
}

static bool
node_demarshal_info (void   *object,
                     void   *data,
                     size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  SpaDict props;
  struct pw_node_info info;
  int i;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !pw_pod_remap_data (SPA_POD_TYPE_STRUCT, data, size, &proxy->context->types) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &info.id,
        SPA_POD_TYPE_LONG, &info.change_mask,
        SPA_POD_TYPE_STRING, &info.name,
        SPA_POD_TYPE_INT, &info.max_inputs,
        SPA_POD_TYPE_INT, &info.n_inputs,
        SPA_POD_TYPE_INT, &info.n_input_formats,
        0))
    return false;

  info.input_formats = alloca (info.n_input_formats * sizeof (SpaFormat*));
  for (i = 0; i < info.n_input_formats; i++)
    if (!spa_pod_iter_get (&it, SPA_POD_TYPE_OBJECT, &info.input_formats[i], 0))
      return false;

  if (!spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &info.max_outputs,
        SPA_POD_TYPE_INT, &info.n_outputs,
        SPA_POD_TYPE_INT, &info.n_output_formats,
        0))
    return false;

  info.output_formats = alloca (info.n_output_formats * sizeof (SpaFormat*));
  for (i = 0; i < info.n_output_formats; i++)
    if (!spa_pod_iter_get (&it, SPA_POD_TYPE_OBJECT, &info.output_formats[i], 0))
      return false;

  if (!spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &info.state,
        SPA_POD_TYPE_STRING, &info.error,
        SPA_POD_TYPE_INT, &props.n_items,
        0))
    return false;

  info.props = &props;
  props.items = alloca (props.n_items * sizeof (SpaDictItem));
  for (i = 0; i < props.n_items; i++) {
    if (!spa_pod_iter_get (&it,
          SPA_POD_TYPE_STRING, &props.items[i].key,
          SPA_POD_TYPE_STRING, &props.items[i].value,
          0))
      return false;
  }
  ((struct pw_node_events*)proxy->implementation)->info (proxy, &info);
  return true;
}

static void
client_node_marshal_update (void           *object,
                            uint32_t        change_mask,
                            uint32_t        max_input_ports,
                            uint32_t        max_output_ports,
                            const SpaProps *props)
{
  struct pw_proxy *proxy = object;
  struct pw_connection *connection = proxy->context->protocol_private;
  struct builder b = { { NULL, 0, 0, NULL, write_pod }, connection };
  SpaPODFrame f;

  if (connection == NULL)
    return;

  core_update_map (proxy->context);

  spa_pod_builder_struct (&b.b, &f,
        SPA_POD_TYPE_INT, change_mask,
        SPA_POD_TYPE_INT, max_input_ports,
        SPA_POD_TYPE_INT, max_output_ports,
        SPA_POD_TYPE_POD, props);

  pw_connection_end_write (connection, proxy->id, PW_CLIENT_NODE_METHOD_UPDATE, b.b.offset);
}

static void
client_node_marshal_port_update (void              *object,
                                 SpaDirection       direction,
                                 uint32_t           port_id,
                                 uint32_t           change_mask,
                                 uint32_t           n_possible_formats,
                                 const SpaFormat  **possible_formats,
                                 const SpaFormat   *format,
                                 uint32_t           n_params,
                                 const SpaParam   **params,
                                 const SpaPortInfo *info)
{
  struct pw_proxy *proxy = object;
  struct pw_connection *connection = proxy->context->protocol_private;
  struct builder b = { { NULL, 0, 0, NULL, write_pod }, connection };
  SpaPODFrame f[2];
  int i;

  if (connection == NULL)
    return;

  core_update_map (proxy->context);

  spa_pod_builder_add (&b.b,
      SPA_POD_TYPE_STRUCT, &f[0],
        SPA_POD_TYPE_INT, direction,
        SPA_POD_TYPE_INT, port_id,
        SPA_POD_TYPE_INT, change_mask,
        SPA_POD_TYPE_INT, n_possible_formats,
        0);

  for (i = 0; i < n_possible_formats; i++)
    spa_pod_builder_add (&b.b, SPA_POD_TYPE_POD, possible_formats[i], 0);

  spa_pod_builder_add (&b.b,
      SPA_POD_TYPE_POD, format,
      SPA_POD_TYPE_INT, n_params,
      0);

  for (i = 0; i < n_params; i++) {
    const SpaParam *p = params[i];
    spa_pod_builder_add (&b.b, SPA_POD_TYPE_POD, p, 0);
  }

  if (info) {
    spa_pod_builder_add (&b.b,
      SPA_POD_TYPE_STRUCT, &f[1],
        SPA_POD_TYPE_INT, info->flags,
        SPA_POD_TYPE_INT, info->rate,
        0);
    spa_pod_builder_add (&b.b, -SPA_POD_TYPE_STRUCT, &f[1], 0);
  } else {
    spa_pod_builder_add (&b.b, SPA_POD_TYPE_POD, NULL, 0);
  }
  spa_pod_builder_add (&b.b, -SPA_POD_TYPE_STRUCT, &f[0], 0);

  pw_connection_end_write (connection, proxy->id, PW_CLIENT_NODE_METHOD_PORT_UPDATE, b.b.offset);
}

static void
client_node_marshal_event (void     *object,
                           SpaEvent *event)
{
  struct pw_proxy *proxy = object;
  struct pw_connection *connection = proxy->context->protocol_private;
  struct builder b = { { NULL, 0, 0, NULL, write_pod }, connection };
  SpaPODFrame f;

  if (connection == NULL)
    return;

  core_update_map (proxy->context);

  spa_pod_builder_struct (&b.b, &f,
        SPA_POD_TYPE_POD, event);

  pw_connection_end_write (connection, proxy->id, PW_CLIENT_NODE_METHOD_EVENT, b.b.offset);
}

static void
client_node_marshal_destroy (void    *object)
{
  struct pw_proxy *proxy = object;
  struct pw_connection *connection = proxy->context->protocol_private;
  struct builder b = { { NULL, 0, 0, NULL, write_pod }, connection };
  SpaPODFrame f;

  if (connection == NULL)
    return;

  core_update_map (proxy->context);

  spa_pod_builder_struct (&b.b, &f, 0);

  pw_connection_end_write (connection, proxy->id, PW_CLIENT_NODE_METHOD_DESTROY, b.b.offset);
}

static bool
client_node_demarshal_done (void   *object,
                            void   *data,
                            size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  struct pw_connection *connection = proxy->context->protocol_private;
  int32_t ridx, widx;
  int readfd, writefd;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &ridx,
        SPA_POD_TYPE_INT, &widx,
        0))
    return false;

  readfd = pw_connection_get_fd (connection, ridx);
  writefd = pw_connection_get_fd (connection, widx);
  if (readfd == -1 || writefd == -1)
    return false;

  ((struct pw_client_node_events*)proxy->implementation)->done (proxy, readfd, writefd);
  return true;
}

static bool
client_node_demarshal_event (void   *object,
                             void   *data,
                             size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  const SpaEvent *event;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !pw_pod_remap_data (SPA_POD_TYPE_STRUCT, data, size, &proxy->context->types) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_OBJECT, &event,
        0))
    return false;

  ((struct pw_client_node_events*)proxy->implementation)->event (proxy, event);
  return true;
}

static bool
client_node_demarshal_add_port (void   *object,
                                void   *data,
                                size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  int32_t seq, direction, port_id;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &seq,
        SPA_POD_TYPE_INT, &direction,
        SPA_POD_TYPE_INT, &port_id,
        0))
    return false;

  ((struct pw_client_node_events*)proxy->implementation)->add_port (proxy, seq, direction, port_id);
  return true;
}

static bool
client_node_demarshal_remove_port (void   *object,
                                   void   *data,
                                   size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  int32_t seq, direction, port_id;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &seq,
        SPA_POD_TYPE_INT, &direction,
        SPA_POD_TYPE_INT, &port_id,
        0))
    return false;

  ((struct pw_client_node_events*)proxy->implementation)->remove_port (proxy, seq, direction, port_id);
  return true;
}

static bool
client_node_demarshal_set_format (void   *object,
                                  void   *data,
                                  size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  uint32_t seq, direction, port_id, flags;
  const SpaFormat *format = NULL;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !pw_pod_remap_data (SPA_POD_TYPE_STRUCT, data, size, &proxy->context->types) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &seq,
        SPA_POD_TYPE_INT, &direction,
        SPA_POD_TYPE_INT, &port_id,
        SPA_POD_TYPE_INT, &flags,
        -SPA_POD_TYPE_OBJECT, &format,
        0))
    return false;

  ((struct pw_client_node_events*)proxy->implementation)->set_format (proxy, seq, direction, port_id,
                                       flags, format);
  return true;
}

static bool
client_node_demarshal_set_property (void   *object,
                                    void   *data,
                                    size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  uint32_t seq, id;
  const void *value;
  uint32_t s;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &seq,
        SPA_POD_TYPE_INT, &id,
        SPA_POD_TYPE_BYTES, &value, &s,
        0))
    return false;

  ((struct pw_client_node_events*)proxy->implementation)->set_property (proxy, seq, id, s, value);
  return true;
}

static bool
client_node_demarshal_add_mem (void   *object,
                               void   *data,
                               size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  struct pw_connection *connection = proxy->context->protocol_private;
  uint32_t direction, port_id, mem_id, type, memfd_idx, flags, offset, sz;
  int memfd;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !pw_pod_remap_data (SPA_POD_TYPE_STRUCT, data, size, &proxy->context->types) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &direction,
        SPA_POD_TYPE_INT, &port_id,
        SPA_POD_TYPE_INT, &mem_id,
        SPA_POD_TYPE_ID, &type,
        SPA_POD_TYPE_INT, &memfd_idx,
        SPA_POD_TYPE_INT, &flags,
        SPA_POD_TYPE_INT, &offset,
        SPA_POD_TYPE_INT, &sz,
        0))
    return false;

  memfd = pw_connection_get_fd (connection, memfd_idx);

  ((struct pw_client_node_events*)proxy->implementation)->add_mem (proxy,
                                                            direction,
                                                            port_id,
                                                            mem_id,
                                                            type,
                                                            memfd,
                                                            flags,
                                                            offset,
                                                            sz);
  return true;
}

static bool
client_node_demarshal_use_buffers (void   *object,
                                   void   *data,
                                   size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  uint32_t seq, direction, port_id, n_buffers, data_id;
  struct pw_client_node_buffer *buffers;
  int i, j;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !pw_pod_remap_data (SPA_POD_TYPE_STRUCT, data, size, &proxy->context->types) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &seq,
        SPA_POD_TYPE_INT, &direction,
        SPA_POD_TYPE_INT, &port_id,
        SPA_POD_TYPE_INT, &n_buffers,
        0))
    return false;

  buffers = alloca (sizeof (struct pw_client_node_buffer) * n_buffers);
  for (i = 0; i < n_buffers; i++) {
    SpaBuffer *buf = buffers[i].buffer = alloca (sizeof (SpaBuffer));

    if (!spa_pod_iter_get (&it,
          SPA_POD_TYPE_INT, &buffers[i].mem_id,
          SPA_POD_TYPE_INT, &buffers[i].offset,
          SPA_POD_TYPE_INT, &buffers[i].size,
          SPA_POD_TYPE_INT, &buf->id,
          SPA_POD_TYPE_INT, &buf->n_metas, 0))
      return false;

    buf->metas = alloca (sizeof (SpaMeta) * buf->n_metas);
    for (j = 0; j < buf->n_metas; j++) {
      SpaMeta *m = &buf->metas[j];

      if (!spa_pod_iter_get (&it,
            SPA_POD_TYPE_ID, &m->type,
            SPA_POD_TYPE_INT, &m->size, 0))
        return false;
    }
    if (!spa_pod_iter_get (&it, SPA_POD_TYPE_INT, &buf->n_datas, 0))
      return false;

    buf->datas = alloca (sizeof (SpaData) * buf->n_datas);
    for (j = 0; j < buf->n_datas; j++) {
      SpaData *d = &buf->datas[j];

      if (!spa_pod_iter_get (&it,
            SPA_POD_TYPE_ID, &d->type,
            SPA_POD_TYPE_INT, &data_id,
            SPA_POD_TYPE_INT, &d->flags,
            SPA_POD_TYPE_INT, &d->mapoffset,
            SPA_POD_TYPE_INT, &d->maxsize,
            0))
        return false;

      d->data = SPA_UINT32_TO_PTR (data_id);
    }
  }
  ((struct pw_client_node_events*)proxy->implementation)->use_buffers (proxy,
                                                                seq,
                                                                direction,
                                                                port_id,
                                                                n_buffers,
                                                                buffers);
  return true;
}

static bool
client_node_demarshal_node_command (void   *object,
                                    void   *data,
                                    size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  const SpaCommand *command;
  uint32_t seq;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !pw_pod_remap_data (SPA_POD_TYPE_STRUCT, data, size, &proxy->context->types) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &seq,
        SPA_POD_TYPE_OBJECT, &command,
        0))
    return false;

  ((struct pw_client_node_events*)proxy->implementation)->node_command (proxy, seq, command);
  return true;
}

static bool
client_node_demarshal_port_command (void   *object,
                                    void   *data,
                                    size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  const SpaCommand *command;
  uint32_t port_id;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !pw_pod_remap_data (SPA_POD_TYPE_STRUCT, data, size, &proxy->context->types) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &port_id,
        SPA_POD_TYPE_OBJECT, &command,
        0))
    return false;

  ((struct pw_client_node_events*)proxy->implementation)->port_command (proxy, port_id, command);
  return true;
}

static bool
client_node_demarshal_transport (void   *object,
                                 void   *data,
                                 size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  struct pw_connection *connection = proxy->context->protocol_private;
  uint32_t memfd_idx, offset, sz;
  int memfd;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &memfd_idx,
        SPA_POD_TYPE_INT, &offset,
        SPA_POD_TYPE_INT, &sz,
        0))
    return false;

  memfd = pw_connection_get_fd (connection, memfd_idx);
  ((struct pw_client_node_events*)proxy->implementation)->transport (proxy, memfd, offset, sz);
  return true;
}

static bool
client_demarshal_info (void   *object,
                       void   *data,
                       size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  SpaDict props;
  struct pw_client_info info;
  uint32_t i;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &info.id,
        SPA_POD_TYPE_LONG, &info.change_mask,
        SPA_POD_TYPE_INT, &props.n_items,
        0))
    return false;

  info.props = &props;
  props.items = alloca (props.n_items * sizeof (SpaDictItem));
  for (i = 0; i < props.n_items; i++) {
    if (!spa_pod_iter_get (&it,
          SPA_POD_TYPE_STRING, &props.items[i].key,
          SPA_POD_TYPE_STRING, &props.items[i].value,
          0))
      return false;
  }
  ((struct pw_client_events*)proxy->implementation)->info (proxy, &info);
  return true;
}

static bool
link_demarshal_info (void   *object,
                     void   *data,
                     size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  struct pw_link_info info;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &info.id,
        SPA_POD_TYPE_LONG, &info.change_mask,
        SPA_POD_TYPE_INT, &info.output_node_id,
        SPA_POD_TYPE_INT, &info.output_port_id,
        SPA_POD_TYPE_INT, &info.input_node_id,
        SPA_POD_TYPE_INT, &info.input_port_id,
        0))
    return false;

  ((struct pw_link_events*)proxy->implementation)->info (proxy, &info);
  return true;
}

static bool
registry_demarshal_global (void   *object,
                           void   *data,
                           size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  uint32_t id;
  const char *type;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &id,
        SPA_POD_TYPE_STRING, &type,
        0))
    return false;

  ((struct pw_registry_events*)proxy->implementation)->global (proxy, id, type);
  return true;
}

static bool
registry_demarshal_global_remove (void   *object,
                                  void   *data,
                                  size_t  size)
{
  struct pw_proxy *proxy = object;
  SpaPODIter it;
  uint32_t id;

  if (!spa_pod_iter_struct (&it, data, size) ||
      !spa_pod_iter_get (&it,
        SPA_POD_TYPE_INT, &id,
        0))
    return false;

  ((struct pw_registry_events*)proxy->implementation)->global_remove (proxy, id);
  return true;
}

static void
registry_marshal_bind (void          *object,
                       uint32_t       id,
                       uint32_t       new_id)
{
  struct pw_proxy *proxy = object;
  struct pw_connection *connection = proxy->context->protocol_private;
  struct builder b = { { NULL, 0, 0, NULL, write_pod }, connection };
  SpaPODFrame f;

  if (connection == NULL)
    return;

  core_update_map (proxy->context);

  spa_pod_builder_struct (&b.b, &f,
        SPA_POD_TYPE_INT, id,
        SPA_POD_TYPE_INT, new_id);

  pw_connection_end_write (connection, proxy->id, PW_REGISTRY_METHOD_BIND, b.b.offset);
}

static const struct pw_core_methods pw_protocol_native_client_core_methods = {
  &core_marshal_client_update,
  &core_marshal_sync,
  &core_marshal_get_registry,
  &core_marshal_create_node,
  &core_marshal_create_client_node,
  &core_marshal_update_types,
};

static const demarshal_func_t pw_protocol_native_client_core_demarshal[] = {
  &core_demarshal_info,
  &core_demarshal_done,
  &core_demarshal_error,
  &core_demarshal_remove_id,
  &core_demarshal_update_types,
};

static const struct pw_interface pw_protocol_native_client_core_interface = {
  PW_CORE_METHOD_NUM, &pw_protocol_native_client_core_methods,
  PW_CORE_EVENT_NUM, pw_protocol_native_client_core_demarshal
};

static const struct pw_registry_methods pw_protocol_native_client_registry_methods = {
  &registry_marshal_bind
};

static const demarshal_func_t pw_protocol_native_client_registry_demarshal[] = {
  &registry_demarshal_global,
  &registry_demarshal_global_remove,
};

static const struct pw_interface pw_protocol_native_client_registry_interface = {
  PW_REGISTRY_METHOD_NUM, &pw_protocol_native_client_registry_methods,
  PW_REGISTRY_EVENT_NUM, pw_protocol_native_client_registry_demarshal,
};

static const struct pw_client_node_methods pw_protocol_native_client_client_node_methods = {
  &client_node_marshal_update,
  &client_node_marshal_port_update,
  &client_node_marshal_event,
  &client_node_marshal_destroy
};

static const demarshal_func_t pw_protocol_native_client_client_node_demarshal[] = {
  &client_node_demarshal_done,
  &client_node_demarshal_event,
  &client_node_demarshal_add_port,
  &client_node_demarshal_remove_port,
  &client_node_demarshal_set_format,
  &client_node_demarshal_set_property,
  &client_node_demarshal_add_mem,
  &client_node_demarshal_use_buffers,
  &client_node_demarshal_node_command,
  &client_node_demarshal_port_command,
  &client_node_demarshal_transport
};

static const struct pw_interface pw_protocol_native_client_client_node_interface = {
  PW_CLIENT_NODE_METHOD_NUM, &pw_protocol_native_client_client_node_methods,
  PW_CLIENT_NODE_EVENT_NUM, pw_protocol_native_client_client_node_demarshal,
};

static const demarshal_func_t pw_protocol_native_client_module_demarshal[] = {
  &module_demarshal_info,
};

static const struct pw_interface pw_protocol_native_client_module_interface = {
  0, NULL,
  PW_MODULE_EVENT_NUM, pw_protocol_native_client_module_demarshal,
};

static const demarshal_func_t pw_protocol_native_client_node_demarshal[] = {
  &node_demarshal_info,
};

static const struct pw_interface pw_protocol_native_client_node_interface = {
  0, NULL,
  PW_NODE_EVENT_NUM, pw_protocol_native_client_node_demarshal,
};

static const demarshal_func_t pw_protocol_native_client_client_demarshal[] = {
  &client_demarshal_info,
};

static const struct pw_interface pw_protocol_native_client_client_interface = {
  0, NULL,
  PW_CLIENT_EVENT_NUM, pw_protocol_native_client_client_demarshal,
};

static const demarshal_func_t pw_protocol_native_client_link_demarshal[] = {
  &link_demarshal_info,
};

static const struct pw_interface pw_protocol_native_client_link_interface = {
  0, NULL,
  PW_LINK_EVENT_NUM, pw_protocol_native_client_link_demarshal,
};

bool
pw_protocol_native_client_setup (struct pw_proxy *proxy)
{
  const struct pw_interface *iface;

  if (proxy->type == proxy->context->type.core) {
    iface = &pw_protocol_native_client_core_interface;
  }
  else if (proxy->type == proxy->context->type.registry) {
    iface = &pw_protocol_native_client_registry_interface;
  }
  else if (proxy->type == proxy->context->type.module) {
    iface = &pw_protocol_native_client_module_interface;
  }
  else if (proxy->type == proxy->context->type.node) {
    iface = &pw_protocol_native_client_node_interface;
  }
  else if (proxy->type == proxy->context->type.client_node) {
    iface = &pw_protocol_native_client_client_node_interface;
  }
  else if (proxy->type == proxy->context->type.client) {
    iface = &pw_protocol_native_client_client_interface;
  }
  else if (proxy->type == proxy->context->type.link) {
    iface = &pw_protocol_native_client_link_interface;
  } else
    return false;
  proxy->iface = iface;
  return true;
}