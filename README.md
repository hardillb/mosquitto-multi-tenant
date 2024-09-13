# Multi Tenant Mosquitto Plugin

This plugin is a very basic way to allow multiple teams to use a single MQTT broker without 
any access to each others topics, and without needing them to stick to a specific topic 
pattern/prefix.

## Build

- Check out & build the mosquitto develop branch (or 2.1.0 once released)
- Check out this project in an adjacent directory
- run `make` in this project


## Configure

```
port 1883
plugin ./mosquitto_multi_tenant.so
plugin_opt_regex ^[a-z0-9]+@([a-z0-9]+)$

allow_anonymous false
password_file passwd
```

## Testing

Use `mosquitto_passwd` to create a `passwd` file with usernames of the format `user@groupname`
and an `admin` user then run `./test.sh` to start the broker.

 - Subscribe user from group `foo` with `mosquitto_sub -u user@foo -P password -v -t '#'`
 - Subscribe user from group `bar` with `mosquitto_sub -u user@bar -P password -v -t '#'`
 - Subscribe admin user with `mosquitto_sub -u admin -P password -v -t '#'`

Then

 - Publish message for `foo` group with `mosquitto_pub -u user@foo -P password -t test -m message`

## Limitations

 - Client IDs still need to be globally unique across the whole broker.
 - ~~Shared Subscriptions not supported (yet)~~

## Acknowledgement

This code is based on the topic-jail plugin example included in the Mosquitto 2.1.0 release
