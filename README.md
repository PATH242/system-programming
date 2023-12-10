# Systems Programming
⇥   [Sorting on multiple files](#lab-1)\
⇥   [Simple terminal](#lab-2)\
⇥   [File system](#lab-3)\
⇥   [Thread pool](#lab-4)\
⇥   [Game lobby chat](#lab-5)
### Lab 1
C program that takes file names that contain numbers, and sorts them.

Please compile it with:
```
gcc -Wextra -Werror -Wall -Wno-gnu-folding-constant -ldl -rdynamic ./lab1/solution.c ./lab1/libcoro.c ./utils/heap_help.c
```
And run it with the files you want using:
```
./a.out ./lab1/test1.txt ./lab1/test2.txt ./lab1/test3.txt ./lab1/test4.txt ./lab1/test5.txt ./lab1/test6.txt 
```

To test results, please use checker.py.
```
python3 ./lab1/checker.py -f result.txt
```

⇥ Sorts numbers in each file with quick sort\
⇥ Uses merge sort to join files\
⇥ Result are stored in results.txt
### Lab 2
**Simple command line.**

Please compile it with
```
gcc -Wextra -Werror -Wall -Wno-gnu-folding-constant ./lab2/solution.c ./lab2/parser.c
```
and run it using 
```
./a.out 
```
and start using it as a terminal.\
If you need to exit, type exit.\
\
Command line also supports: \
⇥   *and, or operations: &&, ||* \
⇥   *pipes |* \
⇥   *background operations &*
### Lab 3
**Simple file system**\
supports:\
⇥   *Open via FD.* \
⇥   *Read via FD.* \
⇥   *Write via FD.* \
⇥   *Close FD.* \
⇥   *Delete via file name.*\
⇥   *permission flags.*\
Deleted files remain accessible through the fds opened on them before deletion, and are fully deleted after these fds referencing them close.

To run on tests:
```
cd lab3 && make && ./a.out
```

### Lab 4
**Thread pool**

To run on tests:
```
cd lab4 && make && ./a.out
```

supports\
⇥ Creating and deleting thread pools\
⇥ Gradual start of threads\
⇥ Push task to pool\
⇥ Join task, Timed Join task (timelimit) 

### Lab 5
**Game Lobby chat**
To run on tests
```
cd lab5 && make && ./test
```
To run server and client independently, use
```
./server
./client
```
supports\
⇥   *Multiple clients* \
⇥   *Long messages*\
⇥   *Incomplete messages' buffering*\
⇥   *Sending messages in batches*\
⇥   *Message author is attached to author*\
⇥   *Client sends its name once and it is attached to all its messages*

### Acknowledgments

This repository is built on the foundation of [sysprog](https://github.com/Gerold103/sysprog/tree/master) by [Vladislav Shpilevoy]. It is a systems programming course repo and tasks' descriptions and solutions' templates are authored by him.