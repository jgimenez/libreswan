: ==== start ====
TESTNAME=ah-pluto-01
source /testing/pluto/bin/eastlocal.sh

ipsec setup start
/testing/pluto/bin/wait-until-pluto-started

ipsec auto --add westnet-eastnet-ah
ipsec whack --debug-control --debug-controlmore --debug-crypt
