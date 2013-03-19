#!/bin/sh

pass=0
count=0
fail=""

frame_echo() {
    r=$(echo "$@")
    echo $r | sed 's/./=/g'
    echo $r
    echo $r | sed 's/./=/g'
}

run_test() {
    name=$1
    shift
    "$@"
    res=$?
    if [ "$res" = 0 ]; then
	frame_echo "PASS: $name"
	pass=$(expr $pass + 1)
    else
	frame_echo "FAIL: $name"
	fail="$fail $name"
    fi
    count=$(expr $count + 1)
}

check_crash() {
    name=$1
    old=$2
    new=$3

    run_test "${name}-crashes" diff -uw $old $new
}


# kill kcm and digest-service to make sure we run new version
if sudo -n true ; then
    sudo killall -9 kcm digest-service
    sudo defaults write org.h5l.hx509 AllowHX509Validation -bool true
fi
defaults write org.h5l.hx509 AllowHX509Validation -bool true

crashuserold=$(mktemp /tmp/heimdal-crash-user-old-XXXXXX)
crashsystemold=$(mktemp /tmp/heimdal-crash-user-old-XXXXXX)
crashusernew=$(mktemp /tmp/heimdal-crash-user-new-XXXXXX)
crashsystemnew=$(mktemp /tmp/heimdal-crash-user-new-XXXXXX)
crashlogs=/Library/Logs/DiagnosticReports

(cd $HOME/$crashlogs && ls -1 ) > $crashuserold
(cd $crashlogs && ls -1 ) > $crashsystemold

# hcrypto
#for a in test_cipher ; do
#    run_test $a /usr/local/libexec/heimdal/bin/$a
#done

# commoncrypto
for a in test_scram test_ntlm ; do
    run_test $a /usr/local/libexec/heimdal/bin/$a
done

# base
for a in test_base ; do
    run_test $a /usr/local/libexec/heimdal/bin/$a
done

# libkrb5
for a in test-principal heimdal-test-cc ; do
    run_test $a /usr/local/libexec/heimdal/bin/$a
done

# check/kdc
for a in check-kdc check-kpasswdd ; do
    run_test $a /usr/local/libexec/heimdal/tests/kdc/$a
done

# check/gss
for a in check-basic check-context ; do
    run_test $a /usr/local/libexec/heimdal/tests/gss/$a
done

# check/apple
if sudo -n true ; then
    sudo launchctl unload /System/Library/LaunchDaemons/com.apple.Kerberos.kdc.plist

    trap "sudo launchctl load /System/Library/LaunchDaemons/com.apple.Kerberos.kdc.plist" SIGINT EXIT

    for a in check-apple-lkdc check-apple-hodadmin check-server-hodadmin check-apple-od ; do
	run_test $a sudo /usr/local/libexec/heimdal/tests/apple/$a
    done

    trap - SIGINT EXIT

    sudo launchctl load /System/Library/LaunchDaemons/com.apple.Kerberos.kdc.plist

    # server tests
    if test -f /System/Library/CoreServices/ServerVersion.plist ; then
	for a in check-apple-server ; do
	    run_test $a sudo /usr/local/libexec/heimdal/tests/apple/$a
	done
    fi

else
    echo "no running tests requiring sudo"
    fail="$fail no-sudo"
    count=$(expr $count + 1)
fi

# check apple non root
for a in check-apple-ad check-apple-dump check-apple-mitdump ; do
    run_test $a /usr/local/libexec/heimdal/tests/apple/$a
done

if sudo -n true ; then
    sudo defaults delete /Library/Preferences/org.h5l.hx509 AllowHX509Validation
fi
defaults delete /Library/Preferences/org.h5l.hx509 AllowHX509Validation

#
# Check for new crash logs
#

(cd $HOME/$crashlogs && ls -1 ) > $crashusernew
(cd $crashlogs && ls -1 ) > $crashsystemnew

check_crash system $crashsystemold $crashsystemnew
check_crash user $crashuserold $crashusernew

rm $crashusernew $crashuserold $crashsystemnew $crashsystemold

if expr "$count" = "$pass" > /dev/null; then
    frame_echo "All tests passed"
    exit 0
else
    frame_echo "tests failed:$fail"
    exit 1
fi
