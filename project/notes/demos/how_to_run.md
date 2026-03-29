# How to Run Any Demo
# Read this once — applies to every demo D1 through D12

---

## The 3 Steps (Same Every Time)

### Step 1 — Create the file
Inside the container terminal type:

```
cat > /workspace/project/demos/dXX_name.cc << 'EOF'
... paste the code here ...
EOF
```

What this does:
- cat >           = write everything that follows into this file
- << 'EOF'        = everything between here and the next EOF line becomes the file content
- The file appears instantly on your Windows laptop too (volume mount magic)

---

### Step 2 — Compile it

```
g++ -std=c++17 \
    -I /workspace/leveldb/include \
    -L /workspace/leveldb/build \
    /workspace/project/demos/dXX_name.cc \
    -o /workspace/project/demos/dXX \
    -lleveldb -lpthread -lsnappy
```

What each flag does:

```
g++                              the C++ compiler
                                 translates your .cc file into machine code

-std=c++17                       use C++ version 17
                                 SuRF headers require this version

-I /workspace/leveldb/include    where to find header files
                                 this is how #include "leveldb/db.h" works
                                 without -I the compiler cannot find the headers

-L /workspace/leveldb/build      where to find compiled libraries
                                 libleveldb.a lives here
                                 without -L the linker cannot find LevelDB

/workspace/project/demos/dXX.cc  your source file (input)

-o /workspace/project/demos/dXX  name the output program dXX
                                 without -o the output is named a.out by default

-lleveldb                        link against libleveldb.a
                                 this adds all compiled LevelDB code to your program

-lpthread                        link POSIX threads library
                                 LevelDB uses threads internally for compaction

-lsnappy                         link Snappy compression library
                                 LevelDB compresses data blocks with Snappy
```

---

### Step 3 — Run it

```
rm -rf /tmp/mydb && /workspace/project/demos/dXX
```

What this does:
- rm -rf /tmp/mydb   = delete any leftover database from previous run
                       so every run starts completely fresh
- &&                 = only run the next command if previous succeeded
                       if rm fails for some reason, program does not run
- /workspace/...dXX  = run the compiled program

---

## The Demo Files Structure

```
/workspace/project/demos/
    d01_open_close.cc      source code
    d01                    compiled binary
    d02_put.cc
    d02
    d03_get.cc
    d03
    ... and so on up to d12
```

---

## If Compile Fails

Common errors and fixes:

```
error: 'leveldb' is not a namespace
    Fix: check -I flag points to correct include path

undefined reference to leveldb::DB::Open
    Fix: check -L flag and -lleveldb are present

/tmp/mydb/LOCK: already held by process
    Fix: run rm -rf /tmp/mydb before running again
         or you forgot delete db in previous program
```
