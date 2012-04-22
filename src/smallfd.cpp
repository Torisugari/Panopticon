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
 * memory consumption (roughly less than 250KiB).
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
#include <dbus/dbus-glib-lowlevel.h>
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

void gfdDumMessage(DBusMessage* aMessage,
                 int aLine = 0, const char* aFile = NULL) {
  const char* type =
    dbus_message_type_to_string(dbus_message_get_type(aMessage));
  dbus_uint32_t serial = dbus_message_get_serial(aMessage);
  const char* sender = dbus_message_get_sender(aMessage);
  const char* destination = dbus_message_get_destination(aMessage);
  const char* path = dbus_message_get_path(aMessage);
  const char* interface = dbus_message_get_interface(aMessage);
  const char* member = dbus_message_get_member(aMessage);
  printf("=== L%d @ %s ==\n"
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

#define GFD_DUMP_DBUS_MESSAGE(__DBUSMESSAGE__) \
          (gfdDumMessage((__DBUSMESSAGE__), (__LINE__), (__FILE__)))

#else
#define GFD_DUMP_DBUS_MESSAGE(ignore)((void) 0)
#endif

int main(int argc, char* argv[]) {
  GMainLoop* loop = g_main_loop_new(NULL, false);

  if (!loop)
    return 1;

  DBusError error;
  dbus_error_init(&error);

  DBusConnection* connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
  if (!connection)
    return 1;

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

  dbus_connection_setup_with_g_main(connection, g_main_context_default());

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

  DBusMessage* method = dbus_message_new_method_call("org.a11y.atspi.Registry",
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
                                              DBUS_TIMEOUT_USE_DEFAULT, &error);
  dbus_message_unref(method);

  if (response) {
    GFD_DUMP_DBUS_MESSAGE(response);
    dbus_message_unref(response);
  }

  dbus_connection_unref(connection);

  g_main_loop_run(loop);

  return 0;
}

DBusHandlerResult filter(DBusConnection* aConnection,
                         DBusMessage* aMessage,
                         void* aUserData) {
#ifndef NDEBUG
//  mtrace();
#endif
  GFD_DUMP_DBUS_MESSAGE(aMessage);

  const char* sender = dbus_message_get_sender(aMessage);
  const char* path = dbus_message_get_path(aMessage);

  if (!sender || !path)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if (0 == strncmp(DBUS_SERVICE_DBUS, sender,
                   sizeof(DBUS_SERVICE_DBUS))) {
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

