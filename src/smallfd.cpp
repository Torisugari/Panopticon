/****************************************************************************
 * The Golden Panopticon Project                                            *
 *                                                                          +
 *                    Great Firedaemon (ver. 2.0)                           *
 *                                                                          *
 *                                                                          *
 ****************************************************************************/

/* 
 * This is proprietary software. Do not redistribute, modify, build, nor run it
 * without author's permission.
 */

/*
 * DBUS version of Great Firedaemon. This is somewhat practical, in terms of
 * memory consumption (roughly around 160KiB).
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include <assert.h>
#include <mcheck.h>

extern "C" {
#include <dbus/dbus.h> 
#include <glib.h>
}

static const char kProductName[]    = "great firedaemon";
static const char kMonitorLogFile[] = "logs/monitor.log";

DBusHandlerResult filter(DBusConnection* aConnection,
                         DBusMessage* aMessage,
                         void* aUserData);

#ifndef NDEBUG
void gfdDumpIter(DBusMessageIter* aIter, int aIndent) {
  // ouch! == aIter
  char* signature = dbus_message_iter_get_signature(aIter);
  int i;

  for (i = 0; i < aIndent; i++)
    printf("  ");

  printf("signature: %s\n", signature);
  dbus_free(signature);

  int type = dbus_message_iter_get_arg_type(aIter);
  while (type != DBUS_TYPE_INVALID) {
    for (i = 0; i < aIndent; i++)
      printf("  ");
    switch (type) {
    case DBUS_TYPE_STRING: {
        const char* data(NULL);
        dbus_message_iter_get_basic(aIter, &data);
        printf("type=str, value=\"%s\";\n", data);
      }
      break;

    case DBUS_TYPE_INT32: {
        dbus_uint32_t data = 0;
        dbus_message_iter_get_basic(aIter, &data);
        printf("type=int, value=%u;\n", data);
      }
      break;

    case DBUS_TYPE_VARIANT: {
        printf("type=var, value= {\n");

        DBusMessageIter iter;
        dbus_message_iter_recurse(aIter, &iter);
        gfdDumpIter(&iter, aIndent + 1);
        for (i = 0; i < aIndent; i++)
          printf("  ");
        printf("};\n");
      }
      break;

    default:
      printf("type=%c\n", char(type));
    }

    dbus_message_iter_next(aIter);
    type = dbus_message_iter_get_arg_type(aIter);
  }
}

void gfdDumpMessage(DBusMessage* aMessage,
                    int aLine = 0, const char* aFile = NULL) {
  const char* type =
    dbus_message_type_to_string(dbus_message_get_type(aMessage));
  dbus_uint32_t serial = dbus_message_get_serial(aMessage);
  const char* sender = dbus_message_get_sender(aMessage);
  const char* destination = dbus_message_get_destination(aMessage);
  const char* path = dbus_message_get_path(aMessage);
  const char* interface = dbus_message_get_interface(aMessage);
  const char* member = dbus_message_get_member(aMessage);
  printf("=== L%d @ %s ===\n"
         "type       : %s\n"
         "serial     : %d\n"
         "destination: %s\n"
         "sender     : %s\n"
         "path       : %s\n"
         "interface  : %s\n"
         "member     : %s\n"
         "---\n",
         aLine, aFile,
         type, serial, destination, sender, path, interface, member);
  DBusMessageIter iter;
  dbus_message_iter_init(aMessage, &iter);
  gfdDumpIter(&iter, 0);
  printf("===\n");
}

void gfdDumpConnection(DBusConnection* aConnection,
                       int aLine = 0, const char* aFile = NULL) {
  DBusError error;
  dbus_error_init(&error);

  const char* status(NULL);

  switch(dbus_connection_get_dispatch_status(aConnection)) {
  case DBUS_DISPATCH_DATA_REMAINS:
    status = "remains";
    break;
  case DBUS_DISPATCH_NEED_MEMORY:
    status = "need memory";
    break;
  case DBUS_DISPATCH_COMPLETE:
    status = "complete";
    break;
  default:
    status = "???";
  }

  int socket(0);
  bool success = dbus_connection_get_socket(aConnection, &socket);
  if (!success)
    socket = 0;

  int fd(0);
  success = dbus_connection_get_unix_fd(aConnection, &fd);
  if (!success)
    fd = 0;

  unsigned long pid(0);
  success = dbus_connection_get_unix_process_id(aConnection, &pid);
  if (!success)
    pid = 0;


  printf(" *** L%d @ %s ***\n"
         " *DBusConnection*: 0x%x\n"
         " *unique name    : %s\n"
         " *connection id  : %s\n"
         " *server id      : %s\n"
         " *dispatch status: %s\n"
         " *socket (fd)    : %d\n"
         " *fd             : %d\n"
         " *pid            : %ld\n"
         " *has messages   : %s\n"
         " *connected      : %s\n"
         " *authenticated  : %s\n"
         " *anonymous      : %s\n"
         " ***\n",
         aLine, aFile, (unsigned int)(aConnection),
         dbus_bus_get_unique_name(aConnection),
         dbus_bus_get_id(aConnection, &error),
         dbus_connection_get_server_id(aConnection),
         status, socket, fd, pid,
         dbus_connection_has_messages_to_send(aConnection)? "yes":" no",
         dbus_connection_get_is_connected(aConnection)?     "yes":" no",
         dbus_connection_get_is_authenticated(aConnection)? "yes":" no",
         dbus_connection_get_is_anonymous(aConnection)?     "yes":" no"
         );
}

#define GFD_DUMP_DBUS_MESSAGE(__DBUSMESSAGE__) \
          (gfdDumpMessage((__DBUSMESSAGE__), (__LINE__), (__FILE__)))

#define GFD_DUMP_DBUS_CONNECTION(__DBUSCONNECTION__) \
          (gfdDumpConnection((__DBUSCONNECTION__), (__LINE__), (__FILE__)))

#else
#define GFD_DUMP_DBUS_MESSAGE(ignore)((void) 0)
#define GFD_DUMP_DBUS_CONNECTION(ignore)((void) 0)
#endif


namespace gfd {
void wakeup(void* aUserData) {
  assert(aUserData);
  g_main_context_wakeup(static_cast<GMainContext*>(aUserData));
}

gboolean handle_watch(GIOChannel* aSource, GIOCondition aCondition,
                      void* aUserData) {
  assert(aUserData);

  DBusWatch* watch = static_cast<DBusWatch*>(aUserData);
  unsigned int flags = dbus_watch_get_flags(watch);

  assert((!!(flags & DBUS_WATCH_READABLE)) == (!!(aCondition & G_IO_IN)));
  assert((!!(flags & DBUS_WATCH_WRITABLE)) == (!!(aCondition & G_IO_OUT)));

  dbus_watch_handle(watch, flags);
  return TRUE;
}

void unref_source(void* aUserData) {
  assert(aUserData);
  GSource* source = static_cast<GSource*>(aUserData);
  g_source_destroy(source);
  g_source_unref(source);
}

dbus_bool_t add_watch(DBusWatch* aWatch, void* aUserData) {
  if (!dbus_watch_get_enabled(aWatch))
    return TRUE;

  void* data = dbus_watch_get_data(aWatch);
  GSource* source(NULL);

  if (data) {
    source = static_cast<GSource*>(data);
  }
  else {
    int fd = dbus_watch_get_unix_fd(aWatch);
    GIOChannel* channel = g_io_channel_unix_new(fd);
    unsigned int condition = (G_IO_ERR | G_IO_HUP);
    unsigned int flags = dbus_watch_get_flags(aWatch);

    if (flags & DBUS_WATCH_READABLE)
      condition |= G_IO_IN;

    if (flags & DBUS_WATCH_WRITABLE)
      condition |= G_IO_OUT;

    source = g_io_create_watch(channel, GIOCondition(condition));

    /*
     * XXX The cast (GIOFunc -> GSourceFunc) is extremely evil. Probably
     *     the intension of the lib designer would be "Use g_io_add_watch(...)
     *     instead", but that will cause loading 40KB of memory which this
     *     app don't use at all. Anyway this makes me uneasy. In the first
     *     place, why can't I find out the document saying this conversion
     *     is safe?
     */
    g_source_set_callback(source, reinterpret_cast<GSourceFunc>(handle_watch),
                          aWatch, NULL);
    g_io_channel_unref (channel);

    dbus_watch_set_data(aWatch, source, unref_source);
  }

  g_source_attach(source, static_cast<GMainContext*>(aUserData));
  return TRUE;
}

static void remove_watch(DBusWatch* aWatch, void* aUserData) {
  void* data = dbus_watch_get_data(aWatch);
  if (!data)
    return;

  GSource* source = static_cast<GSource*>(data);
  g_source_destroy(source);
  g_source_unref(source);
  dbus_watch_set_data(aWatch, NULL, NULL);
}

typedef struct _GSourceWithDBusConnection : public GSource {
  DBusConnection* mConnection;
} GSourceWithDBusConnection;

gboolean prepare (GSource* aSource, gint* aTimeout) {
  assert(aSource);
  assert(aTimeout);

  *aTimeout = -1;

  GSourceWithDBusConnection* source =
    static_cast<GSourceWithDBusConnection*>(aSource);

  DBusDispatchStatus status =
    dbus_connection_get_dispatch_status(source->mConnection);

  return (DBUS_DISPATCH_DATA_REMAINS == status); 
}

gboolean check (GSource* aSource) {
  return FALSE;
}

gboolean dispatch(GSource* aSource, GSourceFunc aCallback,
                         void* aUserData) {
  assert(aSource);

  GSourceWithDBusConnection* source =
    static_cast<GSourceWithDBusConnection*>(aSource);

  GFD_DUMP_DBUS_CONNECTION(source->mConnection);

  dbus_connection_dispatch(source->mConnection);
  return TRUE;
}

} /* namespace gfd:: */

int main(int argc, char* argv[]) {

  DBusError error;
  dbus_error_init(&error);

  DBusConnection* connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
  if (!connection)
    return 1;

  GFD_DUMP_DBUS_CONNECTION(connection);

  /* Query AT-SPI DBUS address */
  {
    DBusMessage* method = dbus_message_new_method_call("org.a11y.Bus",
                                                       "/org/a11y/bus",
                                                       "org.a11y.Bus",
                                                       "GetAddress");

    if (!method) {
      dbus_connection_unref(connection);
      return 1;
    }

    DBusMessage* response =
      dbus_connection_send_with_reply_and_block(connection, method,
                                                DBUS_TIMEOUT_USE_DEFAULT,
                                                &error);
    dbus_message_unref(method);

    if (dbus_error_is_set(&error)) {
      fprintf(stderr, "%s:%s\n", kProductName, error.message); 
      dbus_error_free(&error);
    }

    if(!response)
      return 1;

    GFD_DUMP_DBUS_MESSAGE(response);

    const char* atspiBusAddress(NULL);
    bool appended = dbus_message_get_args(response, &error,
                                          DBUS_TYPE_STRING, &atspiBusAddress,
                                          DBUS_TYPE_INVALID);

    if (dbus_error_is_set(&error)) {
      fprintf(stderr, "%s:%s\n", kProductName, error.message);
      dbus_error_free(&error);
    }

    if (!appended) {
      dbus_message_unref(response);
      return 1;
    }

    /* Swap DBUS connection. */
    dbus_connection_unref(connection);
    connection = dbus_connection_open(atspiBusAddress, &error);

    GFD_DUMP_DBUS_CONNECTION(connection);

    dbus_message_unref(response);

    if (dbus_error_is_set(&error)) {
      fprintf(stderr, "%s:%s\n", kProductName, error.message);
      dbus_error_free(&error);
    }

    if (!connection)
      return 1;

    appended = dbus_bus_register(connection, &error);
    if (dbus_error_is_set(&error)) {
      fprintf(stderr, "%s:%s\n", kProductName, error.message);
      dbus_error_free(&error);
    }

    if (!appended) {
      dbus_connection_unref(connection);
      return 1;
    }
  }

  dbus_bus_add_match(connection,
                     "type='signal',"
                     "interface='org.a11y.atspi.Event.Document',"
                     "member='LoadComplete'",
                     &error);

  if (dbus_error_is_set(&error)) {
    fprintf(stderr, "%s:%s\n", kProductName, error.message); 
    dbus_error_free(&error);
    dbus_connection_unref(connection);
    return 1;
  }

  dbus_connection_add_filter(connection, filter, NULL, NULL);

  {
    DBusMessage* method =
      dbus_message_new_method_call("org.a11y.atspi.Registry",
                                   "/org/a11y/atspi/registry",
                                   "org.a11y.atspi.Registry",
                                   "RegisterEvent");

    if (!method) {
      dbus_connection_unref(connection);
      return 1;
    }

    static const char* event = "document:load-complete";
    bool appended = dbus_message_append_args(method,
                                             DBUS_TYPE_STRING, &event,
                                             DBUS_TYPE_INVALID);

    if (!appended) {
      dbus_message_unref(method);
      dbus_connection_unref(connection);
      return 1;
    }

    DBusMessage* response =
      dbus_connection_send_with_reply_and_block(connection, method,
                                                DBUS_TIMEOUT_USE_DEFAULT,
                                                &error);
    dbus_message_unref(method);

    if (dbus_error_is_set(&error)) {
      fprintf(stderr, "%s:%s\n", kProductName, error.message); 
      dbus_error_free(&error);
    }

    if (response) {
      GFD_DUMP_DBUS_MESSAGE(response);
      dbus_message_unref(response);
    }
  }

  GMainContext* context = g_main_context_default();
  GMainLoop* loop = g_main_loop_new(context, false);

  GSourceFuncs withConnectionTable = {
    gfd::prepare,
    gfd::check,
    gfd::dispatch,
    NULL
  };

  GSource* source =
    g_source_new(&withConnectionTable, sizeof (gfd::GSourceWithDBusConnection));
  
  static_cast<gfd::GSourceWithDBusConnection*>(source)->mConnection =
    connection;

  g_source_attach(source, context);

  dbus_connection_set_wakeup_main_function(connection,
                                           gfd::wakeup,
                                           context, NULL);

  dbus_connection_set_watch_functions (connection,
                                       gfd::add_watch,
                                       gfd::remove_watch,
                                       NULL, context, NULL);
  g_main_loop_run(loop);

  g_source_unref(source);
  dbus_connection_unref(connection);
  return 0;
}

DBusHandlerResult filter(DBusConnection* aConnection,
                         DBusMessage* aMessage,
                         void* aUserData) {
#ifndef NDEBUG
//  mtrace();
#endif
  GFD_DUMP_DBUS_CONNECTION(aConnection);
  GFD_DUMP_DBUS_MESSAGE(aMessage);

  const char* sender = dbus_message_get_sender(aMessage);
  const char* path = dbus_message_get_path(aMessage);

  if (!sender || !path)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if (0 == strncmp(DBUS_SERVICE_DBUS, sender, sizeof(DBUS_SERVICE_DBUS))) {
    /* This is probably "NameAcquired" nottification. */
    assert(0 == strncmp("NameAcquired", dbus_message_get_member(aMessage),
                        sizeof("NameAcquired")));
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  DBusError error;
  dbus_error_init(&error);

  /* Query title to this signal message */
  char* title(NULL);
  {
    DBusMessageIter viter;
    dbus_message_iter_init(aMessage, &viter);
    int type = dbus_message_iter_get_arg_type(&viter);

    while (type != DBUS_TYPE_INVALID) {
      if (DBUS_TYPE_VARIANT == type) {
        DBusMessageIter siter;
        dbus_message_iter_recurse(&viter, &siter);
        type = dbus_message_iter_get_arg_type(&siter);

        if (DBUS_TYPE_STRING == type) {
          dbus_message_iter_get_basic(&siter, &title);
          break;
        }
      }

      dbus_message_iter_next(&viter);
      type = dbus_message_iter_get_arg_type(&viter);
    }
  }

  /* Query URL via DBUS */
  char* url(NULL);
  {
    DBusMessage* method =
      dbus_message_new_method_call(sender, path,
                                   "org.a11y.atspi.Document",
                                   "GetAttributeValue");
    if (!method)
      return DBUS_HANDLER_RESULT_NEED_MEMORY;

    static const char* attribute = "DocURL";
    bool appended = dbus_message_append_args(method,
                                             DBUS_TYPE_STRING, &attribute,
                                             DBUS_TYPE_INVALID);
    if (!appended) {
      dbus_message_unref(method);
      return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    DBusMessage* response =
      dbus_connection_send_with_reply_and_block(aConnection,
                                                method,
                                                DBUS_TIMEOUT_USE_DEFAULT,
                                                &error);

    dbus_message_unref(method);

    if (dbus_error_is_set(&error)) {
      fprintf(stderr, "%s:%s\n", kProductName, error.message); 
      dbus_error_free(&error);
    }

    if(response) {
      GFD_DUMP_DBUS_MESSAGE(response);
      appended = dbus_message_get_args(response, &error,
                                       DBUS_TYPE_STRING, &url,
                                       DBUS_TYPE_INVALID);
      if (dbus_error_is_set(&error)) {
        fprintf(stderr, "%s:%s\n", kProductName, error.message);
        dbus_error_free(&error);
      }
      dbus_message_unref(response);
    }
  }

  /* Compose ISO 8601 datetime */
  char datetime[sizeof("0000-00-00T00:00:00")];
  {
    time_t now = time(NULL);
    tm* gmt = gmtime(&now);
#ifndef NDEBUG
    size_t len =
#endif
    strftime(datetime, sizeof(datetime), "%FT%H:%M:%S", gmt);
    assert((sizeof("0000-00-00T00:00:00") - 1) == len);
    assert('\0' == datetime[len]);
  }

  FILE* fp = fopen(kMonitorLogFile, "a");
  if (!fp) {
    perror(kMonitorLogFile);
  }
  else {
    int count = fprintf(fp, "d=%s+0000\nt=%s\nu=%s\n\n", datetime, title, url);

    if (count < 0) {
      perror(kMonitorLogFile);
    }
    fclose(fp);
  }
#ifndef NDEBUG
//  muntrace();
#endif
  return DBUS_HANDLER_RESULT_HANDLED;
}

