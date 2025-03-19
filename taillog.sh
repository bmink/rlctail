#!/bin/sh

set -x
log stream --predicate 'process == "rlctail"' --style syslog --level info
