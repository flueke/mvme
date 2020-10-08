.. index:: Data Export, EventServer, ROOT, ROOT export
.. _reference-event_server:

EventServer for data export (ROOT or custom formats)
====================================================
Currently there are two ways built into mvme which allow readout data to be
accessed by external software:

* The :ref:`<Analysis ExportSink> analysis-ExportSink` which is described in
  the Analysis reference.

  This method should be used when a limited number of analysis data arrays -
  all belonging to modules in the same VME event - are to be exported.

* The EventServer component described in this section. It provides export
  functionality of the complete event data produced during a DAQ run. The data
  is transmitted over a network connection. The source codes for a minimal
  "printf"-client and for a ROOT client are shipped with mvme.

  The EventServer should be used when you want to export all of the data
  produced by an experiment.

Overview
---------------------------------------
The EventServer component is a TCP based network server built into mvme. It
uses a custom, self-describing protocol to transmit event data across the
network.

The server is attached to the analysis side of mvme which means during a live
DAQ run it will not slow down the readout. Instead if the server, or the
attached clients, cannot keep up with the data rate the normal buffer loss
mechanism at the core of the mvme DAQ will limit the data rate. This means
during a live run clients may only see parts of the data, whereas when
replaying from a mvme listmode file all data will be transmitted.

The supplied ROOT client recreates the full *VME Event -> VME Module -> Data
Array* structure present in an mvme setup using automatically generated ROOT
classes. Additionally histograms of the raw parameter values are created and
written to a separate ROOT file.

.. note::
  The ROOT client requires ROOT6 and has so far only been tested on Linux. A
  Windows port might be added in the future.

EventServer HowTo Guide
---------------------------------------
This guide explains how to start and use the EventServer in mvme. The examples
and screenshots where created using the demo workspace available on our website:

https://mesytec.com/kundendaten/mvme-examples/mvme-example-workspace-01.tar.bz2

The ROOT client

HowTo Guide

   Compiling the mvme event_server ROOT client

   ::
       cd mvme             # cd intot the mvme installation directory
       source bin/initMVME

   Enabling the event_server in mvme

   Running the client and example ROOT file structure

event_server with ROOT client

Description of the event_server component and protocol

event_server internals and 

