# clang-struct – Indexed Structures of the Kernel

*clang-struct* is a [clang](https://clang.llvm.org/) plugin which indexes *structures*, its *members*, and *member uses* into an [sqlite3](https://www.sqlite.org/) database. *clang-struct* is primarily aimed, but not limited, to index the [Linux kernel](https://www.kernel.org/). When the code is indexed, it is easy to find unused members of structures or even whole structures.

## Building
Proceed with the standard [cmake](https://cmake.org) steps like:
```sh
mkdir build
cd build
cmake -G Ninja ..
ninja
```

## Filling in the Database
### Manually
1. Run `db_filler`
2. Run several `clang -cc1 -analyze -load clang-struct.so -analyzer-checker jirislaby.StructMembersChecker source.c` processes.
3. Stop `db_filler` by a `TERM/INT` signal

### In a Batch
A batch runner (to do all the steps) is also available in `scripts/run_commands.pl`. It needs `compile_commands.json` generated in the kernel using `make compile_commands.json`. For example this will generate the database:
```sh
make O=../build configure_the_kernel_as_usual
make O=../build compile_commands.json
cd ../build
export PATH=<path_to_clang-struct_scripts>:$PATH
export LD_LIBRARY_PATH=<path_to_clang-struct.so>
run_commands.pl
```

## Looking at the Results
### CLI – the Database
The resulting database is named `structs.db`. There are several views available, see the output of `sqlite3 structs.db .schema`. The content can be investigated for example by running these under `sqlite3 structs.db`:
```sql
SELECT * FROM use_view;
SELECT * FROM unused_view;
```

### Web Frontend
Also a web frontend exists in `frontend/`. It's written in [Ruby on Rails](https://rubyonrails.org/). Bundler is supposed to take care of bringing it up:
```sh
bundle install
bundle exec rails credentials:edit
bundle exec rake assets:precompile
bundle exec rails server -e production
```

Note that `libyaml-devel` and `ruby-devel` packages are likely needed for install to succeed.
