#! /bin/bash

function fatal() {
    echo "FATAL: $*" >&2
    exit 1
}

function checked() {
    "$@" || fatal "$1 failed with $?: $*"
}

readonly canonical="$( readlink -m "$BASH_SOURCE" )" || fatal "readlink failed: $BASH_SOURCE"
readonly basename="${canonical##*/}"
readonly dirname="${canonical%/*}"

: ${CONCURRENCY:=1000}
: ${TOMCAT_BASE:=/usr/share/java}

function classpath() {
    local result=
    for pathname in "$@"; do
        checked test -f "$pathname"
        result="$result${result:+:}$pathname"
    done
    echo "$result"
}

classpath="$( classpath "$TOMCAT_BASE/"tomcat8-{servlet-api,catalina,coyote,api}.jar )" || fatal "classpath"
checked javac -Xlint:all -Werror -sourcepath "$dirname" -d "$dirname" -cp "$classpath" PartyServer.java

classpath="$( classpath "$TOMCAT_BASE/"tomcat8-{servlet-api,catalina,coyote,api,juli,util,util-scan,jni}.jar )" || fatal "classpath"
checked java -Xss256k -cp "$classpath:$dirname" PartyServer "$CONCURRENCY"
