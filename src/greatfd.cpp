/****************************************************************************
 * The Golden Panopticon Project                                            +
 *                                                                          *
 *                    Great Firedaemon (ver. 1.0)                           *
 *                                                                          *
 *                                                 2012-04-16T00:00:00+00:00*
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

#define GFD_ENABLE_CENSORING
#define GFD_USE_ATTR_HASHTABLE
//#define GFD_STOP_MONITORING_LOG
//#define NODEBUG

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <time.h>

extern "C" {
#include <atspi/atspi.h> 
}

static const char kProductName[]    = "great firedaemon";
static const char kMonitorLogFile[] = "logs/monitor.log";

void documentLoadListener(const AtspiEvent *aEvent);

#ifdef GFD_ENABLE_CENSORING

#include <string.h>
#include <stdlib.h>

static const char kCensorList[]     = "settings/censor.lst";
static const char kCensorLogFile[]  = "logs/censor.log";

bool censorChildren(AtspiAccessible* aParent, const char* aKeyword);

/* singly linked list */
typedef struct _CensorWordList {
  const char* data;
  const _CensorWordList* next;
} CensorWordList;

typedef struct _TextFragmentList {
  char* data;
  _TextFragmentList* next;
} TextFragmentList;

const CensorWordList* gCensorWordList(NULL);
#endif

int main(int argc, char* argv[]) {
#ifdef GFD_ENABLE_CENSORING
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
  {
    char* next;
    const char* line = strtok_r(buff, "\n", &next);
    const CensorWordList* latestNode(NULL);

    while (line) {
      if (char(line[0]) != '\0') {
        CensorWordList* node = new CensorWordList();
        node->data = line;
        node->next = latestNode;
        latestNode = node;
      }
      line = strtok_r(NULL, "\n", &next);
    }
    gCensorWordList = latestNode;
  }
#endif

  GMainLoop* loop = g_main_loop_new(NULL, false);

  if (!loop || (0 != atspi_init())) {
    // If this happens often, I should write a proper error message handler.
    return 1;
  }

  AtspiEventListener* listener =
    atspi_event_listener_new_simple(documentLoadListener, NULL);

  GError* error(NULL);
  bool registered =
    atspi_event_listener_register(listener, "document:load-complete", &error);
 
  if (error) {
    fprintf(stderr, "%s:%s\n", kProductName, error->message);
    g_error_free(error);
  }
  else if (registered) {
    assert(!error);
    g_main_loop_run(loop);
  }

  return atspi_exit();
}

void documentLoadListener(const AtspiEvent *aEvent) {
  /* abort if event target is not browser's dom window. */
  GError* error(NULL);
  AtspiRole role = atspi_accessible_get_role(aEvent->source, &error);

  if (error) {
    fprintf(stderr, "%s:%s\n", kProductName, error->message);
    g_error_free(error);
    return;
  }

  if (ATSPI_ROLE_DOCUMENT_FRAME != role)
    return;

  AtspiDocument* document = atspi_accessible_get_document(aEvent->source);
  if (!document)
    return;


  /* Query title */
  char* title = atspi_accessible_get_name(aEvent->source, &error);
  if (error) {
    fprintf(stderr, "%s:%s\n", kProductName, error->message);
    g_error_free(error);
    return;
  }

  /* Query URL */
#ifndef GFD_USE_ATTR_HASHTABLE
  // XXX I wonder why this won't work...
  //     By the way, what's "GetAttributevaluee"? typo?
  static char attr[] = "DocURL"; // to avoid cast
  gchar* url = atspi_document_get_attribute_value(document, attr, &error);
  if (!!error) {
    fprintf(stderr, "%s:%s\n",kProductName, error->message);
    g_error_free(error);
    return;
  }
#else
  GHashTable* attributes = atspi_document_get_attributes(document, &error);
  g_object_unref(document);

  if (error) {
    fprintf(stderr, "%s:%s\n", kProductName, error->message);
    g_error_free(error);
    return;
  }

  void* value = g_hash_table_lookup(attributes, "DocURL");
  g_object_unref(document);

  char* url = static_cast<char*>(value);
#endif

  /* Compose ISO 8601 datetime */
  char datetime[sizeof("0000-00-00T00:00:00")];
  {
    time_t now = time(NULL);
    tm* gmt = gmtime(&now);
    size_t len = strftime(datetime, sizeof(datetime), "%FT%H:%M:%S", gmt);
    assert((sizeof("0000-00-00T00:00:00") - 1) == len);
  }

  /* Write them down. */
#ifndef GFD_STOP_MONITORING_LOG
  FILE* fp = fopen(kMonitorLogFile, "a");
  if (!fp) {
    perror(kMonitorLogFile);
    return;
  }
#else
  FILE* fp = stdout;
#endif
  int cnt = fprintf(fp, "d=%s+0000\nt=%s\nu=%s\n\n", datetime, title, url);

  if (cnt < 0) {
    perror(kMonitorLogFile);
  }

#ifndef GFD_STOP_MONITORING_LOG
  fclose(fp);
#endif

#ifdef GFD_ENABLE_CENSORING
  const CensorWordList* node = gCensorWordList;
  while(node) {
    bool hit = censorChildren(aEvent->source, node->data);
    if (hit) {
      fp = fopen(kCensorLogFile, "a");
      if (!fp) {
        perror(kCensorLogFile);
        break;
      }

      cnt = fprintf(fp, "k=%s\nd=%s+0000\nt=%s\nu=%s\n\n",
                    node->data, datetime, title, url);
      if (cnt < 0) {
        perror(kCensorLogFile);
      }
      fclose(fp);
    }
    node = node->next;
  }
#endif

  g_free(title);

#ifndef GFD_USE_ATTR_HASHTABLE
  g_free(url);
#else
  g_hash_table_unref(attributes);
#endif

  // Seriously, is there any way to delete the cached |AtspiAccessible|
  // objects?
  // atspi_accessible_clear_cache(aEvent->source); ?
  return;
}

#ifdef GFD_ENABLE_CENSORING
/* In terms of performance, I should cache all the text data instead. */
bool censorChildren(AtspiAccessible* aParent, const char* aKeyword) {
  if (!aParent)
    return false;

  assert(!!aKeyword);

  bool result(false);
  GError* error(NULL);

  int count = atspi_accessible_get_child_count(aParent, &error);
  if (error) {
    fprintf(stderr, "%s:%s\n", kProductName, error->message);
    g_error_free(error);
    error = NULL;
    return false;
  }

  int i;
  for (i = 0; i < count; i++){
    AtspiAccessible* child =
      atspi_accessible_get_child_at_index(aParent, i, &error);
    if (error) {
      fprintf(stderr, "%s:%s\n", kProductName, error->message);
      g_error_free(error);
      error = NULL;
      continue;
    }

    AtspiText* text = atspi_accessible_get_text(child);
    if (text) {
      int end = atspi_text_get_character_count(text, &error);
      if (!error) {
        char* ptr = atspi_text_get_text(text, 0, end, &error);
        result = !!(::strcasestr(ptr, aKeyword));
        g_free(ptr);
      } else {
        fprintf(stderr, "%s:%s\n", kProductName, error->message);
        g_error_free(error);
        error = NULL;
      }
      g_object_unref(text);
    }

    if (!result) {
      result = censorChildren(child, aKeyword);
    }

    if (result)
      return true;
  }

  return false;
}
#endif
