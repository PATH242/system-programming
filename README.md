# Systems Programming
This is a repository for learning about systems programming.

### Lab 1
C program that takes file names with numbers, sorts each of them individually with quick sort, and then uses merge sort to merge them. The result will always be stored in results.txt. There's a checker script in ./lab1 that can be used to check numbers have been sorted correctly.

Please compile it with:
```
gcc -Wextra -Werror -Wall -Wno-gnu-folding-constant -ldl -rdynamic ./lab1/solution.c ./lab1/libcoro.c heap_help.c
```
And run it with the files you want using:\
*HHREPORT=l will enable memory leak checks.* 
```
HHREPORT=l ./a.out ./lab1/test1.txt ./lab1/test2.txt ./lab1/test3.txt ./lab1/test4.txt ./lab1/test5.txt ./lab1/test6.txt 
```

To test results, please use checker.py.
```
python3 ./lab1/checker.py -f result.txt
```

### Lab 2
Simplified version of a command line.

Please compile it with
```
gcc -Wextra -Werror -Wall -Wno-gnu-folding-constant ./lab2/solution.c ./lab2/parser.c
```
and run it using 
```
./a.out 
```
then start using it as a terminal. If you need to exit, type exit.