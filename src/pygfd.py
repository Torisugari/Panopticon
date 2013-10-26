#!/usr/bin/python
import pyatspi

def listener(aEvent):
  document = aEvent.source.queryDocument()
  print "Title:" + aEvent.source.name
  print "URL  :" + document.getAttributeValue("DocURL") + "\n"

pyatspi.Registry.registerEventListener(listener, "document:load-complete")
pyatspi.Registry.start()

