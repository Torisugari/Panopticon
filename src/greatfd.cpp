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

/* This service simply watches browser's URL bar and title bar. And it
 * writes down all the URLs user has visited. The file "monitor.log" will be
 * something like:
 *
 * >d=1970-01-01T00:00:00+0000
 * >t=Wikipedia, the free encyclopedia
 * >u=http://en.wikipedia.org/wiki/Main_Page
 *
 * d, t and u are the acronyms of Datetime, Title and URL respectively.
 *
 * When censoring-mode is enabled (enabled by default), it also watches the
 * document so as to check if the website contains specific words, which
 * is listed in "censor.lst".
 *
 * If one of them matches, it will leave a message in "censor.log".
 *
 * >k=Voldemort
 * >d=2012-04-18T11:27:36+0000
 * >t=Harry Potter - Wikipedia, the free encyclopedia
 * >u=http://en.wikipedia.org/wiki/Harry_Potter
 *
 * The above log message indicates that the web page "Harry Potter - Wikipedia,
 * the free encyclopedia" at <http://en.wikipedia.org/wiki/Harry_Potter>
 * improperly contains (or at least contained at that time) the phrase
 * "Voldemort".
 *
 * As a result, "monitor.log" usually becomes much larger than "censor.log".
 *
 * The biggest merit of this application is that it should work with HTTPS
 * nonetheless, unlike other network monitoring software. Besides this is
 * not a browsers' addon. There's no direct dependency on their versions.
 *
 * Note: The way to exit is not implemented yet. Type Ctrl-C, ps & kill or
 *       gnome-system-monitor instead.
 */

#define GFD_READ_CENSOR_LIST 0
#define GFD_STOP_MONITOR_LOG 0

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <mcheck.h>
#include <unistd.h>

extern "C" {
#include <dbus/dbus.h> 
}

static const char kProductName[]    = "great firedaemon";

#if GFD_STOP_MONITOR_LOG
static const char kMonitorLogFile[] = "logs/monitor.log";
#else
static const char kMonitorLogFile[] = "/dev/null";
#endif

static const char kCensorList[]     = "settings/censor.lst";
static const char kCensorLogFile[]  = "logs/censor.log";

/* singly linked list */
typedef struct _CensorWordList {
  const char* data;
  const _CensorWordList* next;
} CensorWordList;

typedef struct _TextFragmentList {
  char* data;
  _TextFragmentList* next;
} TextFragmentList;

DBusHandlerResult
filter(DBusConnection* aConnection, DBusMessage* aMessage,
       const CensorWordList* aCensorWordList);
bool
censor(const TextFragmentList* aFragment, const char* aKeyword);

inline void
flogf(const char* aFilename, const char* aFormat, ...);

TextFragmentList* copyTexts(DBusConnection* aConnection,
                            const char* aDestination,
                            const char* aPath,
                            TextFragmentList* aLatestNode);

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

    case DBUS_TYPE_OBJECT_PATH: {
        const char* data(NULL);
        dbus_message_iter_get_basic(aIter, &data);
        printf("type=path, value=\"%s\";\n", data);
      }
      break;

    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_INT64:
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
    case DBUS_TYPE_UINT64:
      {
        dbus_uint64_t data = 0;
        dbus_message_iter_get_basic(aIter, &data);
        printf("type=int, value=%llu;\n", data);
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

    case DBUS_TYPE_STRUCT: {
        printf("type=struct, value= {\n");

        DBusMessageIter iter;
        dbus_message_iter_recurse(aIter, &iter);
        gfdDumpIter(&iter, aIndent + 1);
        for (i = 0; i < aIndent; i++)
          printf("  ");
        printf("};\n");
      }
      break;

    case DBUS_TYPE_ARRAY: {
        printf("type=array, value= [\n");

        DBusMessageIter iter;
        dbus_message_iter_recurse(aIter, &iter);
        gfdDumpIter(&iter, aIndent + 1);
        for (i = 0; i < aIndent; i++)
          printf("  ");
        printf("];\n");
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
         " *DBusConnection*: 0x%llx\n"
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
         aLine, aFile, uint64_t(aConnection),
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

void checkDBusError(DBusError* aError, int aLine, const char* aFile) {
  if (!dbus_error_is_set(aError))
    return;

  fprintf(stderr, "%s: L%d @ %s\n%s\n",
          kProductName, aLine, aFile, aError->message);

  dbus_error_free(aError);
}

#define GFD_CHECK_DBUS_ERROR(__DBUSERROR__) \
  (checkDBusError((__DBUSERROR__), (__LINE__), (__FILE__)))

int main(int argc, char* argv[]) {
#if GFD_READ_CENSOR_LIST
  /*
     Note that the life time of this buffer equals to that of
     this application.
   */
  char* buff(NULL); 
  {
    FILE* fp = fopen(kCensorList, "rb"); 
    if (!fp) {
      perror(kCensorList);
      return 1;
    }

    if (0 != fseek(fp, 0, SEEK_END)) {
      perror(kCensorList);
      fclose(fp);
      return 1;
    }

    long size1 = ftell(fp);
    if (size1 < 0) {
      perror(kCensorList);
      fclose(fp);
      return 1;
    }
    else if (size1 == 0) {
      return 1;
    }

    rewind(fp);

    buff = new char[size1];
    size_t size2 = fread(buff, 1, size1, fp);
    if (size_t(size1) != size2) {
      perror(kCensorList);
    }

    fclose(fp);
  }
#endif

  const CensorWordList* latestNode(NULL);
  {
#if GFD_READ_CENSOR_LIST
    char* next;
    const char* line = strtok_r(buff, "\n", &next);

    while (line) {
      if (char(line[0]) != '\0') {
        CensorWordList* node = new CensorWordList();
        node->data = line;
        node->next = latestNode;
        latestNode = node;
      }
      line = strtok_r(NULL, "\n", &next);
    }
#endif
    int i;
    for(i = 1; i < argc; i++) {
      if (char(*argv[i]) != '\0') {
        CensorWordList* node = new CensorWordList();
        node->data = argv[i];
        node->next = latestNode;
        latestNode = node;
      }
    }
  }

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

    GFD_CHECK_DBUS_ERROR(&error);

    if(!response)
      return 1;

    GFD_DUMP_DBUS_MESSAGE(response);

    const char* atspiBusAddress(NULL);
    bool appended = dbus_message_get_args(response, &error,
                                          DBUS_TYPE_STRING, &atspiBusAddress,
                                          DBUS_TYPE_INVALID);

    GFD_CHECK_DBUS_ERROR(&error);

    if (!appended) {
      dbus_message_unref(response);
      return 1;
    }

    /* Swap DBUS connection. */
    dbus_connection_unref(connection);
    connection = dbus_connection_open(atspiBusAddress, &error);

    GFD_DUMP_DBUS_CONNECTION(connection);

    dbus_message_unref(response);

    GFD_CHECK_DBUS_ERROR(&error);

    if (!connection)
      return 1;

    appended = dbus_bus_register(connection, &error);
    GFD_CHECK_DBUS_ERROR(&error);

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

    GFD_CHECK_DBUS_ERROR(&error);

    if (response) {
      GFD_DUMP_DBUS_MESSAGE(response);
      dbus_message_unref(response);
    }
  }

  for (;;) {
    dbus_connection_read_write_dispatch(connection, -1);
    DBusMessage* signal = dbus_connection_pop_message(connection);

    if (!signal) {
      static int life = 20;
      if ((life--) < 0)
        break;

      sleep(1);
      continue;
    }

    DBusHandlerResult result = filter(connection, signal, latestNode);

    dbus_message_unref(signal);

    if (result != DBUS_HANDLER_RESULT_HANDLED) {
      break;
    }
  }

  {
    DBusMessage* method =
      dbus_message_new_method_call("org.a11y.atspi.Registry",
                                   "/org/a11y/atspi/registry",
                                   "org.a11y.atspi.Registry",
                                   "DeregisterEvent");

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

    GFD_CHECK_DBUS_ERROR(&error);

    if (response) {
      GFD_DUMP_DBUS_MESSAGE(response);
      dbus_message_unref(response);
    }
  }

  dbus_connection_unref(connection);
  return 0;
}

DBusHandlerResult filter(DBusConnection* aConnection,
                         DBusMessage* aMessage,
                         const CensorWordList* aCensorWordList) {
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
  DBusMessage* urlMessage(NULL);
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

    urlMessage =
      dbus_connection_send_with_reply_and_block(aConnection,
                                                method,
                                                DBUS_TIMEOUT_USE_DEFAULT,
                                                &error);

    dbus_message_unref(method);

    GFD_CHECK_DBUS_ERROR(&error);

    if (urlMessage) {
      GFD_DUMP_DBUS_MESSAGE(urlMessage);
      appended = dbus_message_get_args(urlMessage, &error,
                                       DBUS_TYPE_STRING, &url,
                                       DBUS_TYPE_INVALID);
      GFD_CHECK_DBUS_ERROR(&error);
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

  flogf(kMonitorLogFile, "d=%s+0000\nt=%s\nu=%s\n\n", datetime, title, url);

  if (aCensorWordList) {
    const CensorWordList* words = aCensorWordList;
    TextFragmentList* texts = copyTexts(aConnection, sender, path, NULL);

    while(words) {
      bool hit = censor(texts, words->data);
      if (hit) {
        flogf(kCensorLogFile, "k=%s\nd=%s+0000\nt=%s\nu=%s\n\n",
              words->data, datetime, title, url);
      }
      words = words->next;
    }

    /* release memory allocated by g_strdup(). */
    while (texts) {
      free(texts->data);

      TextFragmentList* oldNode = texts;
      texts = texts->next;

      delete oldNode;
    }

  }
  if (urlMessage)
    dbus_message_unref(urlMessage);
#ifndef NDEBUG
//  muntrace();
#endif
  return DBUS_HANDLER_RESULT_HANDLED;
}

bool censor(const TextFragmentList* aFragment, const char* aKeyword) {
  const TextFragmentList* fragment = aFragment;
  while (fragment) {
    assert(fragment->data);
    if (strcasestr(fragment->data, aKeyword))
      return true;
    fragment = fragment->next;
  }
  return false;
}

TextFragmentList* copyTexts(DBusConnection* aConnection,
                            const char* aDestination,
                            const char* aPath,
                            TextFragmentList* aLatestNode) {

  TextFragmentList* result(aLatestNode);

  DBusError error;
  dbus_error_init(&error);

  // Check whether this is a text, or not.
  bool isText(false);
  {
    DBusMessage* method =
      dbus_message_new_method_call(aDestination, aPath,
                                   "org.a11y.atspi.Accessible",
                                   "GetInterfaces");
    if (!method)
      return result;

    DBusMessage* response =
      dbus_connection_send_with_reply_and_block(aConnection,
                                                method,
                                                DBUS_TIMEOUT_USE_DEFAULT,
                                                &error);

    dbus_message_unref(method);

    GFD_CHECK_DBUS_ERROR(&error);

    if (response) {
      DBusMessageIter aiter;
      dbus_message_iter_init(response, &aiter);
      int type = dbus_message_iter_get_arg_type(&aiter);
      if (DBUS_TYPE_ARRAY == type) {
        DBusMessageIter siter;
        dbus_message_iter_recurse(&aiter, &siter);
        type = dbus_message_iter_get_arg_type(&siter);
        const char* interface;
        while (DBUS_TYPE_STRING == type) {
          dbus_message_iter_get_basic(&siter, &interface);
          if (0 == strncmp("org.a11y.atspi.Text", interface,
                           sizeof("org.a11y.atspi.Text"))) {
            isText = true;
            break;
          }
          dbus_message_iter_next(&siter);
          type = dbus_message_iter_get_arg_type(&siter);
        }
      }
      dbus_message_unref(response);
    }
  }
  /* Query URL via DBUS */
  int characterCount(0);
  if (isText) {
    DBusMessage* method =
      dbus_message_new_method_call (aDestination, aPath,
                                    DBUS_INTERFACE_PROPERTIES,
                                    "Get");
    if (!method)
      return result;

    static const char* interface = "org.a11y.atspi.Text";
    static const char* attribute = "CharacterCount";

    bool appended =
      dbus_message_append_args(method,
                               DBUS_TYPE_STRING, &interface,
                               DBUS_TYPE_STRING, &attribute,
                               DBUS_TYPE_INVALID);
    if (!appended) {
      dbus_message_unref(method);
      return result;
    }

    DBusMessage* response =
      dbus_connection_send_with_reply_and_block(aConnection,
                                                method,
                                                DBUS_TIMEOUT_USE_DEFAULT,
                                                &error);

    dbus_message_unref(method);

    GFD_CHECK_DBUS_ERROR(&error);
    if (response) {
      DBusMessageIter viter;
      dbus_message_iter_init(response, &viter);
      int type = dbus_message_iter_get_arg_type(&viter);

      GFD_DUMP_DBUS_MESSAGE(response);
        if (DBUS_TYPE_VARIANT == type) {
          DBusMessageIter iiter;
          dbus_message_iter_recurse(&viter, &iiter);
          type = dbus_message_iter_get_arg_type(&iiter);

          if (DBUS_TYPE_INT32 == type) {
            dbus_message_iter_get_basic(&iiter, &characterCount);
          }
        }
      dbus_message_unref(response);
    }
  }

  if (isText && characterCount > 2) {
    DBusMessage* method =
      dbus_message_new_method_call (aDestination, aPath,
                                    "org.a11y.atspi.Text",
                                    "GetText");
    if (!method)
      return result;

    static const int32_t start(0);

    bool appended =
      dbus_message_append_args(method,
                               DBUS_TYPE_INT32, &start,
                               DBUS_TYPE_INT32, &characterCount,
                               DBUS_TYPE_INVALID);
    if (!appended) {
      dbus_message_unref(method);
      return result;
    }

    DBusMessage* response =
      dbus_connection_send_with_reply_and_block(aConnection,
                                                method,
                                                DBUS_TIMEOUT_USE_DEFAULT,
                                                &error);

    dbus_message_unref(method);

    GFD_CHECK_DBUS_ERROR(&error);

    if (response) {
      const char* data(NULL);
      //GFD_DUMP_DBUS_MESSAGE(response);
      appended = dbus_message_get_args(response, &error,
                                       DBUS_TYPE_STRING, &data,
                                       DBUS_TYPE_INVALID);
      GFD_CHECK_DBUS_ERROR(&error);
      if (appended && data) {
        char* str = strdup(data);
        TextFragmentList* node = new TextFragmentList();
        node->data = str;
        node->next = result;
        result = node;
      }
      dbus_message_unref(response);
    }
  }

  int childCount(0);
  {
    DBusMessage* method =
      dbus_message_new_method_call(aDestination, aPath,
                                   DBUS_INTERFACE_PROPERTIES,
                                   "Get");
    if (!method)
      return result;

    static const char* interface = "org.a11y.atspi.Accessible";
    static const char* attribute = "ChildCount";

    bool appended =
      dbus_message_append_args(method,
                               DBUS_TYPE_STRING, &interface,
                               DBUS_TYPE_STRING, &attribute,
                               DBUS_TYPE_INVALID);
    if (!appended) {
      dbus_message_unref(method);
      return result;
    }

    DBusMessage* response =
      dbus_connection_send_with_reply_and_block(aConnection,
                                                method,
                                                DBUS_TIMEOUT_USE_DEFAULT,
                                                &error);

    dbus_message_unref(method);

    GFD_CHECK_DBUS_ERROR(&error);

    if (response) {
      DBusMessageIter viter;
      dbus_message_iter_init(response, &viter);
      int type = dbus_message_iter_get_arg_type(&viter);

      //GFD_DUMP_DBUS_MESSAGE(response);
      while (type != DBUS_TYPE_INVALID) {
        if (DBUS_TYPE_VARIANT == type) {
          DBusMessageIter siter;
          dbus_message_iter_recurse(&viter, &siter);
          type = dbus_message_iter_get_arg_type(&siter);

          if (DBUS_TYPE_INT32 == type) {
            dbus_message_iter_get_basic(&siter, &childCount);
            break;
          }
        }

        dbus_message_iter_next(&viter);
        type = dbus_message_iter_get_arg_type(&viter);
      }
      dbus_message_unref(response);
    }
  }

  int i;
  for (i = 0; i < childCount; i++) {
    DBusMessage* method =
      dbus_message_new_method_call(aDestination, aPath,
                                   "org.a11y.atspi.Accessible",
                                   "GetChildAtIndex");
    if (!method)
      return result;

    bool appended = dbus_message_append_args(method,
                                             DBUS_TYPE_INT32, &i,
                                             DBUS_TYPE_INVALID);
    if (!appended) {
      dbus_message_unref(method);
      return result;
    }

    DBusMessage* response =
      dbus_connection_send_with_reply_and_block(aConnection,
                                                method,
                                                DBUS_TIMEOUT_USE_DEFAULT,
                                                &error);

    dbus_message_unref(method);

    GFD_CHECK_DBUS_ERROR(&error);

    if (response) {
      DBusMessageIter parentIter;
      dbus_message_iter_init(response, &parentIter);
      int type = dbus_message_iter_get_arg_type(&parentIter);
      char* signature = dbus_message_iter_get_signature(&parentIter);

      //GFD_DUMP_DBUS_MESSAGE(response);
      if (DBUS_TYPE_STRUCT == type &&
          0 == strncmp("(so)", signature, sizeof("(so)"))) {
        DBusMessageIter childIter;
        dbus_message_iter_recurse(&parentIter, &childIter);
        type = dbus_message_iter_get_arg_type(&childIter);

        const char* destination(NULL);
        if (DBUS_TYPE_STRING == type) {
          dbus_message_iter_get_basic(&childIter, &destination);
        }
        else {
          /* catch error */
        }

        dbus_message_iter_next(&childIter);
        type = dbus_message_iter_get_arg_type(&childIter);

        const char* path(NULL);
        if (DBUS_TYPE_OBJECT_PATH == type) {
          dbus_message_iter_get_basic(&childIter, &path);
        }
        else {
          /* catch error */
        }

        if (path && destination) {
          result = copyTexts(aConnection,destination, path, result);
        }
      }
      dbus_free(signature);
      dbus_message_unref(response);
    }
  }
  return result;
}

inline void flogf(const char* aFilename, const char* aFormat, ...) {
  /* This is a little dangerous for compiler doesn't check arg types. */
  FILE* fp = fopen(aFilename, "a");
  if (!fp) {
    perror(aFilename);
    return;
  }

  va_list args;
  va_start(args, aFormat);
  int count = vfprintf(fp, aFormat, args);
  va_end(args);

  if (count < 0) {
    perror(aFilename);
  }
  fclose(fp);
}
