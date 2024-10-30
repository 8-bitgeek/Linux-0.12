# how to compile bochs with gui debugger

> If want to use gui debuug then can't compile with option --enable-gdb-stub
```bash 
./configure --prefix=/opt/bochs/ --enable-disasm --enable-iodebug --enable-x86-debugger-gui --with-x
```
