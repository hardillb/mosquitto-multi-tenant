# Multi Tenant Mosquitto Plugin

This plugin is a very basic way to allow multiple teams to use a single MQTT broker without 
any access to each others topics, and without needing them to stick to a specific topic 
pattern/prefix.

## Build

- Check out & build the mosquitto develop branch
- Check out this project in an adjacent directory
- run `make` in this directory


## Configure

```
port 1883
plugin ./mosquitto_multi_tenant.so
plugin_opt_regex ^[a-z0-9]+@([a-z0-9]+)$

allow_anonymous false
password_file passwd
```


## Acknowledgement

This code is based on the topic-jail plugin example included in the Mosquitto 2.1.0 release