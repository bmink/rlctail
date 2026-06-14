#!/bin/sh

#set -x
log stream --predicate 'process == "rlctail"' --style syslog --level info | stdbuf -o0 sed -e 's/^.*\]: //' | stdbuf -o0 grep -v "libinfo"
