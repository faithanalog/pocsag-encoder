#!/bin/bash

set -euv

make

TMP="$(mktemp -d)"

echo "Test - Two messages with newlines should succeed"

printf 'POCSAG512: Address:       1  Function: 3  Alpha:   hello
POCSAG512: Address:       3  Function: 3  Alpha:   world
' > "${TMP}/expected.txt"

printf "1:hello\n3:world" | ./pocsag | multimon-ng -c -a POCSAG512 -q - > "${TMP}/result.txt"

diff -q "${TMP}/expected.txt" "${TMP}/result.txt"



echo "Test - Two messages with \r\n should succeed"

printf 'POCSAG512: Address:       1  Function: 3  Alpha:   hello
POCSAG512: Address:       5  Function: 3  Alpha:   world
' > "${TMP}/expected.txt"

printf "1:hello\r\n5:world" | ./pocsag | multimon-ng -c -a POCSAG512 -q - > "${TMP}/result.txt"

diff -q "${TMP}/expected.txt" "${TMP}/result.txt"



echo "Test - Address with all 21-bits filled succeeds"

printf 'POCSAG512: Address: 2097151  Function: 3  Alpha:   biggest address<NUL><NUL>
' > "${TMP}/expected.txt"

printf "2097151:biggest address" | ./pocsag | multimon-ng -c -a POCSAG512 -q - > "${TMP}/result.txt"

diff -q "${TMP}/expected.txt" "${TMP}/result.txt"



echo "Test - Address bigger than 21 bits fails"

printf 'Address exceeds 21 bits: 2097152\n' > "${TMP}/expected.txt"

! ( printf "2097152:too big" | ./pocsag >/dev/null 2> "${TMP}/result.txt" )

diff -q "${TMP}/expected.txt" "${TMP}/result.txt"



echo "Test - Negative address fails"

printf 'Address exceeds 21 bits: 4294967295\n' > "${TMP}/expected.txt"

! ( printf '%s' "-1:too small" | ./pocsag >/dev/null 2> "${TMP}/result.txt" )

diff -q "${TMP}/expected.txt" "${TMP}/result.txt"



echo "Test - no messages prints nothing"

[[ "$(printf "" | ./pocsag | wc -c)" = 0 ]]


# Is this like, allowed??
echo "Test - Empty messages"

printf 'POCSAG512: Address:       1  Function: 3 
POCSAG512: Address:       2  Function: 3 
' > "${TMP}/expected.txt"

printf "1:\n2:" | ./pocsag | multimon-ng -c -a POCSAG512 -q - > "${TMP}/result.txt"

diff -q "${TMP}/expected.txt" "${TMP}/result.txt"


echo "Test - No colon is an error"

printf 'Malformed Line!\n' > "${TMP}/expected.txt"

! ( printf "1\n2:" | ./pocsag >/dev/null 2>"${TMP}/result.txt" )



# Yay

rm -rv "${TMP}/"
echo " === OK thats all the tests passing ==="
