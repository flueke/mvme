.. index:: Metrics, Prometheus, Grafana
.. _reference-metrics:

Metrics
=======

Since version 1.10 mvme exports prometheus compatible metrics. By default an
embedded webserver exposes metrics on port 13802. ``curl`` can be used to list
the available metrics: ``curl http://localhost:13802/metrics``.

Included in the mvme distribution is a docker-compose project for setting up
local prometheus and grafana instances to monitor metrics exported by mvme. Also
included is a simple dashboard which can be imported into existing grafana
instances. See ``extras/metrics/README.md`` for details.
