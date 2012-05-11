#!/usr/bin/python
import pyatspi
import re

def listener(aEvent):
  document = aEvent.source.queryDocument()
  attributes = document.getAttributes()
  test = re.compile("^DocURL:")
  node = filter(test.match, attributes)[0]
  print "Title:" + aEvent.source.name
  print "URL  :" + re.sub(test, "", node) + "\n"

pyatspi.Registry.registerEventListener(listener, "document:load-complete")
pyatspi.Registry.start()

# Note that getAttributeValue is broken.
# See https://bugzilla.gnome.org/show_bug.cgi?id=674515

################################################################################
#def listener(aEvent):
#  document = aEvent.source.queryDocument()
#  print "Title:" + aEvent.source.name
#  print "URL  :" + document.getAttributeVlue("DocURL") + "\n"
################################################################################

