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

checked javac -Xlint:all -Werror -sourcepath "$dirname" -d "$dirname" PartyClient.java
checked java -Xss256k -cp "$dirname" PartyClient "$CONCURRENCY"
