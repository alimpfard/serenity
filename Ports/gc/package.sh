#!/usr/bin/env -S bash ../.port_include.sh
port=gc
version=8.0.4
useconfigure=true
files="https://github.com/ivmai/bdwgc/releases/download/v8.0.4/gc-8.0.4.tar.gz $port-$version.tar.gz"
configopts="--enable-threads=single"

pre_configure() {
    get_up_to_date_config_sub
}
