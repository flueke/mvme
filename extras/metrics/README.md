# mvme prometheus/grafana support files

## Contents

    ├── docker-compose.yml              Compose project file to setup prometheus and grafana for mvme.
    ├── grafana
    │   ├── data
    │   │   └── dashboards
    │   │       └── mvme.json           Basic dashboard displaying mvme metrics. Can be imported into existing grafana installations.
    │   └── etc                         Initial configuration and provisioning for the grafana instance.
    │       ├── grafana.ini
    │       └── provisioning
    │           ├── dashboards
    │           │   └── mvme.yml
    │           └── datasources
    │               └── prometheus.yml
    └── prometheus
        └── prometheus.yml              Prometheus config including a scrape job for mvme running on localhost.

## Usage

- Run `docker compose up -d`, then navigate to http://localhost:3000
- Login using *admin/admin* as username and password. Optionally change the admin password.
- The mvme dashboard should be available in grafana.
- Run mvme and start the DAQ or replay data from file. Metrics should start to update.

## Troubleshooting

- Ensure mvme is exporting metrics on port 13802: `curl http://localhost:13802/metrics` must yield
  prometheus metrics. If this does not work check if your firewall is blocking the port or some
  other program is using port 13802.

- Ensure prometheus is working: go to `http://localhost:9090` and check `Status -> Targets`. The
  mvme target should be up and running.
