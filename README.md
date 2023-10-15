# Systems Programming
This is a repository for learning about systems programming.

### Lab 1
To run lab 1 code,
Please compile it with:
```
gcc -Wextra -Werror -Wall -Wno-gnu-folding-constant -ldl -rdynamic ./lab1/solution.c ./lab1/libcoro.c heap_help.c
```
And run it with the files you want using:\
PS: HHREPORT=l will enable memory leak checks. 
```
HHREPORT=l ./a.out ./lab1/test1.txt ./lab1/test2.txt ./lab1/test3.txt ./lab1/test4.txt ./lab1/test5.txt ./lab1/test6.txt 
```

To test results, please use checker.py.
```
python3 ./lab1/checker.py -f result.txt
```