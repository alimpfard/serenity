#!/usr/bin/env -S bash ../.port_include.sh
port=citron
version=0.0.9.3
useconfigure=true
echo $DESTDIR
configopts="STD_PATH=/share/Citron --prefix=$DESTDIR"
files="https://github.com/alimpfard/citron/archive/refs/heads/master.zip $port-$version.zip"
depends="libffi"
tld_workdir=$port-master
workdir=$tld_workdir

export LDFLAGS="-lregex -ldl"

pre_configure() {
    workdir=$tld_workdir/autohell
    run aclocal
    run autoconf
    run libtoolize
    run automake --add-missing
    run autoreconf
    run rm config.sub
    get_up_to_date_config_sub

    pushd $port-master
    rm -fr Library
    git clone https://github.com/alimpfard/citron_standard_library.git Library
    popd
}

pre_install() {
    export workdir=$tld_workdir/autohell
}

install() {
    run make $installopts install
}
