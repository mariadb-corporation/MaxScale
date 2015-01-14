#! /bin/bash
./harness -q -i hint_testing.input -c hint_testing.cnf -o hint_testing.output -t 1 -s 1 -q &>/dev/null
diff hint_testing.expected hint_testing.output &>/dev/null
if [[ "$?" == "0"  ]]
then
    echo "PASSED"
else
    echo "FAILED"
fi
